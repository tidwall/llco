#ifndef LLCO_H
#define LLCO_H

#include <stdbool.h>
#include <stddef.h>

#define LLCO_MINSTACKSIZE 16384

struct llco_desc {
    void *stack;
    size_t stack_size;
    void (*entry)(void *udata);
    void (*cleanup)(void *stack, size_t stack_size, void *udata);
    void *udata;
};

struct llco;

struct llco *llco_current(void);
void llco_start(struct llco_desc *desc, bool final);
void llco_switch(struct llco *co, bool final);
const char *llco_method(void *caps);

// Coroutine stack unwinding
struct llco_symbol {
    void *cfa;            // Canonical Frame Address
    void *ip;             // Instruction Pointer
    const char *fname;    // Pathname of shared object
    void *fbase;          // Base address of shared object
    const char *sname;    // Name of nearest symbol
    void *saddr;          // Address of nearest symbol
};

int llco_unwind(bool (*func)(struct llco_symbol *));

#endif // LLCO_H
