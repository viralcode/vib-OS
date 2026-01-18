/*
 * Vib-OS Kernel - ARM64 Architecture Implementation
 */

#include "arch/arch.h"
#include "arch/arm64/gic.h"
#include "arch/arm64/timer.h"
#include "printk.h"
#include "types.h"

/* Forward declarations for timer functions */
extern uint64_t timer_get_count(void);
extern uint64_t timer_get_frequency(void);

/* ===================================================================== */
/* SMP (Symmetric Multi-Processing) Support */
/* ===================================================================== */

#define MAX_CPUS 8

/* Per-CPU data */
struct cpu_data {
    uint32_t cpu_id;
    uint32_t online;
    void *stack;
    void (*entry)(void);
};

static struct cpu_data cpu_info[MAX_CPUS];
static volatile uint32_t num_cpus_online = 1;  /* Boot CPU is online */
static volatile uint32_t smp_initialized = 0;

/* Spinlock for SMP synchronization */
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spin_lock(spinlock_t *lock)
{
    uint32_t tmp;
    asm volatile(
        "sevl\n"
        "1: wfe\n"
        "2: ldaxr %w0, [%1]\n"
        "   cbnz %w0, 1b\n"
        "   stxr %w0, %w2, [%1]\n"
        "   cbnz %w0, 2b\n"
        : "=&r" (tmp)
        : "r" (&lock->lock), "r" (1)
        : "memory"
    );
}

static inline void spin_unlock(spinlock_t *lock)
{
    asm volatile("stlr wzr, [%0]" :: "r" (&lock->lock) : "memory");
}

/* Global kernel lock for SMP safety */
static spinlock_t kernel_lock = SPINLOCK_INIT;

void smp_lock(void)
{
    spin_lock(&kernel_lock);
}

void smp_unlock(void)
{
    spin_unlock(&kernel_lock);
}

/* Get current CPU ID */
uint32_t smp_processor_id(void)
{
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    return mpidr & 0xFF;  /* Aff0 is the CPU ID on most systems */
}

/* Get number of online CPUs */
uint32_t smp_num_cpus(void)
{
    return num_cpus_online;
}

/* Secondary CPU entry point (called from assembly) */
void secondary_cpu_init(void)
{
    uint32_t cpu_id = smp_processor_id();
    
    printk(KERN_INFO "SMP: CPU %u coming online\n", cpu_id);
    
    /* Initialize GIC for this CPU */
    gic_cpu_init();
    
    /* Mark CPU as online */
    cpu_info[cpu_id].online = 1;
    __atomic_add_fetch(&num_cpus_online, 1, __ATOMIC_SEQ_CST);
    
    /* Enable interrupts */
    arch_irq_enable();
    
    printk(KERN_INFO "SMP: CPU %u online\n", cpu_id);
    
    /* Enter idle loop - wait for work */
    while (1) {
        asm volatile("wfe");  /* Wait for event */
    }
}

/* Boot secondary CPUs (PSCI method for QEMU virt) */
int smp_boot_secondary(uint32_t cpu_id, void (*entry)(void), void *stack)
{
    if (cpu_id >= MAX_CPUS || cpu_id == 0) return -1;
    if (cpu_info[cpu_id].online) return 0;  /* Already online */
    
    cpu_info[cpu_id].cpu_id = cpu_id;
    cpu_info[cpu_id].entry = entry;
    cpu_info[cpu_id].stack = stack;
    
    /* Use PSCI CPU_ON to start the secondary CPU */
    /* PSCI function IDs */
    #define PSCI_CPU_ON_64 0xC4000003
    
    uint64_t target_cpu = cpu_id;
    uint64_t entry_point = (uint64_t)entry;
    uint64_t context_id = cpu_id;
    int64_t ret;
    
    asm volatile(
        "mov x0, %1\n"       /* PSCI function ID */
        "mov x1, %2\n"       /* target CPU */
        "mov x2, %3\n"       /* entry point */
        "mov x3, %4\n"       /* context ID */
        "hvc #0\n"           /* Hypervisor call */
        "mov %0, x0\n"       /* Return value */
        : "=r" (ret)
        : "r" ((uint64_t)PSCI_CPU_ON_64), "r" (target_cpu), 
          "r" (entry_point), "r" (context_id)
        : "x0", "x1", "x2", "x3", "memory"
    );
    
    if (ret == 0) {
        printk(KERN_INFO "SMP: Booting CPU %u\n", cpu_id);
        return 0;
    } else {
        printk(KERN_WARNING "SMP: Failed to boot CPU %u (PSCI error %lld)\n", 
               cpu_id, (long long)ret);
        return -1;
    }
}

