#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/interrupt.h"
tid_t process_execute (const char *file_name);
int process_wait (tid_t tid);
void process_exit (void);
void process_activate (void);
bool handle_mm_fault(uint32_t* uaddr,uint32_t *sp);

extern struct list lru_list;
void init_lru();
#endif /* userprog/process.h */
