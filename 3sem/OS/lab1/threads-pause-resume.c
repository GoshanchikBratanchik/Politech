/*
  File for 'threads-pause-resume' task implementation.
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

void infinity_cycle(void *aux) {
    while(1) {
        msg("%s\n", aux);
        timer_sleep(100);
    }
}

void test_threads_pause_resume(void) 
{
  msg("Test thread_pause_resume\n");
  tid_t t1 = thread_create("thread 1", PRI_DEFAULT, infinity_cycle, "thread 1");
  tid_t t2 = thread_create("thread 2", PRI_DEFAULT, infinity_cycle, "thread 2");
  timer_sleep(400);
  msg("Pause thread thread 1");
  thread_pause(t1);
  timer_sleep(400);
  msg("Resume thread thread 1");
  thread_resume(t1);
  timer_sleep(400);
}
