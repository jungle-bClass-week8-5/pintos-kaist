#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
	 Used to detect stack overflow.  See the big comment at the top
	 of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
	 Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
	 that are ready to run but not actually running. */
static struct list ready_list;

static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;	 /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;	 /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4					/* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
	 If true, use multi-level feedback queue scheduler.
	 Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

// 추가 : nice
int load_avg;
static struct list all_list;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

// // 수정 추가함수
// /* Thread를 blocked 상태로 만들고 sleep queue에 삽입하여 대기 */
void thread_sleep(int64_t ticks);
// /* Sleep queue에서 깨워야 할 thread를 찾아서 wake */
void thread_awake(int64_t ticks);
// /* Thread들이 가진 tick 값에서 최소 값을 저장 */
void update_next_tick_to_awake();
// /* 최소 tick값을 반환 */
int64_t get_next_tick_to_awake(void);

static long long next_tick_to_awake;

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
	 that's currently running into a thread.  This can't work in
	 general and it is possible in this case only because loader.S
	 was careful to put the bottom of the stack at a page boundary.

	 Also initializes the run queue and the tid lock.

	 After calling this function, be sure to initialize the page
	 allocator before trying to create any threads with
	 thread_create().

	 It is not safe to call thread_current() until this function
	 finishes. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
			.size = sizeof(gdt) - 1,
			.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	// 수정: sleep_list 추가
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&destruction_req);
	list_init(&all_list);

	// 수정: 추가 init
	list_init(&sleep_list);
	next_tick_to_awake = INT64_MAX;

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();

	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;

	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
	 Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);
	// 추가 : nice
	load_avg = LOAD_AVG_DEFAULT;
	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
	 Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	//
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
				 idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
	 PRIORITY, which executes FUNCTION passing AUX as the argument,
	 and adds it to the ready queue.  Returns the thread identifier
	 for the new thread, or TID_ERROR if creation fails.

	 If thread_start() has been called, then the new thread may be
	 scheduled before thread_create() returns.  It could even exit
	 before thread_create() returns.  Contrariwise, the original
	 thread may run for any amount of time before the new thread is
	 scheduled.  Use a semaphore or some other form of
	 synchronization if you need to ensure ordering.

	 The code provided sets the new thread's `priority' member to
	 PRIORITY, but no actual priority scheduling is implemented.
	 Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority,
										thread_func *function, void *aux)
{
	// 커널 스레드 == 프로세스
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	// t에 하나의 페이지 할당 PAL_ZERO-> 모든 페이지 바이트0으로 만듬
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);

	t->fdt = palloc_get_multiple(PAL_ZERO, 2);
	t->fdt[0] = 0;
	t->fdt[1] = 1;
	t->next_fd = 2;

	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// rip => 다음 실행할 명령어의 메모리 주소
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	// 추가: syscall
	struct thread *curr = thread_current();
	/* 부모 프로세스 저장 */
	/* 프로그램이 로드되지 않음 */
	/* 프로세스가 종료되지 않음 */
	/* exit 세마포어 0으로 초기화 */
	/* load 세마포어 0으로 초기화 */
	sema_init(&t->load_sema, 0);

	/* 자식 리스트에 추가 */
	list_push_back(&curr->child_list, &t->child_elem);

	/* Add to run queue. */
	thread_unblock(t);

	// 추가: syscall fork()

	/* 수정 추가함수 :우선순위 */
	if (t->priority > curr->priority)
	{
		thread_yield();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
	 again until awoken by thread_unblock().

	 This function must be called with interrupts turned off.  It
	 is usually a better idea to use one of the synchronization
	 primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
	 This is an error if T is not blocked.  (Use thread_yield() to
	 make the running thread ready.)

	 This function does not preempt the running thread.  This can
	 be important: if the caller had disabled interrupts itself,
	 it may expect that it can atomically unblock a thread and
	 update other data. */
//  block 된것을 ready로 바꾸로 readylist 로 넣어준다.
void thread_unblock(struct thread *t)
{
	// printf("thread_unblock\n");
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);
	// 수정
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);

	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
	 This is running_thread() plus a couple of sanity checks.
	 See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
		 If either of these assertions fire, then your thread may
		 have overflowed its stack.  Each thread has less than 4 kB
		 of stack, so a few big automatic arrays or moderate
		 recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
	 returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
		 We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	list_remove(&thread_current()->allelem);
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
	 may be scheduled again immediately at the scheduler's whim. */