/* Initialize SMP subsystem */
void smp_init(void)
{
    if (smp_initialized) return;
    
    printk(KERN_INFO "SMP: Initializing multiprocessor support\n");
    
    /* Initialize boot CPU info */
    cpu_info[0].cpu_id = 0;
    cpu_info[0].online = 1;
    
    smp_initialized = 1;
    
    printk(KERN_INFO "SMP: Boot CPU (CPU 0) initialized\n");
    
    /* Note: Secondary CPUs are not auto-booted.
     * Call smp_boot_secondary() to start them when ready.
     * For QEMU virt with -smp N, CPUs wait for PSCI CPU_ON.
     */
}

/* ===================================================================== */
/* Early Initialization */
/* ===================================================================== */

void arch_early_init(void)
{
    printk(KERN_INFO "ARM64: Early initialization complete\n");
}

void arch_init(void)
{
    printk(KERN_INFO "ARM64: Full initialization complete\n");
}

/* ===================================================================== */
/* Interrupt Management */
/* ===================================================================== */

void arch_irq_enable(void)
{
    asm volatile("msr daifclr, #2" ::: "memory");
}

void arch_irq_disable(void)
{
    asm volatile("msr daifset, #2" ::: "memory");
}

unsigned long arch_irq_save(void)
{
    unsigned long flags;
    asm volatile(
        "mrs %0, daif\n"
        "msr daifset, #2"
        : "=r"(flags) :: "memory"
    );
    return flags;
}

void arch_irq_restore(unsigned long flags)
{
    asm volatile("msr daif, %0" :: "r"(flags) : "memory");
}

void arch_irq_init(void)
{
    gic_init();
}

/* ===================================================================== */
/* Timer Management */
/* ===================================================================== */

void arch_timer_init(void)
{
    timer_init();
}

uint64_t arch_timer_get_ticks(void)
{
    return timer_get_count();
}

uint64_t arch_timer_get_frequency(void)
{
    return timer_get_frequency();
}

uint64_t arch_timer_get_ms(void)
{
    uint64_t ticks = timer_get_count();
    uint64_t freq = timer_get_frequency();
    return (ticks * 1000) / freq;
}

/* ===================================================================== */
/* Memory Management */
/* ===================================================================== */

void arch_mmu_init(void)
{
    /* MMU setup is done in vmm.c */
    printk(KERN_INFO "ARM64: MMU initialization\n");
}

void arch_mmu_enable(void)
{
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0);  /* M bit: Enable MMU */
    sctlr |= (1 << 2);  /* C bit: Enable data cache */
    sctlr |= (1 << 12); /* I bit: Enable instruction cache */
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    asm volatile("isb");
}

void arch_mmu_switch_context(phys_addr_t pgd)
{
    asm volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb sy\n"
        "isb"
        :: "r"(pgd)
    );
}

void arch_mmu_invalidate_tlb(virt_addr_t vaddr)
{
    if (vaddr == 0) {
        /* Invalidate all */
        asm volatile(
            "tlbi vmalle1\n"
            "dsb sy\n"
            "isb"
            ::: "memory"
        );
    } else {
        /* Invalidate single page */
        asm volatile(
            "tlbi vae1, %0\n"
            "dsb sy\n"
            "isb"
            :: "r"(vaddr >> 12)
            : "memory"
        );
    }
}

/* ===================================================================== */
/* Context Switching */
/* ===================================================================== */

