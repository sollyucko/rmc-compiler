// Test whether a system call seems to act as a fence.

#define _GNU_SOURCE
#include "atomic.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

volatile long x = 0;
volatile long y = 0;

int thread0()
{
    x = 1;
    syscall(-1);
    return y;
}

int thread1()
{
    y = 1;
    syscall(-1);
    return x;
}

void reset()
{
    x = y = 0;
}

// Formatting results
int result_counts[4];

void process_results(int *r)
{
    int idx = (r[1]<<1) | r[0];
    result_counts[idx]++;
}

void summarize_results()
{
    for (int r0 = 0; r0 <= 1; r0++) {
        for (int r1 = 0; r1 <= 1; r1++) {
            int idx = (r1<<1) | r0;
            printf("r0=%d r1=%d: %d\n", r0, r1, result_counts[idx]);
        }
    }
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1};

int thread_count = 2;
