/* Verifies that lowering a thread's priority so that it is no
   longer the highest-priority thread in the system causes it to
   yield immediately. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/thread.h"

static thread_func changing_thread;

void
test_priority_change (void) 
{
  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  msg ("Creating a high-priority thread 2."); // 1 main
  thread_create ("thread 2", PRI_DEFAULT + 1, changing_thread, NULL); // 1.5
  msg ("Thread 2 should have just lowered its priority."); // 3 main
  thread_set_priority (PRI_DEFAULT - 2); // 3.5 main
  msg ("Thread 2 should have just exited."); // 5 
}

static void
changing_thread (void *aux UNUSED) 
{
  msg ("Thread 2 now lowering priority."); // 2
  thread_set_priority (PRI_DEFAULT - 1);
  msg ("Thread 2 exiting."); // 4
}
