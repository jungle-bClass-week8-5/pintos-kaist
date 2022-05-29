#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include <console.h>

// 추가 : 시스템콜 전역변수 락
static struct lock sys_lock;
static bool use_sys_lock;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int read(int fd, void *buffer, unsigned size);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&sys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	/*
 인자 들어오는 순서:
 1번째 인자: %rdi
 2번째 인자: %rsi
 3번째 인자: %rdx
 4번째 인자: %r10
 5번째 인자: %r8
 6번째 인자: %r9
 */
	// TODO: Your implementation goes here.

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		/* code */
		break;
	case SYS_EXEC:
		/* code */
		break;
	case SYS_WAIT:
		/* code */
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		/* code */
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		/* code */
		break;
	case SYS_READ:
		/* code */
		break;
	case SYS_WRITE:
		/* code */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		/* code */
		break;
	case SYS_TELL:
		/* code */
		break;
	case SYS_CLOSE:
		/* code */
		break;
	case SYS_MMAP:
		/* code */
		break;
	case SYS_MUNMAP:
		/* code */
		break;
	case SYS_CHDIR:
		/* code */
		break;
	case SYS_MKDIR:
		/* code */
		break;
	case SYS_READDIR:
		/* code */
		break;
	case SYS_ISDIR:
		/* code */
		break;

	case SYS_INUMBER:
		/* code */
		break;

	case SYS_SYMLINK:
		/* code */
		break;

	case SYS_DUP2:
		/* code */
		break;

	case SYS_MOUNT:
		/* code */
		break;

	case SYS_UMOUNT:
		/* code */
		break;

	default:
		thread_exit();
		break;
	}
	// printf("system call!\n");
}
// 추가 : 시스템콜!
/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
void check_address(void *addr)
{
	if (!is_user_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}
/* 유저 스택에 있는 인자들을 커널에 저장하는 함수
스택 포인터(esp)에 count(인자의 개수) 만큼의 데이터를 arg에 저장 */
// void get_argument(void *rsp, int *arg, int count)
// {
// }

void halt(void)
{
	power_off();
}

void exit(int status)
{
	thread_current()->exit_status = status;
	thread_exit();
}

bool create(const char *file, unsigned initial_size)
{
	// printf("process create \n");
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	// printf("procces remove\n");
	check_address(file);
	return filesys_remove(file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	if (fd == 1)
	{
	}
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	if (fd == 0)
	{
		return -1;
	}
	if (fd == 1)
	{
		putbuf(buffer, size);
		return sizeof(buffer);
	}
	else
	{
		lock_acquire(&sys_lock);
		struct file *write_file = process_get_file(fd);

		lock_release(&sys_lock);
		return file_write(write_file, buffer, size);
	}
}
// file을 열고 성공하면 fd를 반환 하고 실패하면 -1을 반환
int open(const char *file)
{
	// file에 대한 이름이 인자로 옴
	check_address(file);
	struct file *openfile = filesys_open(file);
	if (openfile == NULL)
		return -1;
	return process_add_file(openfile);
}

int filesize(int fd)
{
	struct file *getfile = process_get_file(fd);
	if (getfile == NULL)
		return -1;
	return file_length(getfile);
}
