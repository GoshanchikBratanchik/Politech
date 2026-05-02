
/*
  File for 'test-new-alg' task implementation.
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/timer.h"

static thread_func thread_f;

void test_new_alg(void)
{
    msg("Test new alg begin");
    thread_set_priority(PRI_MAX);
    thread_create_burst("thread 1", 45, 4, thread_f, NULL);
    thread_create_burst("thread 2", 3, 3, thread_f, NULL);
    thread_create_burst("thread 3", 46, 1, thread_f, NULL);
    thread_create_burst("thread 4", 4, 6, thread_f, NULL);
    msg("Main waiting...");
    thread_set_priority(PRI_MIN);
}

static void thread_f(void *aux UNUSED)
{
    struct thread *cur = thread_current();

    while (1) {

        msg("%s running: work time = %d", cur->name, cur->ticks);
	timer_sleep(4);
         
    }
}

