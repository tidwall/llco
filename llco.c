// https://github.com/tidwall/llco
//
// Copyright (c) 2024 Joshua J Baker.
// This software is available as a choice of Public Domain or MIT-0.

#ifdef _FORTIFY_SOURCE
#define LLCO_FORTIFY_SOURCE _FORTIFY_SOURCE
// Disable __longjmp_chk validation so that we can jump between stacks.
#pragma push_macro("_FORTIFY_SOURCE")
#undef _FORTIFY_SOURCE
#include <setjmp.h>
#define _FORTIFY_SOURCE LLCO_FORTIFY_SOURCE
#undef LLCO_FORTIFY_SOURCE
#pragma pop_macro("_FORTIFY_SOURCE")
#endif

#ifndef LLCO_STATIC
#include "llco.h"
#else
#include <stddef.h>
#include <stdbool.h>
#define LLCO_MINSTACKSIZE 16384
#define LLCO_EXTERN static
struct llco_desc {
    void *stack;
    size_t stack_size;
    void (*entry)(void *udata);
    void (*cleanup)(void *stack, size_t stack_size, void *udata);
    void *udata;
};
struct llco_symbol {
    void *cfa;
    void *ip;
    const char *fname;
    void *fbase;
    const char *sname;
    void *saddr;
};
#endif

#include <stdlib.h>

#ifdef LLCO_VALGRIND
#include <valgrind/valgrind.h>
#endif

#ifndef LLCO_EXTERN
#define LLCO_EXTERN
#endif

#if defined(__GNUC__)
#ifdef noinline
#define LLCO_NOINLINE noinline
#else
#define LLCO_NOINLINE __attribute__ ((noinline))
#endif
#ifdef noreturn
#define LLCO_NORETURN noreturn
#else
#define LLCO_NORETURN __attribute__ ((noreturn))
#endif
#else
#define LLCO_NOINLINE
#define LLCO_NORETURN
#endif

#if defined(_MSC_VER)
#define __thread __declspec(thread)
#endif

static void llco_entry(void *arg);

LLCO_NORETURN
static void llco_exit(void) {
    _Exit(0);
}

#ifdef LLCO_ASM
#error LLCO_ASM must not be defined
#endif

// Passing the entry function into assembly requires casting the function 
// pointer to an object pointer, which is forbidden in the ISO C spec but
// allowed in posix. Ignore the warning attributed to this  requirement when
// the -pedantic compiler flag is provide.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

////////////////////////////////////////////////////////////////////////////////
// Below is various assembly code adapted from the Lua Coco [MIT] and Minicoro
// [MIT-0] projects by Mike Pall and Eduardo Bart respectively.
////////////////////////////////////////////////////////////////////////////////

/*
Lua Coco (coco.luajit.org) 
Copyright (C) 2004-2016 Mike Pall. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

////////////////////////////////////////////////////////////////////////////////
// ARM
////////////////////////////////////////////////////////////////////////////////
#if defined(__ARM_EABI__) && !defined(LLCO_NOASM)
#define LLCO_ASM
#define LLCO_READY
#define LLCO_METHOD "asm,arm_eabi"

struct llco_asmctx {
#ifndef __SOFTFP__
    void* f[16];
#endif
    void *d[4]; /* d8-d15 */
    void *r[4]; /* r4-r11 */
    void *lr;
    void *sp;
};

void _llco_asm_entry(void);
int _llco_asm_switch(struct llco_asmctx *from, struct llco_asmctx *to);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __llco_asm_switch\n"
    "__llco_asm_switch:\n"
#else
    ".globl _llco_asm_switch\n"
    ".type _llco_asm_switch #function\n"
    ".hidden _llco_asm_switch\n"
    "_llco_asm_switch:\n"
#endif
#ifndef __SOFTFP__
    "  vstmia r0!, {d8-d15}\n"
#endif
    "  stmia r0, {r4-r11, lr}\n"
    "  str sp, [r0, #9*4]\n"
#ifndef __SOFTFP__
    "  vldmia r1!, {d8-d15}\n"
#endif
    "  ldr sp, [r1, #9*4]\n"
    "  ldmia r1, {r4-r11, pc}\n"
#ifndef __APPLE__
    ".size _llco_asm_switch, .-_llco_asm_switch\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __llco_asm_entry\n"
    "__llco_asm_entry:\n"
#else
    ".globl _llco_asm_entry\n"
    ".type _llco_asm_entry #function\n"
    ".hidden _llco_asm_entry\n"
    "_llco_asm_entry:\n"
#endif
    "  mov r0, r4\n"
    "  mov ip, r5\n"
    "  mov lr, r6\n"
    "  bx ip\n"
#ifndef __APPLE__
    ".size _llco_asm_entry, .-_llco_asm_entry\n"
#endif
);

static void llco_asmctx_make(struct llco_asmctx *ctx, void* stack_base, 
    size_t stack_size, void *arg)
{
    ctx->d[0] = (void*)(arg);
    ctx->d[1] = (void*)(llco_entry);
    ctx->d[2] = (void*)(0xdeaddead); /* Dummy return address. */
    ctx->lr = (void*)(_llco_asm_entry);
    ctx->sp = (void*)((size_t)stack_base + stack_size);
}

#endif

////////////////////////////////////////////////////////////////////////////////
// ARM 64-bit
////////////////////////////////////////////////////////////////////////////////
#if defined(__aarch64__) && !defined(LLCO_NOASM)
#define LLCO_ASM
#define LLCO_READY
#define LLCO_METHOD "asm,aarch64"

struct llco_asmctx {
    void *x[12]; /* x19-x30 */
    void *sp;
    void *lr;
    void *d[8]; /* d8-d15 */
};

void _llco_asm_entry(void);
int _llco_asm_switch(struct llco_asmctx *from, struct llco_asmctx *to);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __llco_asm_switch\n"
    "__llco_asm_switch:\n"
