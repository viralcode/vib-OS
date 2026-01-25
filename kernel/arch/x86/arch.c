/*
 * x86 32-bit Architecture Implementation
 */

#include "arch/arch.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* CPU Context (defined in arch.h) */
/* ===================================================================== */

/* x86 32-bit uses the cpu_context_t from arch.h */

/* ===================================================================== */
/* Timer */
/* ===================================================================== */

static volatile uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 100; /* 100 Hz */

void arch_timer_init(void)
{
    extern void pit_init(void);
    pit_init();
}

uint64_t arch_timer_get_ticks(void)
{
    return timer_ticks;
}

uint64_t arch_timer_get_frequency(void)
{
    return timer_frequency;
}

uint64_t arch_timer_get_ms(void)
{
    return (timer_ticks * 1000) / timer_frequency;
}

void arch_timer_tick(void)
{
    timer_ticks++;
}

/* ===================================================================== */
/* Interrupts */
/* ===================================================================== */

void arch_irq_enable(void)
{
    asm volatile("sti");
}

void arch_irq_disable(void)
{
    asm volatile("cli");
}

void arch_irq_init(void)
{
    extern void pic_init(void);
    pic_init();
}

/* ===================================================================== */
/* CPU Operations */
/* ===================================================================== */

void arch_cpu_idle(void)
{
    asm volatile("hlt");
}

void arch_cpu_relax(void)
{
    asm volatile("pause");
}

void arch_memory_barrier(void)
{
    asm volatile("" ::: "memory");
}

/* Legacy function names for compatibility */
void arch_halt(void)
{
    arch_cpu_idle();
}

void arch_idle(void)
{
    arch_cpu_idle();
}

void arch_dsb(void)
{
    arch_memory_barrier();
}

/* ===================================================================== */
/* MMU/Paging */
/* ===================================================================== */

void arch_mmu_init(void)
{
    /* MMU setup is done in vmm.c */
}

void arch_mmu_enable(void)
{
    /* Enable paging */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* Set PG bit */
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}

void arch_mmu_switch_context(phys_addr_t pgd)
{
    asm volatile("mov %0, %%cr3" :: "r"((uint32_t)pgd) : "memory");
}

/* ===================================================================== */
/* Context Accessors are defined as inline in arch.h */
/* ===================================================================== */
/* Architecture-specific initialization */
/* ===================================================================== */

void arch_early_init(void)
{
    /* Early x86 initialization */
}

void arch_init(void)
{
    printk("ARCH: Initializing x86 32-bit\n");
    
    /* Initialize PIC (Programmable Interrupt Controller) */
    arch_irq_init();
    
    /* Initialize PIT (Programmable Interval Timer) */
    arch_timer_init();
    
    printk("ARCH: x86 initialization complete\n");
}

/* ===================================================================== */
/* Process Entry Wrapper (architecture-specific) */
/* ===================================================================== */

void process_entry_wrapper(void)
{
    /* This function is called when a new process starts */
    /* It's defined in assembly for ARM64, but we can do it in C for x86 */
    
    /* Enable interrupts */
    arch_irq_enable();
    
    /* Get the entry point and argument from the context */
    /* This will be set up by the process creation code */
    
    /* For now, just halt */
    while (1) {
        arch_cpu_idle();
    }
}
