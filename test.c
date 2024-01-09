#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
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

int main(void) {
    printf("%s\n", llco_method(0));
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
        printf("PASSED\n");
    }
    return 0;
}