#else
    ".globl _llco_asm_switch\n"
    ".type _llco_asm_switch #function\n"
    ".hidden _llco_asm_switch\n"
    "_llco_asm_switch:\n"
#endif

    "  mov x10, sp\n"
    "  mov x11, x30\n"
    "  stp x19, x20, [x0, #(0*16)]\n"
    "  stp x21, x22, [x0, #(1*16)]\n"
    "  stp d8, d9, [x0, #(7*16)]\n"
    "  stp x23, x24, [x0, #(2*16)]\n"
    "  stp d10, d11, [x0, #(8*16)]\n"
    "  stp x25, x26, [x0, #(3*16)]\n"
    "  stp d12, d13, [x0, #(9*16)]\n"
    "  stp x27, x28, [x0, #(4*16)]\n"
    "  stp d14, d15, [x0, #(10*16)]\n"
    "  stp x29, x30, [x0, #(5*16)]\n"
    "  stp x10, x11, [x0, #(6*16)]\n"
    "  ldp x19, x20, [x1, #(0*16)]\n"
    "  ldp x21, x22, [x1, #(1*16)]\n"
    "  ldp d8, d9, [x1, #(7*16)]\n"
    "  ldp x23, x24, [x1, #(2*16)]\n"
    "  ldp d10, d11, [x1, #(8*16)]\n"
    "  ldp x25, x26, [x1, #(3*16)]\n"
    "  ldp d12, d13, [x1, #(9*16)]\n"
    "  ldp x27, x28, [x1, #(4*16)]\n"
    "  ldp d14, d15, [x1, #(10*16)]\n"
    "  ldp x29, x30, [x1, #(5*16)]\n"
    "  ldp x10, x11, [x1, #(6*16)]\n"
    "  mov sp, x10\n"
    "  br x11\n"
#ifndef __APPLE__
    ".size _llco_asm_switch, .-_llco_asm_switch\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __llco_asm_entry\n"
    "__llco_asm_entry:\n"
#else
    ".globl _llco_asm_entry\n"
    ".type _llco_asm_entry #function\n"
    ".hidden _llco_asm_entry\n"
    "_llco_asm_entry:\n"
#endif
    "  mov x0, x19\n"
    "  mov x30, x21\n"
    "  br x20\n"
#ifndef __APPLE__
    ".size _llco_asm_entry, .-_llco_asm_entry\n"
#endif
);

static void llco_asmctx_make(struct llco_asmctx *ctx, void* stack_base,
  size_t stack_size, void *arg)
{
    ctx->x[0] = (void*)(arg);
    ctx->x[1] = (void*)(llco_entry);
    ctx->x[2] = (void*)(0xdeaddeaddeaddead); /* Dummy return address. */
    ctx->sp = (void*)((size_t)stack_base + stack_size);
    ctx->lr = (void*)(_llco_asm_entry);
}
#endif 

////////////////////////////////////////////////////////////////////////////////
// RISC-V (rv64/rv32)
////////////////////////////////////////////////////////////////////////////////
#if defined(__riscv) && !defined(LLCO_NOASM)
#define LLCO_ASM
#define LLCO_READY
#define LLCO_METHOD "asm,riscv"

struct llco_asmctx {
    void* s[12]; /* s0-s11 */
    void* ra;
    void* pc;
    void* sp;
#ifdef __riscv_flen
#if __riscv_flen == 64
    double fs[12]; /* fs0-fs11 */
#elif __riscv_flen == 32
    float fs[12]; /* fs0-fs11 */
#endif
#endif /* __riscv_flen */
};

void _llco_asm_entry(void);
int _llco_asm_switch(struct llco_asmctx *from, struct llco_asmctx *to);

__asm__(
    ".text\n"
    ".globl _llco_asm_entry\n"
    ".type _llco_asm_entry @function\n"
    ".hidden _llco_asm_entry\n"
    "_llco_asm_entry:\n"
    "  mv a0, s0\n"
    "  jr s1\n"
    ".size _llco_asm_entry, .-_llco_asm_entry\n"
);

