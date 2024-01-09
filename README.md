# llco

A low-level coroutine library for C.

The main purpose of this project is to power
[sco](https://github.com/tidwall/sco) and
[neco](https://github.com/tidwall/neco), which are more general purpose
coroutine libraries.

## Features

- Cross-platform. Linux, Mac, Webassembly, Android, iOS, Windows, RaspPi, RISC-V.
- No allocations. You'll need to bring your own stack.
- Fast context switching. Uses assembly in most cases.

## API

```C
void llco_start(struct llco_desc *desc, bool final);
void llco_switch(struct llco *co, bool final);
struct llco *llco_current(void);
```

## Example

```C

void cleanup(void *stack, size_t stack_size, void *udata) {
    // Free the coroutine stack
    free(stack);
}

void entry(void *udata) {
    printf("Coroutine started\n");
    // Switch back to the main thread and cleanup this coroutine
    llco_switch(0, true);
}

int main(void) {
    // Start a coroutine using an newly allocated stack
    struct llco_desc desc = {
        .stack = malloc(LLCO_MINSTACKSIZE),
        .stack_size = LLCO_MINSTACKSIZE,
        .entry = entry,
        .cleanup = cleanup,
    };
    llco_start(&desc, false);
    printf("Back to main\n");
}

```

## Notes

- Windows: Only x86_64 is supported at this time. The Windows Fibers API is not 
  being used as a fallback due to it's need to allocate memory dynamically for
  the `CreateFiber` call.
- Webassembly: Must be compiled with Emscripten using the `-sASYNCIFY` flag.
- All other platforms may fallback to using ucontext when the assembly method
  is not available. The `uco_method(0)` function can be used to see if assembly
  or ucontext is being used.
- The ucontext fallback method only uses the ucontext API when starting a
  coroutine. Once the coroutine has been started, `setjmp` and `longjmp` take
  over the switching duties.

## Options

- `-DLLCO_NOASM`: Disable assembly
- `-DLLCO_STACKJMP`: Always use `setjmp` and `longjmp` for jumping between stacks.

Much of the assembly code was adapted from the [https://github.com/edubart/minicoro](minicoro)
project by [Eduardo Bart](https://github.com/edubart), which was originally
adapted from the [Lua Coco](https://coco.luajit.org) project by
[Mike Pall](https://github.com/MikePall).

## License

Public Domain or MIT No Attribution, your choice.