//  현재 쓰레드가 sleep이 아니면
void thread_yield(void)
{
	// 현재 실행 중인 쓰레드 구조에 대한 포인터를 얻음
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable(); //인터럽트 비활성화
	if (curr != idle_thread)
		// 리스트에 끝에 넣는다.
		// 수정: 적절한 위에 삽입
		// list_push_back(&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL);
	// 우선순위 // 정렬하기 list_insert_ordered()
	//쓰레드의 상태를 ready로 변경하고 스케줄러 실행
	do_schedule(THREAD_READY);
	// 인터럽트를 다시 원래 상태로 만듬
	intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	// 수정: 우선순위
	// 추가
	if (thread_mlfqs)
	{
		return;
	}
	thread_current()->priority = new_priority;
	thread_current()->init_priority = new_priority;
	refresh_priority();

	donate_priority();

	test_max_priority();
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	/* 현제 스레드의 nice값을 변경하는 함수를 구현하다. 해당 작업중에 인터럽트는 비활성화 해야 한다. */
	/* 현제 스레드의 nice 값을 변경한다. nice 값 변경 후에 현재 스레드의 우선순위를 재계산 하고 우선순위에 의해 스케줄링 한다. */
	enum intr_level old_level = intr_disable();
	thread_current()->nice = nice;
	mlfqs_priority(thread_current());
	// 나이스 - 의심
	test_max_priority();
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	/* 현재 스레드의 nice 값을 반환한다.
	해당 작업중에 인터럽트는 비활성되어야 한다. */
	enum intr_level old_level = intr_disable();
	int nice = thread_current()->nice;
	intr_set_level(old_level);
	return nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	/* load_avg에 100을 곱해서 반환 한다.
	해당 과정중에 인터럽트는 비활성되어야 한다. */
	enum intr_level old_level = intr_disable();
	int temp = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return temp;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	/* recent_cpu 에 100을 곱해서 반환 한다.
	해당 과정중에 인터럽트는 비활성되어야 한다. */
	enum intr_level old_level = intr_disable();
	int temp = fp_to_int_round(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);
	return temp;
}

/* Idle thread.  Executes when no other thread is ready to run.

	 The idle thread is initially put on the ready list by
	 thread_start().  It will be scheduled once initially, at which
	 point it initializes idle_thread, "up"s the semaphore passed
	 to it to enable thread_start() to continue, and immediately
	 blocks.  After that, the idle thread never appears in the
	 ready list.  It is returned by next_thread_to_run() as a
	 special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

			 The `sti' instruction disables interrupts until the
			 completion of the next instruction, so these two
			 instructions are executed atomically.  This atomicity is
			 important; otherwise, an interrupt could be handled
			 between re-enabling interrupts and waiting for the next
			 one to occur, wasting as much as one clock tick worth of
			 time.

			 See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
			 7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt"
								 :
								 :
								 : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
// 스케줄러는 인터럽트가 꺼진 상태에서 실행
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
	 NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	// 스레드 sp 위치를 tf.rsp에 저장
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	//스레드 우선순위 초기화
	t->priority = priority;
	//스택 오버플로우 감지
	t->magic = THREAD_MAGIC;
	list_push_back(&all_list, &t->allelem);
	// 수정 : donation 초기화 코드
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);
	// 추가 : nice
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;

	// 추가 syscall
	t->exit_status = 0;
	list_init(&t->child_list);
	/* 자식 리스트 초기화 */
}

/* Chooses and returns the next thread to be scheduled.  Should
	 return a thread from the run queue, unless the run queue is
	 empty.  (If the running thread can continue running, then it
	 will be in the run queue.)  If the run queue is empty, return
	 idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
// iretq 인터럽트 리턴
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			:
			: "g"((uint64_t)tf)
			: "memory");
}

/* Switching the thread by activating the new thread's page
	 tables, and, if the previous thread is dying, destroying it.

	 At this function's invocation, we just switched from thread
	 PREV, the new thread is already running, and interrupts are
	 still disabled.

	 It's not safe to call printf() until the thread switch is
	 complete.  In practice that means that printf()s should be
	 added at the end of the function. */

// 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고 이전 스레드가 죽으면 소멸합니다.
// 이 함수를 호출할 때 PREV 스레드에서 방금 전환했고 새 스레드는 이미 실행 중이며 인터럽트는 여전히 비활성화되어 있습니다.
// 스레드 전환이 완료될 때까지 printf()를 호출하는 것은 안전하지 않습니다. 실제로는 printf()가 함수 끝에 추가되어야 함을 의미합니다.
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n" // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n" // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n" // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n" // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"	 // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			:
			: "g"(tf_cur), "g"(tf)
			: "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
				list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif
	// 현재스레드와 다음 스레드가 같지 않다.
	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
			 thread. This must happen late so that thread_exit() doesn't
			 pull out the rug under itself.
			 We just queuing the page free reqeust here because the page is
			 currently used bye the stack.
			 The real destruction logic will be called at the beginning of the
			 schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		// 스레드 전환 전 현재 실행 중인 정보를 저장 -> PCB 저장 부분
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	// tid를 만들 때 겹치지 않도록 lock을 만들고 tid를 만들고 풀어서 tid 리턴
	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

// 수정 추가 함수
void thread_sleep(int64_t ticks)
{

	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable(); //인터럽트 비활성화
	curr->wakeup_tick = ticks;

	if (curr != idle_thread)
	{
		list_push_back(&sleep_list, &curr->elem);
	}

	update_next_tick_to_awake();
	do_schedule(THREAD_BLOCKED);
	intr_set_level(old_level);
}

