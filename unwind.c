#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "llco.h"

#define STKSZ 32768

void cleanup(void *stk, size_t stksz, void *udata) {
    printf("== CLEANUP ==\n");
    free(stk);
}


bool symbol(struct llco_symbol *sym) {
    printf("== UNWIND ==\n");
    printf("  cfa:           %p\n", sym->cfa);
    printf("  ip:            %p\n", sym->ip);
    printf("  fname:         %s\n", sym->fname);
    printf("  fbase:         %p\n", sym->fbase);
    printf("  sname:         %s\n", sym->sname);
    printf("  saddr:         %p\n", sym->saddr);
    return true;
}

__attribute__((noinline)) void func4(int x) { 
    printf("func4 (%d)\n", x); 
    printf(">> %d\n", llco_unwind(symbol));
    // _Unwind_Backtrace(btfnc, NULL);
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
    printf(">> %d\n", llco_unwind(symbol));
    return 0;
}

