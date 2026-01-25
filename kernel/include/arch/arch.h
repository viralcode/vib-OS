/*
 * Vib-OS - Architecture Abstraction Layer
 * 
 * This header provides a unified interface for architecture-specific
 * functionality across ARM64, x86_64, and x86.
 */

#ifndef _ARCH_H
#define _ARCH_H

#include "types.h"

/* ===================================================================== */
/* Architecture Detection */
/* ===================================================================== */

#if defined(__aarch64__)
    #define ARCH_ARM64 1
    #define ARCH_NAME "arm64"
#elif defined(__x86_64__)
    #define ARCH_X86_64 1
    #define ARCH_NAME "x86_64"
#elif defined(__i386__)
    #define ARCH_X86 1
    #define ARCH_NAME "x86"
#else
    #error "Unsupported architecture"
#endif

/* ===================================================================== */
/* Architecture-Specific Types */
/* ===================================================================== */

#ifdef ARCH_ARM64
    typedef uint64_t arch_reg_t;
    #define PAGE_SHIFT 12
    #define PAGE_SIZE (1UL << PAGE_SHIFT)
#elif defined(ARCH_X86_64)
    typedef uint64_t arch_reg_t;
    #define PAGE_SHIFT 12
    #define PAGE_SIZE (1UL << PAGE_SHIFT)
#elif defined(ARCH_X86)
    typedef uint32_t arch_reg_t;
    #define PAGE_SHIFT 12
    #define PAGE_SIZE (1UL << PAGE_SHIFT)
#endif

/* ===================================================================== */
/* CPU Context for Context Switching */
/* ===================================================================== */

typedef struct {
#ifdef ARCH_ARM64
    uint64_t x[31];     /* x0-x30 */
    uint64_t sp;        /* Stack pointer */
    uint64_t pc;        /* Program counter */
    uint64_t pstate;    /* Processor state */
#elif defined(ARCH_X86_64)
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cs, ss;
#elif defined(ARCH_X86)
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs, ss, ds, es, fs, gs;
#endif
} cpu_context_t;

/* ===================================================================== */
/* Architecture Initialization */
/* ===================================================================== */

/**
 * arch_early_init - Very early architecture initialization
 * Called before any other subsystems are initialized
 */
void arch_early_init(void);

/**
 * arch_init - Full architecture initialization
 * Called after memory management is set up
 */
void arch_init(void);

/* ===================================================================== */
/* Interrupt Management */
/* ===================================================================== */

/**
 * arch_irq_enable - Enable interrupts
 */
void arch_irq_enable(void);

/**
 * arch_irq_disable - Disable interrupts
 */
void arch_irq_disable(void);

/**
 * arch_irq_save - Save interrupt state and disable
 * @return: Previous interrupt state
 */
unsigned long arch_irq_save(void);

/**
 * arch_irq_restore - Restore interrupt state
 * @flags: Interrupt state to restore
 */
void arch_irq_restore(unsigned long flags);

/**
 * arch_irq_init - Initialize interrupt controller
 */
void arch_irq_init(void);

/* ===================================================================== */
/* Timer Management */
/* ===================================================================== */

/**
 * arch_timer_init - Initialize system timer
 */
void arch_timer_init(void);

/**
 * arch_timer_get_ticks - Get current timer ticks
 * @return: Current tick count
 */
uint64_t arch_timer_get_ticks(void);

/**
 * arch_timer_get_frequency - Get timer frequency in Hz
 * @return: Timer frequency
 */
uint64_t arch_timer_get_frequency(void);

/**
 * arch_timer_get_ms - Get current time in milliseconds
 * @return: Time in milliseconds since boot
 */
uint64_t arch_timer_get_ms(void);

/* ===================================================================== */
/* Context Switching */
/* ===================================================================== */

/**
 * switch_context - Switch CPU context between tasks
 * @old: Pointer to save old context
 * @new: Pointer to load new context
 * 
 * This is implemented in assembly (switch.S) for each architecture
 */
void switch_context(cpu_context_t *old, cpu_context_t *new);

/* ===================================================================== */
/* Memory Management */
/* ===================================================================== */

/**
 * arch_mmu_init - Initialize MMU/paging
 */
void arch_mmu_init(void);

/**
 * arch_mmu_enable - Enable MMU/paging
 */
void arch_mmu_enable(void);

/**
 * arch_mmu_switch_context - Switch to different page table
 * @pgd: Physical address of page directory/table
 */