void thread_awake(int64_t ticks)
{
	next_tick_to_awake = INT64_MAX;

	struct list_elem *curr_thread_list = list_begin(&sleep_list);
	struct thread *curr_thread;

	while (curr_thread_list != list_end(&sleep_list))
	{
		enum intr_level old_level;
		curr_thread = list_entry(curr_thread_list, struct thread, elem);

		if (curr_thread->wakeup_tick <= ticks)
		{
			// old_level = intr_disable();
			// curr_thread->status = THREAD_READY;

			struct list_elem *next_list = list_remove(curr_thread_list);
			thread_unblock(curr_thread);
			// list_push_back(&ready_list, &curr_thread->elem);
			curr_thread_list = next_list;

			// intr_set_level(old_level);
		}
		else
		{
			// 다음꺼를 탐색해라
			curr_thread_list = curr_thread_list->next;
		}
	}
	update_next_tick_to_awake();
	// intr_set_level (old_level);
}

void update_next_tick_to_awake()
{
	struct list_elem *curr_thread_list = list_begin(&sleep_list);
	struct thread *curr_thread;

	int64_t min_ticks = 0;
	while (curr_thread_list)
	{
		curr_thread = list_entry(curr_thread_list, struct thread, elem);
		if (min_ticks == 0)
		{
			min_ticks = curr_thread->wakeup_tick;
		}
		else if (curr_thread->wakeup_tick < min_ticks)
		{
			min_ticks = curr_thread->wakeup_tick;
		}
		curr_thread_list = curr_thread_list->next;
	}

	next_tick_to_awake = min_ticks;
}

int64_t get_next_tick_to_awake(void)
{
	return next_tick_to_awake;
}

/* 추가함수 :우선순위 */
void test_max_priority(void)
{
	struct thread *curr_thread = thread_current();
	struct thread *most_priority_thread = curr_thread;
	if (!list_empty(&ready_list))
	{
		most_priority_thread = list_entry(list_front(&ready_list), struct thread, elem); // elem에 대한 thread를 가져다줌
	}

	if (most_priority_thread->priority > curr_thread->priority)
	{
		thread_yield();
	}
}
// list_insert_ordered() -> (정렬) 함수에 사용
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *a_thread = list_entry(a, struct thread, elem);
	struct thread *b_thread = list_entry(b, struct thread, elem);

	if (a_thread->priority > b_thread->priority)
	{
		return 1;
	}
	return 0;
}
// 우선순위가 높은 thread의 우선순위를 lock에 있는 thread들에게 기부
void donate_priority()
{
	int depth;
	// struct thread *cur = thread_current();

	// for (depth = 0; depth < 8; depth++)
	// {
	// 	if (!cur->wait_on_lock) // 기다리는 lock이 없다면 종료
	// 		break;
	// 	struct thread *holder = cur->wait_on_lock->holder;
	// 	holder->priority = cur->priority;
	// 	cur = holder;
	// }
	struct thread *curr = thread_current();
	// // lock을 잠군 thread
	// struct lock *lock_t = curr->wait_on_lock;
	struct lock *lock_t = curr->wait_on_lock;
	int i = 0;
	while (lock_t && i < 8)
	{
		if (lock_t && curr->priority > lock_t->holder->priority)
		{
			lock_t->holder->priority = curr->priority;
			lock_t = lock_t->holder->wait_on_lock;
			i++;
		}
	}
}
// 추가 : 나이스
// mlfqs_calculate_priority
void mlfqs_priority(struct thread *t)
{
	if (t == idle_thread)
		return;
	// PRI_MAX – (recent_cpu / 4) – (nice * 2)
	t->priority = fp_to_int(add_mixed(div_mixed(t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}
// mlfqs_calculate_recent_cpu
void mlfqs_recent_cpu(struct thread *t)
{
	if (t == idle_thread)
		return;
	// recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
	t->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed(load_avg, 2),
																					 add_mixed(mult_mixed(load_avg, 2), 1)),
																		t->recent_cpu),
														t->nice);
}
// mlfqs_calculate_load_avg
void mlfqs_load_avg(void)
{
	int ready_threads;
	// idle 스레드 인지 확인하여 ready리스트의 개수를 센다. idle은 실행 가능한 스레드로 취급하지 않는다.
	if (thread_current() == idle_thread)
		ready_threads = list_size(&ready_list);
	else
		ready_threads = list_size(&ready_list) + 1;

	// load_avg = (59/60) * load_avg + (1/60) * ready_threads
	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads));
}
// mlfqs_increment_recent_cpu
void mlfqs_increment(void)
{
	if (thread_current() == idle_thread)
		return;
	thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
}
// mlfqs_recalculate_recent_cpu
void mlfqs_recalc(void)
{
	/* 모든 thread의 recent_cpu와 priority값 재계산 한다. */
	struct list_elem *start;
	for (start = list_begin(&all_list); start != list_end(&all_list); start = list_next(start))
	{
		struct thread *t = list_entry(start, struct thread, allelem);
		mlfqs_recent_cpu(t);
	}
}
// mlfqs_recalculate_priority
void mlfqs_recalculate_priority(void)
{
	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, allelem);
		mlfqs_priority(t);
	}
}