__asm__(
    ".text\n"
    ".globl _llco_asm_switch\n"
    ".type _llco_asm_switch @function\n"
    ".hidden _llco_asm_switch\n"
    "_llco_asm_switch:\n"
#if __riscv_xlen == 64
    "  sd s0, 0x00(a0)\n"
    "  sd s1, 0x08(a0)\n"
    "  sd s2, 0x10(a0)\n"
    "  sd s3, 0x18(a0)\n"
    "  sd s4, 0x20(a0)\n"
    "  sd s5, 0x28(a0)\n"
    "  sd s6, 0x30(a0)\n"
    "  sd s7, 0x38(a0)\n"
    "  sd s8, 0x40(a0)\n"
    "  sd s9, 0x48(a0)\n"
    "  sd s10, 0x50(a0)\n"
    "  sd s11, 0x58(a0)\n"
    "  sd ra, 0x60(a0)\n"
    "  sd ra, 0x68(a0)\n" /* pc */
    "  sd sp, 0x70(a0)\n"
#ifdef __riscv_flen
#if __riscv_flen == 64
    "  fsd fs0, 0x78(a0)\n"
    "  fsd fs1, 0x80(a0)\n"
    "  fsd fs2, 0x88(a0)\n"
    "  fsd fs3, 0x90(a0)\n"
    "  fsd fs4, 0x98(a0)\n"
    "  fsd fs5, 0xa0(a0)\n"
    "  fsd fs6, 0xa8(a0)\n"
    "  fsd fs7, 0xb0(a0)\n"
    "  fsd fs8, 0xb8(a0)\n"
    "  fsd fs9, 0xc0(a0)\n"
    "  fsd fs10, 0xc8(a0)\n"
    "  fsd fs11, 0xd0(a0)\n"
    "  fld fs0, 0x78(a1)\n"
    "  fld fs1, 0x80(a1)\n"
    "  fld fs2, 0x88(a1)\n"
    "  fld fs3, 0x90(a1)\n"
    "  fld fs4, 0x98(a1)\n"
    "  fld fs5, 0xa0(a1)\n"
    "  fld fs6, 0xa8(a1)\n"
    "  fld fs7, 0xb0(a1)\n"
    "  fld fs8, 0xb8(a1)\n"
    "  fld fs9, 0xc0(a1)\n"
    "  fld fs10, 0xc8(a1)\n"
    "  fld fs11, 0xd0(a1)\n"
#else
#error "Unsupported RISC-V FLEN"
#endif
#endif /* __riscv_flen */
    "  ld s0, 0x00(a1)\n"
    "  ld s1, 0x08(a1)\n"
    "  ld s2, 0x10(a1)\n"
    "  ld s3, 0x18(a1)\n"
    "  ld s4, 0x20(a1)\n"
    "  ld s5, 0x28(a1)\n"
    "  ld s6, 0x30(a1)\n"
    "  ld s7, 0x38(a1)\n"
    "  ld s8, 0x40(a1)\n"
    "  ld s9, 0x48(a1)\n"
    "  ld s10, 0x50(a1)\n"
    "  ld s11, 0x58(a1)\n"
    "  ld ra, 0x60(a1)\n"
    "  ld a2, 0x68(a1)\n" /* pc */
    "  ld sp, 0x70(a1)\n"
    "  jr a2\n"
#elif __riscv_xlen == 32
    "  sw s0, 0x00(a0)\n"
    "  sw s1, 0x04(a0)\n"
    "  sw s2, 0x08(a0)\n"
    "  sw s3, 0x0c(a0)\n"
    "  sw s4, 0x10(a0)\n"
    "  sw s5, 0x14(a0)\n"
    "  sw s6, 0x18(a0)\n"
    "  sw s7, 0x1c(a0)\n"
    "  sw s8, 0x20(a0)\n"
    "  sw s9, 0x24(a0)\n"
    "  sw s10, 0x28(a0)\n"
    "  sw s11, 0x2c(a0)\n"
    "  sw ra, 0x30(a0)\n"
    "  sw ra, 0x34(a0)\n" /* pc */
    "  sw sp, 0x38(a0)\n"
#ifdef __riscv_flen
#if __riscv_flen == 64
    "  fsd fs0, 0x3c(a0)\n"
    "  fsd fs1, 0x44(a0)\n"
    "  fsd fs2, 0x4c(a0)\n"
    "  fsd fs3, 0x54(a0)\n"
    "  fsd fs4, 0x5c(a0)\n"
    "  fsd fs5, 0x64(a0)\n"
    "  fsd fs6, 0x6c(a0)\n"
    "  fsd fs7, 0x74(a0)\n"
    "  fsd fs8, 0x7c(a0)\n"
    "  fsd fs9, 0x84(a0)\n"
    "  fsd fs10, 0x8c(a0)\n"
    "  fsd fs11, 0x94(a0)\n"
    "  fld fs0, 0x3c(a1)\n"
    "  fld fs1, 0x44(a1)\n"
    "  fld fs2, 0x4c(a1)\n"
    "  fld fs3, 0x54(a1)\n"
    "  fld fs4, 0x5c(a1)\n"
    "  fld fs5, 0x64(a1)\n"
    "  fld fs6, 0x6c(a1)\n"
    "  fld fs7, 0x74(a1)\n"
    "  fld fs8, 0x7c(a1)\n"
    "  fld fs9, 0x84(a1)\n"
    "  fld fs10, 0x8c(a1)\n"
    "  fld fs11, 0x94(a1)\n"
#elif __riscv_flen == 32
    "  fsw fs0, 0x3c(a0)\n"
    "  fsw fs1, 0x40(a0)\n"
    "  fsw fs2, 0x44(a0)\n"
    "  fsw fs3, 0x48(a0)\n"
    "  fsw fs4, 0x4c(a0)\n"
    "  fsw fs5, 0x50(a0)\n"
    "  fsw fs6, 0x54(a0)\n"
    "  fsw fs7, 0x58(a0)\n"
    "  fsw fs8, 0x5c(a0)\n"
    "  fsw fs9, 0x60(a0)\n"
    "  fsw fs10, 0x64(a0)\n"
    "  fsw fs11, 0x68(a0)\n"
    "  flw fs0, 0x3c(a1)\n"
    "  flw fs1, 0x40(a1)\n"
    "  flw fs2, 0x44(a1)\n"
    "  flw fs3, 0x48(a1)\n"
    "  flw fs4, 0x4c(a1)\n"
    "  flw fs5, 0x50(a1)\n"
    "  flw fs6, 0x54(a1)\n"
    "  flw fs7, 0x58(a1)\n"
    "  flw fs8, 0x5c(a1)\n"
    "  flw fs9, 0x60(a1)\n"
    "  flw fs10, 0x64(a1)\n"
    "  flw fs11, 0x68(a1)\n"
#else
#error "Unsupported RISC-V FLEN"
#endif
#endif /* __riscv_flen */
    "  lw s0, 0x00(a1)\n"
    "  lw s1, 0x04(a1)\n"
    "  lw s2, 0x08(a1)\n"
    "  lw s3, 0x0c(a1)\n"
    "  lw s4, 0x10(a1)\n"
    "  lw s5, 0x14(a1)\n"
    "  lw s6, 0x18(a1)\n"
    "  lw s7, 0x1c(a1)\n"
    "  lw s8, 0x20(a1)\n"
    "  lw s9, 0x24(a1)\n"
    "  lw s10, 0x28(a1)\n"
    "  lw s11, 0x2c(a1)\n"
    "  lw ra, 0x30(a1)\n"
    "  lw a2, 0x34(a1)\n" /* pc */
    "  lw sp, 0x38(a1)\n"
    "  jr a2\n"
#else
#error "Unsupported RISC-V XLEN"
#endif /* __riscv_xlen */
  ".size _llco_asm_switch, .-_llco_asm_switch\n"
);

