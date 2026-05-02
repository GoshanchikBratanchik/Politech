/*
  File for 'max-rec-calls' task implementation.
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static void rec(int deth) {
    char a, b;

    msg("deth = %d\n", deth);
    rec(deth + 1);
}

static void test_thread(void* aux UNUSED) {
    rec(1);
}

void test_max_rec_calls(void)
{
    thread_create("rec", PRI_DEFAULT, test_thread, NULL);
    thread_exit();
}
