#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#define ULIMIT (1<<20)
// typedef int pid_t;
void syscall_init (void);
bool is_open_file_executing(const char* file);

extern struct semaphore* file_handle_lock;
#endif /* userprog/syscall.h */