static void llco_asmctx_make(struct llco_asmctx *ctx, 
    void* stack_base, size_t stack_size, void *arg)
{
    ctx->s[0] = (void*)(arg);
    ctx->s[1] = (void*)(llco_entry);
    ctx->pc = (void*)(_llco_asm_entry);
#if __riscv_xlen == 64
    ctx->ra = (void*)(0xdeaddeaddeaddead);
#elif __riscv_xlen == 32
    ctx->ra = (void*)(0xdeaddead);
#endif
    ctx->sp = (void*)((size_t)stack_base + stack_size);
}

#endif // riscv

////////////////////////////////////////////////////////////////////////////////
// x86
////////////////////////////////////////////////////////////////////////////////
#if (defined(__i386) || defined(__i386__)) && !defined(LLCO_NOASM)
#define LLCO_ASM
#define LLCO_READY
#define LLCO_METHOD "asm,i386"

struct llco_asmctx {
    void *eip, *esp, *ebp, *ebx, *esi, *edi;
};

void _llco_asm_switch(struct llco_asmctx *from, struct llco_asmctx *to);

__asm__(
#ifdef __DJGPP__ /* DOS compiler */
    "__llco_asm_switch:\n"
#else
    ".text\n"
    ".globl _llco_asm_switch\n"
    ".type _llco_asm_switch @function\n"
    ".hidden _llco_asm_switch\n"
    "_llco_asm_switch:\n"
#endif
    "  call 1f\n"
    "  1:\n"
    "  popl %ecx\n"
    "  addl $(2f-1b), %ecx\n"
    "  movl 4(%esp), %eax\n"
    "  movl 8(%esp), %edx\n"
    "  movl %ecx, (%eax)\n"
    "  movl %esp, 4(%eax)\n"
    "  movl %ebp, 8(%eax)\n"
    "  movl %ebx, 12(%eax)\n"
    "  movl %esi, 16(%eax)\n"
    "  movl %edi, 20(%eax)\n"
    "  movl 20(%edx), %edi\n"
    "  movl 16(%edx), %esi\n"
    "  movl 12(%edx), %ebx\n"
    "  movl 8(%edx), %ebp\n"
    "  movl 4(%edx), %esp\n"
    "  jmp *(%edx)\n"
    "  2:\n"
    "  ret\n"
#ifndef __DJGPP__
    ".size _llco_asm_switch, .-_llco_asm_switch\n"
#endif
);

static void llco_asmctx_make(struct llco_asmctx *ctx,
    void* stack_base, size_t stack_size, void *arg)
{
    void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - 16 - 
        1*sizeof(size_t));
    stack_high_ptr[0] = (void*)(0xdeaddead);  // Dummy return address.
    stack_high_ptr[1] = (void*)(arg);
    ctx->eip = (void*)(llco_entry);
    ctx->esp = (void*)(stack_high_ptr);
}
#endif // __i386__

////////////////////////////////////////////////////////////////////////////////
// x64
////////////////////////////////////////////////////////////////////////////////
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(LLCO_NOASM)
#define LLCO_ASM
#define LLCO_READY
#define LLCO_METHOD "asm,x64"

#ifdef _WIN32

struct llco_asmctx {
    void *rip, *rsp, *rbp, *rbx, *r12, *r13, *r14, *r15, *rdi, *rsi;
    void* xmm[20]; /* xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, 
        xmm14, xmm15 */
    void* fiber_storage;
    void* dealloc_stack;
    void* stack_limit;
    void* stack_base;
};

#if defined(__GNUC__)
#define LLCO_ASM_BLOB __attribute__((section(".text")))
#elif defined(_MSC_VER)
#define LLCO_ASM_BLOB __declspec(allocate(".text"))
#pragma section(".text")
#endif

LLCO_ASM_BLOB static unsigned char llco_wrap_main_code_entry[] = {
    0x4c,0x89,0xe9,                               // mov    %r13,%rcx
    0x41,0xff,0xe4,                               // jmpq   *%r12
    0xc3,                                         // retq
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90  // nop
};

