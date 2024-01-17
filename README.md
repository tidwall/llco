# llco

A low-level coroutine library for C.

The main purpose of this project is to power
[sco](https://github.com/tidwall/sco) and
[neco](https://github.com/tidwall/neco), which are more general purpose
coroutine libraries.

## Features

- Stackful coroutines. 
- No allocations (bring your own stack)
- Cross-platform. Linux, Mac, Webassembly, iOS, Android, FreeBSD, Windows, RaspPi, RISC-V
- Fast context switching. Uses assembly in most cases
- No built-in scheduler. You are in charge of the coroutine priority
- Single file amalgamation. No dependencies.

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
    // Start a coroutine from the main function using an newly allocated stack.
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

## API

```C
// Switch to another coroutine. Set the `co` param to NULL to switch to the 
// main function. Use the final param to tell the program that you are done
// with the current coroutine, at which point it's respective `cleanup` 
// callback will be called.
void llco_switch(struct llco *co, bool final);

// Start a new coroutine. This can be called from the main function or a 
// nested coroutine.
void llco_start(struct llco_desc *desc, bool final);


// Return the current coroutine or NULL if not currently running in a
// coroutine.
struct llco *llco_current(void);

// Returns a string that indicates which coroutine method is being used by
// the program. Such as "asm" or "ucontext", etc.
const char *llco_method(void *caps);
```

## Caveats

- Windows: Only x86_64 is supported at this time. The Windows Fibers API is not 
  currently suitable as a fallback do to the `CreateFiber` call needing to
  allocate memory dynamically.
- Webassembly: Must be compiled with Emscripten using the `-sASYNCIFY` flag.
- All other platforms may fallback to using ucontext when the assembly method
  is not available. The `uco_method(0)` function can be used to see if assembly
  or ucontext is being used.
- The ucontext fallback method only uses the ucontext API when starting a
  coroutine. Once the coroutine has been started, `setjmp` and `longjmp` take
  over the switching duties.

## Compiler Options

- `-DLLCO_NOASM`: Disable assembly. Use ucontext fallback instead.
- `-DLLCO_STACKJMP`: Use `setjmp` and `longjmp` for jumping between stacks, 
   even with the assembly method.

## Acknowledgements

Much of the assembly code was adapted from the [https://github.com/edubart/minicoro](minicoro)
project by [Eduardo Bart](https://github.com/edubart), which was originally
adapted from the [Lua Coco](https://coco.luajit.org) project by
[Mike Pall](https://github.com/MikePall).

## License

Public Domain or MIT No Attribution, your choice.
