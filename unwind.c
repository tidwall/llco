#include <stdlib.h>
#include <stdio.h>
#include <unwind.h>
#include "llco.h"


#define STKSZ 32768

void cleanup(void *stk, size_t stksz, void *udata) {
    printf("== CLEANUP ==\n");
    free(stk);
}

static _Unwind_Reason_Code btfnc(struct _Unwind_Context *uwc, void *ptr) {
    printf("== UNWIND ==\n");
    return _URC_END_OF_STACK;
}

__attribute__((noinline)) void func4(int x) { 
    printf("func4 (%d)\n", x); 

    _Unwind_Backtrace(btfnc, NULL);
}

__attribute__((noinline)) void func3(int x) { printf("func3\n"); func4(x); }
__attribute__((noinline)) void func2(int x) { printf("func2\n"); func3(x); }
__attribute__((noinline)) void func1(int x) { printf("func1\n"); func2(x); }


// __attribute__((noinline))
void entry(void *udata) {
    printf("== COROUTINE ==\n");
    func1(10);
    printf("== SWITCH TO MAIN ==\n");
    llco_switch(0, true);
}

int main(void) {
    printf("%s\n", llco_method(0));
    printf("== MAIN ==\n");
    struct llco_desc desc = {
        .stack = malloc(STKSZ),
        .stack_size = STKSZ,
        .entry = entry,
        .cleanup = cleanup,
        .udata = (void*)1,
    };
    llco_start(&desc, false);
    printf("== EXIT ==\n");
    return 0;
}