LLCO_ASM_BLOB static unsigned char llco_asm_switch_code[] = {
    0x48,0x8d,0x05,0x3e,0x01,0x00,0x00,           // lea    0x13e(%rip),%rax
    0x48,0x89,0x01,                               // mov    %rax,(%rcx)
    0x48,0x89,0x61,0x08,                          // mov    %rsp,0x8(%rcx)
    0x48,0x89,0x69,0x10,                          // mov    %rbp,0x10(%rcx)
    0x48,0x89,0x59,0x18,                          // mov    %rbx,0x18(%rcx)
    0x4c,0x89,0x61,0x20,                          // mov    %r12,0x20(%rcx)
    0x4c,0x89,0x69,0x28,                          // mov    %r13,0x28(%rcx)
    0x4c,0x89,0x71,0x30,                          // mov    %r14,0x30(%rcx)
    0x4c,0x89,0x79,0x38,                          // mov    %r15,0x38(%rcx)
    0x48,0x89,0x79,0x40,                          // mov    %rdi,0x40(%rcx)
    0x48,0x89,0x71,0x48,                          // mov    %rsi,0x48(%rcx)
    0x0f,0x11,0x71,0x50,                          // movups %xmm6,0x50(%rcx)
    0x0f,0x11,0x79,0x60,                          // movups %xmm7,0x60(%rcx)
    0x44,0x0f,0x11,0x41,0x70,                     // movups %xmm8,0x70(%rcx)
    0x44,0x0f,0x11,0x89,0x80,0x00,0x00,0x00,      // movups %xmm9,0x80(%rcx)
    0x44,0x0f,0x11,0x91,0x90,0x00,0x00,0x00,      // movups %xmm10,0x90(%rcx)
    0x44,0x0f,0x11,0x99,0xa0,0x00,0x00,0x00,      // movups %xmm11,0xa0(%rcx)
    0x44,0x0f,0x11,0xa1,0xb0,0x00,0x00,0x00,      // movups %xmm12,0xb0(%rcx)
    0x44,0x0f,0x11,0xa9,0xc0,0x00,0x00,0x00,      // movups %xmm13,0xc0(%rcx)
    0x44,0x0f,0x11,0xb1,0xd0,0x00,0x00,0x00,      // movups %xmm14,0xd0(%rcx)
    0x44,0x0f,0x11,0xb9,0xe0,0x00,0x00,0x00,      // movups %xmm15,0xe0(%rcx)
    0x65,0x4c,0x8b,0x14,0x25,0x30,0x00,0x00,0x00, // mov    %gs:0x30,%r10
    0x49,0x8b,0x42,0x20,                          // mov    0x20(%r10),%rax
    0x48,0x89,0x81,0xf0,0x00,0x00,0x00,           // mov    %rax,0xf0(%rcx)
    0x49,0x8b,0x82,0x78,0x14,0x00,0x00,           // mov    0x1478(%r10),%rax
    0x48,0x89,0x81,0xf8,0x00,0x00,0x00,           // mov    %rax,0xf8(%rcx)
    0x49,0x8b,0x42,0x10,                          // mov    0x10(%r10),%rax
    0x48,0x89,0x81,0x00,0x01,0x00,0x00,           // mov    %rax,0x100(%rcx)
    0x49,0x8b,0x42,0x08,                          // mov    0x8(%r10),%rax
    0x48,0x89,0x81,0x08,0x01,0x00,0x00,           // mov    %rax,0x108(%rcx)
    0x48,0x8b,0x82,0x08,0x01,0x00,0x00,           // mov    0x108(%rdx),%rax
    0x49,0x89,0x42,0x08,                          // mov    %rax,0x8(%r10)
    0x48,0x8b,0x82,0x00,0x01, 0x00, 0x00,         // mov    0x100(%rdx),%rax
    0x49,0x89,0x42,0x10,                          // mov    %rax,0x10(%r10)
    0x48,0x8b,0x82,0xf8,0x00, 0x00, 0x00,         // mov    0xf8(%rdx),%rax
    0x49,0x89,0x82,0x78,0x14, 0x00, 0x00,         // mov    %rax,0x1478(%r10)
    0x48,0x8b,0x82,0xf0,0x00, 0x00, 0x00,         // mov    0xf0(%rdx),%rax
    0x49,0x89,0x42,0x20,                          // mov    %rax,0x20(%r10)
    0x44,0x0f,0x10,0xba,0xe0,0x00,0x00,0x00,      // movups 0xe0(%rdx),%xmm15
    0x44,0x0f,0x10,0xb2,0xd0,0x00,0x00,0x00,      // movups 0xd0(%rdx),%xmm14
    0x44,0x0f,0x10,0xaa,0xc0,0x00,0x00,0x00,      // movups 0xc0(%rdx),%xmm13
    0x44,0x0f,0x10,0xa2,0xb0,0x00,0x00,0x00,      // movups 0xb0(%rdx),%xmm12
    0x44,0x0f,0x10,0x9a,0xa0,0x00,0x00,0x00,      // movups 0xa0(%rdx),%xmm11
    0x44,0x0f,0x10,0x92,0x90,0x00,0x00,0x00,      // movups 0x90(%rdx),%xmm10
    0x44,0x0f,0x10,0x8a,0x80,0x00,0x00,0x00,      // movups 0x80(%rdx),%xmm9
    0x44,0x0f,0x10,0x42,0x70,                     // movups 0x70(%rdx),%xmm8
    0x0f,0x10,0x7a,0x60,                          // movups 0x60(%rdx),%xmm7
    0x0f,0x10,0x72,0x50,                          // movups 0x50(%rdx),%xmm6
    0x48,0x8b,0x72,0x48,                          // mov    0x48(%rdx),%rsi
    0x48,0x8b,0x7a,0x40,                          // mov    0x40(%rdx),%rdi
    0x4c,0x8b,0x7a,0x38,                          // mov    0x38(%rdx),%r15
    0x4c,0x8b,0x72,0x30,                          // mov    0x30(%rdx),%r14
    0x4c,0x8b,0x6a,0x28,                          // mov    0x28(%rdx),%r13
    0x4c,0x8b,0x62,0x20,                          // mov    0x20(%rdx),%r12
    0x48,0x8b,0x5a,0x18,                          // mov    0x18(%rdx),%rbx
    0x48,0x8b,0x6a,0x10,                          // mov    0x10(%rdx),%rbp
    0x48,0x8b,0x62,0x08,                          // mov    0x8(%rdx),%rsp
    0xff,0x22,                                    // jmpq   *(%rdx)
    0xc3,                                         // retq
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,      // nop
    0x90,0x90,                                    // nop
};

void (*_llco_asm_entry)(void) = 
    (void(*)(void))(void*)llco_wrap_main_code_entry;
void (*_llco_asm_switch)(struct llco_asmctx *from, 
    struct llco_asmctx *to) = (void(*)(struct llco_asmctx *from, 
    struct llco_asmctx *to))(void*)llco_asm_switch_code;

static void llco_asmctx_make(struct llco_asmctx *ctx, 
    void* stack_base, size_t stack_size, void *arg)
{
    stack_size = stack_size - 32; // Reserve 32 bytes for the shadow space.
    void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - 
        sizeof(size_t));
    stack_high_ptr[0] = (void*)(0xdeaddeaddeaddead);  // Dummy return address.
    ctx->rip = (void*)(_llco_asm_entry);
    ctx->rsp = (void*)(stack_high_ptr);
    ctx->r12 = (void*)(llco_entry);
    ctx->r13 = (void*)(arg);
    void* stack_top = (void*)((size_t)stack_base + stack_size);
    ctx->stack_base = stack_top;
    ctx->stack_limit = stack_base;
    ctx->dealloc_stack = stack_base;
}