void arch_context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx)
{
    /* Save current context */
    asm volatile(
        "stp x0, x1, [%0, #0]\n"
        "stp x2, x3, [%0, #16]\n"
        "stp x4, x5, [%0, #32]\n"
        "stp x6, x7, [%0, #48]\n"
        "stp x8, x9, [%0, #64]\n"
        "stp x10, x11, [%0, #80]\n"
        "stp x12, x13, [%0, #96]\n"
        "stp x14, x15, [%0, #112]\n"
        "stp x16, x17, [%0, #128]\n"
        "stp x18, x19, [%0, #144]\n"
        "stp x20, x21, [%0, #160]\n"
        "stp x22, x23, [%0, #176]\n"
        "stp x24, x25, [%0, #192]\n"
        "stp x26, x27, [%0, #208]\n"
        "stp x28, x29, [%0, #224]\n"
        "str x30, [%0, #240]\n"
        "mov x1, sp\n"
        "str x1, [%0, #248]\n"
        :: "r"(old_ctx)
        : "x1", "memory"
    );
    
    /* Save PC and PSTATE */
    asm volatile(
        "adr x1, 1f\n"
        "str x1, [%0, #256]\n"
        "mrs x1, nzcv\n"
        "str x1, [%0, #264]\n"
        "1:\n"
        :: "r"(old_ctx)
        : "x1", "memory"
    );
    
    /* Restore new context */
    asm volatile(
        "ldp x0, x1, [%0, #0]\n"
        "ldp x2, x3, [%0, #16]\n"
        "ldp x4, x5, [%0, #32]\n"
        "ldp x6, x7, [%0, #48]\n"
        "ldp x8, x9, [%0, #64]\n"
        "ldp x10, x11, [%0, #80]\n"
        "ldp x12, x13, [%0, #96]\n"
        "ldp x14, x15, [%0, #112]\n"
        "ldp x16, x17, [%0, #128]\n"
        "ldp x18, x19, [%0, #144]\n"
        "ldp x20, x21, [%0, #160]\n"
        "ldp x22, x23, [%0, #176]\n"
        "ldp x24, x25, [%0, #192]\n"
        "ldp x26, x27, [%0, #208]\n"
        "ldp x28, x29, [%0, #224]\n"
        "ldr x30, [%0, #240]\n"
        "ldr x1, [%0, #248]\n"
        "mov sp, x1\n"
        "ldr x1, [%0, #256]\n"
        "br x1\n"
        :: "r"(new_ctx)
    );
}

void arch_context_init(cpu_context_t *ctx, void (*entry)(void*), void *stack, void *arg)
{
    /* Zero out context */
    for (size_t i = 0; i < sizeof(cpu_context_t); i++) {
        ((uint8_t*)ctx)[i] = 0;
    }
    
    /* Set up initial state */
    ctx->pc = (uint64_t)entry;
    ctx->sp = (uint64_t)stack;
    ctx->x[0] = (uint64_t)arg;  /* First argument in ARM64 calling convention */
    ctx->pstate = 0x3C5;        /* EL1h, IRQs masked */
}

/* ===================================================================== */
/* CPU Information */
/* ===================================================================== */

uint32_t arch_cpu_id(void)
{
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xFF;
}

uint32_t arch_cpu_count(void)
{
    /* TODO: Parse device tree for CPU count */
    return 1;
}

void arch_cpu_info(char *buf, size_t size)
{
    uint64_t midr;
    asm volatile("mrs %0, midr_el1" : "=r"(midr));
    
    uint32_t implementer = (midr >> 24) & 0xFF;
    uint32_t variant = (midr >> 20) & 0xF;
    uint32_t arch = (midr >> 16) & 0xF;
    uint32_t partnum = (midr >> 4) & 0xFFF;
    uint32_t revision = midr & 0xF;
    
    /* Simple format */
    const char *impl_name = "Unknown";
    if (implementer == 0x41) impl_name = "ARM";
    else if (implementer == 0x61) impl_name = "Apple";
    
    int written = 0;
    const char *src = impl_name;
    while (*src && written < (int)size - 1) {
        buf[written++] = *src++;
    }
    
    if (written < (int)size - 1) {
        buf[written++] = ' ';
    }
    
    /* Add part number in hex */
    if (written < (int)size - 5) {
        buf[written++] = '0';
        buf[written++] = 'x';
        for (int i = 2; i >= 0; i--) {
            uint8_t nibble = (partnum >> (i * 4)) & 0xF;
            buf[written++] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        }
    }
    
    buf[written] = '\0';
    
    (void)variant;
    (void)arch;
    (void)revision;
}

/* ===================================================================== */
/* Low-Level Utilities */
/* ===================================================================== */

void arch_halt(void)
{
    while (1) {
        asm volatile("msr daifset, #0xf; wfi");
    }
}

void arch_idle(void)
{
    asm volatile("wfi");
}

void arch_barrier(void)
{
    asm volatile("dmb sy" ::: "memory");
}

void arch_dsb(void)
{
    asm volatile("dsb sy" ::: "memory");
}

void arch_isb(void)
{
    asm volatile("isb" ::: "memory");
}
