#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void check_address(void *addr);
static struct lock sys_lock;
#endif /* userprog/syscall.h */