#else

struct llco_asmctx {
    void *rip, *rsp, *rbp, *rbx, *r12, *r13, *r14, *r15;
};

void _llco_asm_entry(void);
int _llco_asm_switch(struct llco_asmctx *from, struct llco_asmctx *to);

__asm__(
    ".text\n"
#ifdef __MACH__ /* Mac OS X assembler */
    ".globl __llco_asm_entry\n"
    "__llco_asm_entry:\n"
#else /* Linux assembler */
    ".globl _llco_asm_entry\n"
    ".type _llco_asm_entry @function\n"
    ".hidden _llco_asm_entry\n"
    "_llco_asm_entry:\n"
#endif
    "  movq %r13, %rdi\n"
    "  jmpq *%r12\n"
#ifndef __MACH__
    ".size _llco_asm_entry, .-_llco_asm_entry\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __MACH__ /* Mac OS assembler */
    ".globl __llco_asm_switch\n"
    "__llco_asm_switch:\n"
#else /* Linux assembler */
    ".globl _llco_asm_switch\n"
    ".type _llco_asm_switch @function\n"
    ".hidden _llco_asm_switch\n"
    "_llco_asm_switch:\n"
#endif
    "  leaq 0x3d(%rip), %rax\n"
    "  movq %rax, (%rdi)\n"
    "  movq %rsp, 8(%rdi)\n"
    "  movq %rbp, 16(%rdi)\n"
    "  movq %rbx, 24(%rdi)\n"
    "  movq %r12, 32(%rdi)\n"
    "  movq %r13, 40(%rdi)\n"
    "  movq %r14, 48(%rdi)\n"
    "  movq %r15, 56(%rdi)\n"
    "  movq 56(%rsi), %r15\n"
    "  movq 48(%rsi), %r14\n"
    "  movq 40(%rsi), %r13\n"
    "  movq 32(%rsi), %r12\n"
    "  movq 24(%rsi), %rbx\n"
    "  movq 16(%rsi), %rbp\n"
    "  movq 8(%rsi), %rsp\n"
    "  jmpq *(%rsi)\n"
    "  ret\n"
#ifndef __MACH__
    ".size _llco_asm_switch, .-_llco_asm_switch\n"
#endif
);

static void llco_asmctx_make(struct llco_asmctx *ctx, 
    void* stack_base, size_t stack_size, void *arg)
{
    // Reserve 128 bytes for the Red Zone space (System V AMD64 ABI).
    stack_size = stack_size - 128; 
    void** stack_high_ptr = (void**)((size_t)stack_base + stack_size - 
        sizeof(size_t));
    stack_high_ptr[0] = (void*)(0xdeaddeaddeaddead);  // Dummy return address.
    ctx->rip = (void*)(_llco_asm_entry);
    ctx->rsp = (void*)(stack_high_ptr);
    ctx->r12 = (void*)(llco_entry);
    ctx->r13 = (void*)(arg);
}

#endif
#endif // x64

// --- END ASM Code --- //

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

