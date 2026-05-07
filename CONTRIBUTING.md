# Contributing to EasyMem

First of all, thank you for being here! 

I've been on the other side: you spend your weekend optimizing someone else's messy codebase, making it 10x faster, only to be hit with a 10-page contribution guide full of bureaucratic rules, strict commit formats, and formatting nitpicks. You close the tab and walk away. 

**I don't do that here.** 

At EasyMem, code quality and performance speak louder than bureaucracy. If you have an idea, a bug fix, or a massive optimization, I want to see your code. We can figure out the formatting details later.

## The Golden Rules

To keep EasyMem true to its philosophy, there are only a few architectural rules I strictly follow:

1. **Header-Only (STB-style):** The core libraries must remain single-file headers. No `.c` files, no complex build systems required for the end-user.
2. **Zero-Overhead Philosophy:** Performance is critical. Avoid adding internal locks, mutexes, or hidden allocations. 
3. **Bare-Metal Friendly:** I do not assume the existence of a `libc` heap unless explicitly allowed. The code must perfectly respect the `EM_NO_MALLOC` macro.
4. **16-bit, 32-bit, and 64-bit Support:** The code must compile and run correctly across all pointer sizes. Be careful with bitwise operations, shifts, and alignment mathematics. The CI will test this, but keep it in mind.

## The Workflow

If you want to add a feature or fix a bug, here is the ideal pipeline:

1. **Write the code.**
2. **Add a test:** If you introduced a new feature or fixed an edge case, create a new test file in the `tests/` directory (name it `yourfeature_test.c`). The `Makefile` will automatically pick it up and generate the build targets for it—no need to edit the build scripts.
3. **Run tests locally:** 
   * Run `make tests`. 
   * *(Note: `make tests_full` runs the exact same test suite, it just enables verbose debug output in the console. You can run `make` or `make list` to see all dynamically generated targets).*
4. **Push to your fork and check CI:** The GitHub Actions workflow **must be green**. It automatically runs an aggressive matrix (Sanitizers, strict ARM alignment, Big Endian s390x, and 16-bit fallbacks). Let the robots do the heavy lifting.
5. **Feed it to the Fuzzer:** Run the fuzzers locally for the standard 5 minutes each. 
   * Just like tests, if you add a new `.c` file to the `fuzzers/` directory (name it `yourfeature_fuzzer.c`), the `Makefile` will automatically detect it. Check `make list` for your new `make fuzz_yourfeature` target. 
   * *Tip: If you implemented a new sub-allocator, please create a dedicated standalone fuzzer for it to test its specific mechanics, AND update the existing `chaos_fuzzer.c` to include it in the global chaos!*
6. **Open the PR.** 

*Don't worry about strict commit message formats (like `feat(core): ...`). Just write what you did in plain English.*

## Code Style
*   Try to match the surrounding code style.
*   If you add a new API function, please add a small comment explaining *what* it does and *why*.
*   If you implement a complex bit-manipulation hack, write a "WHY DOING THIS?" comment. I love reading about the physics of the code!

## Let's make C memory management *easy*.
If you're unsure about an architectural decision, just open a Draft PR or an Issue, and we'll discuss it. I'm always open to new ideas.
