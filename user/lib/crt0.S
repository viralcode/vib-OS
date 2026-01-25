/*
 * VibeOS C Runtime Startup
 *
 * Entry point for all userspace programs.
 * Receives: x0=kapi_t*, x1=argc, x2=argv
 */

.section ".text.start"
.global _start

_start:
    // x0 = kapi_t*, x1 = argc, x2 = argv
    // x30 contains return address to kernel - save it!

    // Save x30 (return address) and x29 (frame pointer)
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    // Call main(kapi_t *api, int argc, char **argv)
    // x0, x1, x2 already set by kernel
    bl      main

    // Restore x29 and x30, then return to kernel
    ldp     x29, x30, [sp], #16
    ret

/*
 * memcpy - Copy n bytes from src to dest
 * void *memcpy(void *dest, const void *src, size_t n)
 * x0 = dest, x1 = src, x2 = n
 * Returns dest in x0
 */
.section ".text"
.global memcpy
memcpy:
    cbz     x2, .memcpy_done    // If n == 0, return
    mov     x3, x0              // Save dest for return value
.memcpy_loop:
    ldrb    w4, [x1], #1        // Load byte from src, post-increment
    strb    w4, [x0], #1        // Store byte to dest, post-increment
    subs    x2, x2, #1          // n--
    b.ne    .memcpy_loop        // Loop if n != 0
    mov     x0, x3              // Return original dest
.memcpy_done:
    ret

/*
 * memmove - Copy n bytes, handles overlapping regions
 * void *memmove(void *dest, const void *src, size_t n)
 */
.global memmove
memmove:
    cbz     x2, .memmove_done
    mov     x3, x0              // Save dest for return
    cmp     x0, x1
    b.ls    .memmove_forward    // If dest <= src, copy forward
    // Copy backward (dest > src, potential overlap)
    add     x0, x0, x2          // dest + n
    add     x1, x1, x2          // src + n
.memmove_backward:
    ldrb    w4, [x1, #-1]!      // Load byte, pre-decrement
    strb    w4, [x0, #-1]!      // Store byte, pre-decrement
    subs    x2, x2, #1
    b.ne    .memmove_backward
    mov     x0, x3
    ret
.memmove_forward:
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    subs    x2, x2, #1
    b.ne    .memmove_forward
    mov     x0, x3
.memmove_done:
    ret

/*
 * memset - Set n bytes to c
 * void *memset(void *s, int c, size_t n)
 */
.global memset
memset:
    cbz     x2, .memset_done
    mov     x3, x0              // Save s for return
.memset_loop:
    strb    w1, [x0], #1        // Store byte, post-increment
    subs    x2, x2, #1
    b.ne    .memset_loop
    mov     x0, x3
.memset_done:
    ret
