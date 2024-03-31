#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "llco.h"

#define STKSZ 32768

char tstr[512];
char tresult[4096] = { 0 };

#define tprintf(...) { \
    printf(__VA_ARGS__); \
    snprintf(tstr, sizeof(tstr), __VA_ARGS__); \
    strcat(tresult, tstr); \
} \

void cleanup(void *stk, size_t stksz, void *udata) {
    tprintf("(cleanup %d)\n", (int)(intptr_t)udata);
    free(stk);
}

struct llco *co1, *co2, *co3;

void entry3(void *udata) {
    co3 = llco_current();
    tprintf("(entry %d)\n", (int)(intptr_t)udata);
    llco_switch(co1, false);
    tprintf("(mark C)\n");
    llco_switch(co1, true);
}

void entry2(void *udata) {
    co2 = llco_current();
    tprintf("(entry %d)\n", (int)(intptr_t)udata);
    struct llco_desc desc = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = entry3,
        .cleanup = cleanup,
        .udata = (void*)3,
    };
    llco_start(&desc, true);
}

void entry1(void *udata) {
    co1 = llco_current();
    tprintf("(entry %d)\n", (int)(intptr_t)udata);
    struct llco_desc desc = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = entry2,
        .cleanup = cleanup,
        .udata = (void*)2,
    };
    llco_start(&desc, false);
    tprintf("(mark B)\n");
    llco_switch(co3, false);
    tprintf("(mark D)\n");
    llco_switch(0, true);
}

void perfcleanup(void *stk, size_t stksz, void *udata) {
    free(stk);
}

// Return monotonic nanoseconds of the CPU clock.
static int64_t getnow(void) {
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
}

int64_t perfstart, perfend;

struct llco *perf1co;
struct llco *perf2co;

#define NSWITCHES 10000000
int perf_count = 0;

void perf1(void *udata) {
    perf1co = llco_current();
    llco_switch(0, 0);
    while (perf_count < NSWITCHES) {
        perf_count++;
        llco_switch(perf2co, 0);
    }
    llco_switch(0, 1);
}

void perf2(void *udata) {
    perf2co = llco_current();
    perfstart = getnow();
    while (perf_count < NSWITCHES) {
        perf_count++;
        llco_switch(perf1co, 0);
    }
    perfend = getnow();
    llco_switch(perf1co, 1);
}

void test_perf() {
    struct llco_desc desc1 = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = perf1,
        .cleanup = perfcleanup,
        .udata = (void*)1,
    };
    llco_start(&desc1, false);

    struct llco_desc desc2 = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = perf2,
        .cleanup = perfcleanup,
        .udata = (void*)2,
    };
    llco_start(&desc2, false);

    printf("perf: %ld switches in %.3f secs, %ld ns / switch\n", 
        (long)perf_count, (double)(perfend-perfstart)/1e9, 
        (long)((perfend-perfstart) / perf_count));

}

int main(void) {
    printf("%s\n", llco_method(0));
    assert(!llco_current());
    tprintf("(mark A)\n");
    struct llco_desc desc = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = entry1,
        .cleanup = cleanup,
        .udata = (void*)1,
    };
    llco_start(&desc, false);
    tprintf("(mark E)\n");
    assert(!llco_current());
    const char *exp = 
        "(mark A)\n"
        "(entry 1)\n"
        "(entry 2)\n"
        "(cleanup 2)\n"
        "(entry 3)\n"
        "(mark B)\n"
        "(mark C)\n"
        "(cleanup 3)\n"
        "(mark D)\n"
        "(cleanup 1)\n"
        "(mark E)\n";
    if (strcmp(tresult, exp) != 0) {
        printf("== expected ==\n%s", exp);
        printf("FAILED\n");
    } else {
        test_perf();
        printf("PASSED\n");
    }
    return 0;
}

