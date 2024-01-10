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

#endif // LLCO_H