void arch_mmu_switch_context(phys_addr_t pgd);

/**
 * arch_mmu_invalidate_tlb - Invalidate TLB entries
 * @vaddr: Virtual address to invalidate (0 = all)
 */
void arch_mmu_invalidate_tlb(virt_addr_t vaddr);

/* ===================================================================== */
/* Context Switching */
/* ===================================================================== */

/**
 * arch_context_switch - Switch CPU context
 * @old_ctx: Pointer to save current context
 * @new_ctx: Pointer to context to restore
 */
void arch_context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

/**
 * arch_context_init - Initialize a new context
 * @ctx: Context to initialize
 * @entry: Entry point function
 * @stack: Stack pointer
 * @arg: Argument to pass to entry point
 */
void arch_context_init(cpu_context_t *ctx, void (*entry)(void*), void *stack, void *arg);

/**
 * arch_context_get_pc - Get program counter from context
 * @ctx: Context to read from
 * @return: Program counter value
 */
static inline uint64_t arch_context_get_pc(cpu_context_t *ctx) {
#ifdef ARCH_ARM64
    return ctx->pc;
#elif defined(ARCH_X86_64)
    return ctx->rip;
#elif defined(ARCH_X86)
    return ctx->eip;
#else
    return 0;
#endif
}

/**
 * arch_context_get_sp - Get stack pointer from context
 * @ctx: Context to read from
 * @return: Stack pointer value
 */
static inline uint64_t arch_context_get_sp(cpu_context_t *ctx) {
#ifdef ARCH_ARM64
    return ctx->sp;
#elif defined(ARCH_X86_64)
    return ctx->rsp;
#elif defined(ARCH_X86)
    return ctx->esp;
#else
    return 0;
#endif
}

/**
 * arch_context_set_pc - Set program counter in context
 * @ctx: Context to modify
 * @pc: Program counter value
 */
static inline void arch_context_set_pc(cpu_context_t *ctx, uint64_t pc) {
#ifdef ARCH_ARM64
    ctx->pc = pc;
#elif defined(ARCH_X86_64)
    ctx->rip = pc;
#elif defined(ARCH_X86)
    ctx->eip = (uint32_t)pc;
#endif
}

/**
 * arch_context_set_sp - Set stack pointer in context
 * @ctx: Context to modify
 * @sp: Stack pointer value
 */
static inline void arch_context_set_sp(cpu_context_t *ctx, uint64_t sp) {
#ifdef ARCH_ARM64
    ctx->sp = sp;
#elif defined(ARCH_X86_64)
    ctx->rsp = sp;
#elif defined(ARCH_X86)
    ctx->esp = (uint32_t)sp;
#endif
}

/**
 * arch_context_set_flags - Set processor flags in context
 * @ctx: Context to modify
 * @flags: Flags value (architecture-specific)
 */
static inline void arch_context_set_flags(cpu_context_t *ctx, uint64_t flags) {
#ifdef ARCH_ARM64
    ctx->pstate = flags;
#elif defined(ARCH_X86_64)
    ctx->rflags = flags;
#elif defined(ARCH_X86)
    ctx->eflags = (uint32_t)flags;
#endif
}

/* ===================================================================== */
/* CPU Information */
/* ===================================================================== */

/**
 * arch_cpu_id - Get current CPU ID
 * @return: CPU ID (0 for single-core systems)
 */
uint32_t arch_cpu_id(void);

/**
 * arch_cpu_count - Get number of CPUs
 * @return: Number of CPUs
 */
uint32_t arch_cpu_count(void);

/**
 * arch_cpu_info - Get CPU information string
 * @buf: Buffer to write info to
 * @size: Size of buffer
 */
void arch_cpu_info(char *buf, size_t size);

/* ===================================================================== */
/* Low-Level Utilities */
/* ===================================================================== */

/**
 * arch_halt - Halt the CPU
 */
void arch_halt(void) __attribute__((noreturn));

/**
 * arch_idle - Put CPU in low-power idle state
 */
void arch_idle(void);

/**
 * arch_barrier - Memory barrier
 */
void arch_barrier(void);

/**
 * arch_dsb - Data synchronization barrier
 */
void arch_dsb(void);

/**
 * arch_isb - Instruction synchronization barrier
 */
void arch_isb(void);

/* ===================================================================== */
/* Port I/O (x86 only) */
/* ===================================================================== */

#if defined(ARCH_X86_64) || defined(ARCH_X86)

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif /* x86 */

#endif /* _ARCH_H */