////////////////////////////////////////////////////////////////////////////////
// ASM with stackjmp activated
////////////////////////////////////////////////////////////////////////////////
#if defined(LLCO_READY) && defined(LLCO_STACKJMP)
LLCO_NOINLINE LLCO_NORETURN
static void llco_stackjmp(void *stack, size_t stack_size, 
    void(*entry)(void *arg))
{
    struct llco_asmctx ctx = { 0 };
    llco_asmctx_make(&ctx, stack, stack_size, 0);
    struct llco_asmctx ctx0 = { 0 };
    _llco_asm_switch(&ctx0, &ctx);
    llco_exit();
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Windows Fibers
////////////////////////////////////////////////////////////////////////////////
#if defined(_WIN32) && !defined(LLCO_READY)
#define LLCO_WINDOWS
#define LLCO_READY
#define LLCO_METHOD "fibers,windows"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#error Windows fibers unsupported

#endif 

////////////////////////////////////////////////////////////////////////////////
// Webassembly Fibers
////////////////////////////////////////////////////////////////////////////////
#if defined(__EMSCRIPTEN__) && !defined(LLCO_READY)
#define LLCO_WASM
#define LLCO_READY
#define LLCO_METHOD "fibers,emscripten"

#include <emscripten/fiber.h>
#include <string.h>

#ifndef LLCO_ASYNCIFY_STACK_SIZE
#define LLCO_ASYNCIFY_STACK_SIZE 4096
#endif

static __thread char llco_main_stack[LLCO_ASYNCIFY_STACK_SIZE];

#endif

////////////////////////////////////////////////////////////////////////////////
// Ucontext
////////////////////////////////////////////////////////////////////////////////
#if !defined(LLCO_READY)
#define LLCO_UCONTEXT
#define LLCO_READY
#define LLCO_METHOD "ucontext"
#ifndef LLCO_STACKJMP
#define LLCO_STACKJMP
#endif

#if defined(__FreeBSD__) || defined(__APPLE__)
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>

static __thread ucontext_t stackjmp_ucallee;
static __thread int stackjmp_ucallee_gotten = 0;

#if defined(__APPLE__) && defined(__aarch64__) && !defined(LLCO_NOSTACKADJUST)
// Here we ensure that the initial context switch will *not* page the 
// entire stack into process memory before executing the entry point
// function. Which is a behavior that can be observed on Mac OS with
// Apple Silicon. This "trick" can be optionally removed at the expense
// of slower initial jumping into large stacks.
enum llco_stack_grows { DOWNWARDS, UPWARDS }; 

static enum llco_stack_grows llco_stack_grows0(int *addr0) { 
    int addr1; 
    return addr0 < &addr1 ? UPWARDS : DOWNWARDS;
} 

static enum llco_stack_grows llco_stack_grows(void) {
    int addr0;
    return llco_stack_grows0(&addr0);
}

static void llco_adjust_ucontext_stack(ucontext_t *ucp) {
    if (llco_stack_grows() == UPWARDS) {
        ucp->uc_stack.ss_sp = (char*)ucp->uc_stack.ss_sp+ucp->uc_stack.ss_size;
        ucp->uc_stack.ss_size = 0;
    }
}
#else 
#define llco_adjust_ucontext_stack(ucp)
#endif

// Ucontext always uses stackjmp with setjmp/longjmp, instead of swapcontext
// becuase it's much faster.
LLCO_NOINLINE LLCO_NORETURN
static void llco_stackjmp(void *stack, size_t stack_size, 
    void(*entry)(void *arg))
{
    if (!stackjmp_ucallee_gotten) {
        stackjmp_ucallee_gotten = 1;
        getcontext(&stackjmp_ucallee);
    }
    stackjmp_ucallee.uc_stack.ss_sp = stack;
    stackjmp_ucallee.uc_stack.ss_size = stack_size;
    llco_adjust_ucontext_stack(&stackjmp_ucallee);
    makecontext(&stackjmp_ucallee, (void*)entry, 0);
    setcontext(&stackjmp_ucallee);
    llco_exit();
}

#endif // Ucontext

#if defined(LLCO_STACKJMP)
#include <setjmp.h>
#ifdef _WIN32
// For reasons outside of my understanding, Windows does not allow for jumping
// between stacks using the setjmp/longjmp mechanism.
#error Windows stackjmp not supported
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
// llco switching code
////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>

struct llco {
    struct llco_desc desc;
#if defined(LLCO_STACKJMP)
    jmp_buf buf;
#elif defined(LLCO_ASM)
    struct llco_asmctx ctx;
#elif defined(LLCO_WASM)
    emscripten_fiber_t fiber;
#elif defined(LLCO_WINDOWS)
    LPVOID fiber;
#endif
#ifdef LLCO_VALGRIND
    int valgrind_stack_id;
#endif
#if defined(__GNUC__)
    void *uw_stop_ip; // record of the last unwind ip.
#endif
};

#ifdef LLCO_VALGRIND
static __thread unsigned int llco_valgrind_stack_id = 0;
static __thread unsigned int llco_cleanup_valgrind_stack_id = 0;
#endif

static __thread struct llco llco_thread = { 0 };
static __thread struct llco *llco_cur = NULL;
static __thread struct llco_desc llco_desc;
static __thread volatile bool llco_cleanup_needed = false;
static __thread volatile struct llco_desc llco_cleanup_desc;
static __thread volatile bool llco_cleanup_active = false;

#define llco_cleanup_guard() { \
    if (llco_cleanup_active) { \
        fprintf(stderr, "%s not available during cleanup\n", __func__); \
        abort(); \
    } \
}

static void llco_cleanup_last(void) {
    if (llco_cleanup_needed) {
        if (llco_cleanup_desc.cleanup) {
            llco_cleanup_active = true;
#ifdef LLCO_VALGRIND
            VALGRIND_STACK_DEREGISTER(llco_cleanup_valgrind_stack_id);
#endif
            llco_cleanup_desc.cleanup(llco_cleanup_desc.stack, 
                llco_cleanup_desc.stack_size, llco_cleanup_desc.udata);
            llco_cleanup_active = false;
        }
        llco_cleanup_needed = false;
    }
}

LLCO_NOINLINE
static void llco_entry_wrap(void *arg) {
    llco_cleanup_last();
#if defined(LLCO_WASM)
    llco_cur = arg;
    llco_cur->desc = llco_desc;
#else
    (void)arg;
    struct llco self = { .desc = llco_desc };
    llco_cur = &self;
#endif
#ifdef LLCO_VALGRIND
    llco_cur->valgrind_stack_id = llco_valgrind_stack_id;
#endif
#if defined(__GNUC__) && !defined(__EMSCRIPTEN__)
    llco_cur->uw_stop_ip = __builtin_return_address(0);
#endif
    llco_cur->desc.entry(llco_cur->desc.udata);
}


LLCO_NOINLINE LLCO_NORETURN
static void llco_entry(void *arg) {
    llco_entry_wrap(arg);
    llco_exit();
}

LLCO_NOINLINE
static void llco_switch1(struct llco *from, struct llco *to, 
    void *stack, size_t stack_size)
{
#ifdef LLCO_VALGRIND
    llco_valgrind_stack_id = VALGRIND_STACK_REGISTER(stack, stack + stack_size);
#endif
#if defined(LLCO_STACKJMP)
    if (to) {
        if (!_setjmp(from->buf)) {
            _longjmp(to->buf, 1);
        }
    } else {
        if (!_setjmp(from->buf)) {
            llco_stackjmp(stack, stack_size, llco_entry);
        }
    }
#elif defined(LLCO_ASM)
    if (to) {
        _llco_asm_switch(&from->ctx, &to->ctx);
    } else {
        struct llco_asmctx ctx = { 0 };
        llco_asmctx_make(&ctx, stack, stack_size, 0);
        _llco_asm_switch(&from->ctx, &ctx);
    }
#elif defined(LLCO_WASM)
    if (to) {
        emscripten_fiber_swap(&from->fiber, &to->fiber);
    } else {
        if (from == &llco_thread) {
            emscripten_fiber_init_from_current_context(&from->fiber, 
                llco_main_stack, LLCO_ASYNCIFY_STACK_SIZE);
        }
        stack_size -= LLCO_ASYNCIFY_STACK_SIZE;
        char *astack = ((char*)stack) + stack_size;
        size_t astack_size = LLCO_ASYNCIFY_STACK_SIZE - sizeof(struct llco);
        struct llco *self = (void*)(astack + astack_size);
        memset(self, 0, sizeof(struct llco));
        emscripten_fiber_init(&self->fiber, llco_entry, 
           self, stack, stack_size, astack, astack_size);
        emscripten_fiber_swap(&from->fiber, &self->fiber);
    }
#elif defined(LLCO_WINDOWS)
    // Unsupported
#endif
}

