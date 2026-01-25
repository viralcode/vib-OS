/*
 * Vib-OS Context Switch (ported from VibeOS)
 *
 * Saves ALL registers for preemptive multitasking.
 * FPU state save/restore removed for simplicity.
 *
 * AArch64 cpu_context_t layout:
 *   0x000 - 0x0F0: x[0-30] (31 registers, 248 bytes)
 *   0x0F8: sp
 *   0x100: pc (return address)
 *   0x108: pstate
 */

.global switch_context
.type process_context_switch, %function

/*
 * void process_context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx)
 *
 * old_ctx: x0 - where to save current context
 * new_ctx: x1 - context to restore
 */

switch_context:
    // Save current context to old_ctx (x0)
    cbz     x0, .Lrestore

    // Save x2-x30 first
    stp     x2,  x3,  [x0, #0x10]
    stp     x4,  x5,  [x0, #0x20]
    stp     x6,  x7,  [x0, #0x30]
    stp     x8,  x9,  [x0, #0x40]
    stp     x10, x11, [x0, #0x50]
    stp     x12, x13, [x0, #0x60]
    stp     x14, x15, [x0, #0x70]
    stp     x16, x17, [x0, #0x80]
    stp     x18, x19, [x0, #0x90]
    stp     x20, x21, [x0, #0xa0]
    stp     x22, x23, [x0, #0xb0]
    stp     x24, x25, [x0, #0xc0]
    stp     x26, x27, [x0, #0xd0]
    stp     x28, x29, [x0, #0xe0]
    str     x30, [x0, #0xf0]

    // Save x0, x1 (parameter values)
    mov     x2, x0
    mov     x3, x1
    stp     x2, x3, [x0, #0x00]

    // Save sp
    mov     x4, sp
    str     x4, [x2, #0xf8]

    // Save pc (link register = return address)
    str     x30, [x2, #0x100]

    // Save pstate (DAIF | mode)
    mrs     x4, daif
    mov     x5, #5              // EL1h mode
    orr     x4, x4, x5
    str     x4, [x2, #0x108]

    // Restore new_ctx pointer
    mov     x1, x3

.Lrestore:
    // Restore sp
    ldr     x2, [x1, #0xf8]
    mov     sp, x2

    // Set up elr_el1 and spsr_el1 for eret
    ldr     x2, [x1, #0x100]    // pc -> elr_el1
    msr     elr_el1, x2
    ldr     x2, [x1, #0x108]    // pstate -> spsr_el1
    msr     spsr_el1, x2

    // Restore general purpose registers x2-x30
    ldp     x2,  x3,  [x1, #0x10]
    ldp     x4,  x5,  [x1, #0x20]
    ldp     x6,  x7,  [x1, #0x30]
    ldp     x8,  x9,  [x1, #0x40]
    ldp     x10, x11, [x1, #0x50]
    ldp     x12, x13, [x1, #0x60]
    ldp     x14, x15, [x1, #0x70]
    ldp     x16, x17, [x1, #0x80]
    ldp     x18, x19, [x1, #0x90]
    ldp     x20, x21, [x1, #0xa0]
    ldp     x22, x23, [x1, #0xb0]
    ldp     x24, x25, [x1, #0xc0]
    ldp     x26, x27, [x1, #0xd0]
    ldp     x28, x29, [x1, #0xe0]
    ldr     x30, [x1, #0xf0]

    // Restore x0, x1 last
    ldp     x0, x1, [x1, #0x00]

    // Use eret to properly restore PSTATE
    eret


/* ===================================================================== */
/* Legacy cpu_switch_to for old scheduler */
/* ===================================================================== */

.global cpu_switch_to
.type cpu_switch_to, %function

#define OLD_X19     0
#define OLD_X20     8
#define OLD_X21     16
#define OLD_X22     24
#define OLD_X23     32
#define OLD_X24     40
#define OLD_X25     48
#define OLD_X26     56
#define OLD_X27     64
#define OLD_X28     72
#define OLD_FP      80
#define OLD_SP      88
#define OLD_PC      96

cpu_switch_to:
    mov     x8, x0
    
    stp     x19, x20, [x8, #OLD_X19]
    stp     x21, x22, [x8, #OLD_X21]
    stp     x23, x24, [x8, #OLD_X23]
    stp     x25, x26, [x8, #OLD_X25]
    stp     x27, x28, [x8, #OLD_X27]
    stp     x29, x30, [x8, #OLD_FP]
    mov     x9, sp
    str     x9, [x8, #OLD_SP]
    str     x30, [x8, #OLD_PC]
    
    mov     x8, x1
    
    ldp     x19, x20, [x8, #OLD_X19]
    ldp     x21, x22, [x8, #OLD_X21]
    ldp     x23, x24, [x8, #OLD_X23]
    ldp     x25, x26, [x8, #OLD_X25]
    ldp     x27, x28, [x8, #OLD_X27]
    ldp     x29, x30, [x8, #OLD_FP]
    ldr     x9, [x8, #OLD_SP]
    mov     sp, x9
    
    ret


/* ===================================================================== */
/* Task entry wrapper (for old scheduler) */
/* ===================================================================== */

.global task_entry_wrapper
.type task_entry_wrapper, %function

task_entry_wrapper:
    msr     daifclr, #2
    mov     x0, x20
    blr     x19
    mov     x0, #0
    bl      exit_task
1:
    wfi
    b       1b
