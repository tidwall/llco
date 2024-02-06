#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "llco.h"



#include <unwind.h>



typedef struct dl_info {
    const char      *dli_fname;     /* Pathname of shared object */
    void            *dli_fbase;     /* Base address of shared object */
    const char      *dli_sname;     /* Name of nearest symbol */
    void            *dli_saddr;     /* Address of nearest symbol */
} Dl_info;
int dladdr(const void *, Dl_info *);


struct unwind_info {
    void *cfa;
    void *ip;
    int ip_before;
    void *data_rel_base;
    void *text_rel_base;
    const char *fname;     /* Pathname of shared object */
    void *fbase;           /* Base address of shared object */
    const char *sname;     /* Name of nearest symbol */
    void *saddr;           /* Address of nearest symbol */
};

static void unwind_getinfo(struct _Unwind_Context *uwc, 
    struct unwind_info *info)
{
    memset(info, 0, sizeof(struct unwind_info));
    info->cfa = (void*)_Unwind_GetCFA(uwc);
    info->ip = (void*)_Unwind_GetIPInfo(uwc, &info->ip_before);
#ifdef __linux__
    info->data_rel_base = (void*)_Unwind_GetDataRelBase(uwc);
    info->text_rel_base = (void*)_Unwind_GetTextRelBase(uwc);
#endif
    Dl_info dl_info = { 0 };
    if (info->ip && dladdr(info->ip, &dl_info)) {
        info->fname = dl_info.dli_fname;
        info->fbase = dl_info.dli_fbase;
        info->sname = dl_info.dli_sname;
        info->saddr = dl_info.dli_saddr;
    }
}

static _Unwind_Reason_Code btfnc(struct _Unwind_Context *uwc, void *ptr) {
    printf("== UNWIND ==\n");
    struct unwind_info info;
    unwind_getinfo(uwc, &info);

    printf("  cfa:           %p\n", info.cfa);
    printf("  ip:            %p\n", info.ip);
    printf("  ip_before:     %d\n", info.ip_before);
    printf("  data_rel_base: %p\n", info.data_rel_base);
    printf("  text_rel_base: %p\n", info.text_rel_base);
    printf("  fname:         %s\n", info.fname);
    printf("  fbase:         %p\n", info.fbase);
    printf("  sname:         %s\n", info.sname);
    printf("  saddr:         %p\n", info.saddr);

    return _URC_NO_REASON;
    return _URC_END_OF_STACK;
}







#define STKSZ 32768

void cleanup(void *stk, size_t stksz, void *udata) {
    printf("== CLEANUP ==\n");
    free(stk);
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
    printf("  frame_address: %p\n", __builtin_frame_address(0));
    printf("  return_address: %p\n", __builtin_return_address(0));
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