static void llco_switch0(struct llco_desc *desc, struct llco *co, 
    bool final)
{
    struct llco *from = llco_cur ? llco_cur : &llco_thread;
    struct llco *to = desc ? NULL : co ? co : &llco_thread;
    if (from != to) {
        if (final) {
            llco_cleanup_needed = true;
            llco_cleanup_desc = from->desc;
#ifdef LLCO_VALGRIND
            llco_cleanup_valgrind_stack_id = from->valgrind_stack_id;
#endif
        }
        if (desc) {
            llco_desc = *desc;
            llco_switch1(from, 0, desc->stack, desc->stack_size);
        } else {
            llco_cur = to;
            llco_switch1(from, to, 0, 0);
        }
        llco_cleanup_last();
    }
}

////////////////////////////////////////////////////////////////////////////////
// Exported methods
////////////////////////////////////////////////////////////////////////////////

// Start a new coroutine.
LLCO_EXTERN
void llco_start(struct llco_desc *desc, bool final) {
    if (!desc || desc->stack_size < LLCO_MINSTACKSIZE) {
        fprintf(stderr, "stack too small\n");
        abort();
    }
    llco_cleanup_guard();
    llco_switch0(desc, 0, final);
}

// Switch to another coroutine.
LLCO_EXTERN
void llco_switch(struct llco *co, bool final) {
    llco_cleanup_guard();
    llco_switch0(0, co, final);
}

// Return the current coroutine or NULL if not currently running in a
// coroutine.
LLCO_EXTERN
struct llco *llco_current(void) {
    llco_cleanup_guard();
    return llco_cur == &llco_thread ? 0 : llco_cur;
}

// Returns a string that indicates which coroutine method is being used by
// the program. Such as "asm" or "ucontext", etc.
LLCO_EXTERN
const char *llco_method(void *caps) {
    (void)caps;
    return LLCO_METHOD
#ifdef LLCO_STACKJMP
        ",stackjmp"
#endif
    ;
}

#if defined(__GNUC__) && !defined(__EMSCRIPTEN__) && !defined(_WIN32)

#include <unwind.h>
#include <string.h>
#include <dlfcn.h>

struct llco_dlinfo {
    const char      *dli_fname;     /* Pathname of shared object */
    void            *dli_fbase;     /* Base address of shared object */
    const char      *dli_sname;     /* Name of nearest symbol */
    void            *dli_saddr;     /* Address of nearest symbol */
};

#ifdef __linux__
int dladdr(const void *, void *);
#endif

static void llco_getsymbol(struct _Unwind_Context *uwc, 
    struct llco_symbol *sym)
{
    memset(sym, 0, sizeof(struct llco_symbol));
    sym->cfa = (void*)_Unwind_GetCFA(uwc);
    int ip_before; /* unused */
    sym->ip = (void*)_Unwind_GetIPInfo(uwc, &ip_before);
    struct llco_dlinfo dlinfo = { 0 };
    if (sym->ip && dladdr(sym->ip, (void*)&dlinfo)) {
        sym->fname = dlinfo.dli_fname;
        sym->fbase = dlinfo.dli_fbase;
        sym->sname = dlinfo.dli_sname;
        sym->saddr = dlinfo.dli_saddr;
    }
}

struct llco_unwind_context {
    void *udata;
    void *start_ip;
    bool started;
    int nsymbols;
    int nsymbols_actual;
    struct llco_symbol last;
    bool (*func)(struct llco_symbol *, void *);
    void *unwind_addr;
};

static _Unwind_Reason_Code llco_func(struct _Unwind_Context *uwc, void *ptr) {
    struct llco_unwind_context *ctx = ptr;
    
    struct llco *cur = llco_current();
    if (cur && !cur->uw_stop_ip) {
        return _URC_END_OF_STACK;
    }
    struct llco_symbol sym;
    llco_getsymbol(uwc, &sym);
    if (ctx->start_ip && !ctx->started && sym.ip != ctx->start_ip) {
        return _URC_NO_REASON;
    }
    ctx->started = true;
    if (!sym.ip || (cur && sym.ip == cur->uw_stop_ip)) {
        return _URC_END_OF_STACK;
    }
    ctx->nsymbols++;
    if (!cur) {
        ctx->nsymbols_actual++;
        if (ctx->func && !ctx->func(&sym, ctx->udata)) {
            return _URC_END_OF_STACK;
        }
    } else {
        if (ctx->nsymbols > 1) {
            ctx->nsymbols_actual++;
            if (ctx->func && !ctx->func(&ctx->last, ctx->udata)) {
                return _URC_END_OF_STACK;
            }
        }
        ctx->last = sym;
    }
    return _URC_NO_REASON;
}

LLCO_EXTERN
int llco_unwind(bool(*func)(struct llco_symbol *sym, void *udata), void *udata){
    struct llco_unwind_context ctx = { 
#if defined(__GNUC__) && !defined(__EMSCRIPTEN__)
        .start_ip = __builtin_return_address(0),
#endif
        .func = func, 
        .udata = udata 
    };
    _Unwind_Backtrace(llco_func, &ctx);
    return ctx.nsymbols_actual;
}

#else

LLCO_EXTERN
int llco_unwind(bool(*func)(struct llco_symbol *sym, void *udata), void *udata){
    (void)func; (void)udata;
    /* Unsupported */
    return 0;
}

#endif
