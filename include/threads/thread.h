#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,		/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING		/* About to be destroyed. */
};

/* Thread identifier type.
	 You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0			 /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63		 /* Highest priority. */

// 추가 : nice
#define PRI_MAX 63
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */

struct thread
{
	/* Owned by thread.c. */
	tid_t tid;								 /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];						 /* Name (for debugging purposes). */
	int priority;							 /* Priority. */
	// 추가: donation
	// 초기 우선순위 저장 필드
	int init_priority;
	// 해당 쓰레드가 대기하고 있는 lock자료구조
	struct lock *wait_on_lock;
	struct list_elem d_elem; /* List element. */
	struct list donations;

	// 추가: nice
	int nice;
	int recent_cpu;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */
	struct list_elem allelem;

	// 추가 syscall
	int exit_status;	 // thread 종료 상태 저장
	struct file **fdt; // file discripter table 초기 0, 1
	int next_fd;			 // 추가될 next_fd 저장

	struct list child_list;
	struct list_elem child_elem;
	tid_t parent_tid;
	bool mem_alloc_bool;

	struct semaphore *load_sema;
	// struct semaphore exit_sema; //????

	// 프로세스의 생성 성공 여부를 확인하는 플래그 추가 (실행 파일이 로드에 실패하면 -1)
	// 프로세스의 종료 유무를 확인하는 필드 추가
	// 프로세스의 종료 상태를 나타내는 필드 추가 -> 이건 추가한거 같은데 exit_thread
	// 자식 프로세스의 생성/종료 대기를 위한 세마포어 추가
	// 자식 프로세스 리스트 필드 추가 : 자식 리스트, 자식 리스트 element
	// 부모 프로세스 디스크립터를 가리키는 필드 추가
	/* 부모 프로세스의 디스크립터 */

	/* 자식 리스트 element */
	/* 자식 리스트 */
	/* 프로세스의 프로그램 메모리 적재 유무 */
	/* 프로세스가 종료 유무 확인 */
	/* exit 세마포어 */
	/* load 세마포어 */
	/* exit 호출 시 종료 status */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
									// // 추가 syscall

#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif
	// 수정: 언제 깨울지 확인하는 변수
	int64_t wakeup_tick;
	/* Owned by thread.c. */
	// intr_frame은 실행중인 프로세스의 register 정보, stack pointer, instruction counter를 저장하는 자료구조
	struct intr_frame tf; /* Information for switching */
	unsigned magic;				/* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
	 If true, use multi-level feedback queue scheduler.
	 Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

/* 수정 추가 함수 : 알람  */
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick_to_awake();
int64_t get_next_tick_to_awake(void);

/* 수정 추가함수 :우선순위 */
void test_max_priority(void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

// 수정 추가함수 : donation
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);

// 수정 추가함수 : 나이스
void mlfqs_increment(void);
void mlfqs_recalc(void);
void mlfqs_recalculate_priority(void);
#endif /* threads/thread.h */
