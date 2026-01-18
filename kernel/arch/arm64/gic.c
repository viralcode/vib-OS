/*
 * UnixOS Kernel - GIC (Generic Interrupt Controller) Implementation
 */

#include "arch/arm64/gic.h"
#include "printk.h"

/* ===================================================================== */
/* GIC base addresses */
/* ===================================================================== */

/* QEMU virt machine addresses */
#define GICD_BASE           0x08000000UL
#define GICR_BASE           0x080A0000UL

/* GICv3 stride between redistributors (each CPU has 64KB + 64KB) */
#define GICR_STRIDE         (2 * 0x10000)

/* ===================================================================== */
/* Interrupt handler table */
/* ===================================================================== */

struct irq_desc {
    irq_handler_t handler;
    void *data;
};

static struct irq_desc irq_table[GIC_MAX_IRQS];

/* ===================================================================== */
/* MMIO helpers */
/* ===================================================================== */

static inline void gicd_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(GICD_BASE + offset) = val;
}

static inline uint32_t gicd_read(uint32_t offset)
{
    return *(volatile uint32_t *)(GICD_BASE + offset);
}

static inline void gicr_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(GICR_BASE + offset) = val;
}

static inline uint32_t gicr_read(uint32_t offset)
{
    return *(volatile uint32_t *)(GICR_BASE + offset);
}

/* ===================================================================== */
/* System register helpers */
/* ===================================================================== */

static inline void gic_write_icc_pmr(uint64_t val)
{
    asm volatile("msr " ICC_PMR_EL1 ", %0" : : "r" (val));
}

static inline void gic_write_icc_igrpen1(uint64_t val)
{
    asm volatile("msr " ICC_IGRPEN1_EL1 ", %0" : : "r" (val));
}

static inline void gic_write_icc_sre(uint64_t val)
{
    asm volatile("msr " ICC_SRE_EL1 ", %0" : : "r" (val));
}

static inline uint64_t gic_read_icc_sre(void)
{
    uint64_t val;
    asm volatile("mrs %0, " ICC_SRE_EL1 : "=r" (val));
    return val;
}

static inline uint32_t gic_read_icc_iar1(void)
{
    uint64_t val;
    asm volatile("mrs %0, " ICC_IAR1_EL1 : "=r" (val));
    return (uint32_t)val;
}

static inline void gic_write_icc_eoir1(uint64_t val)
{
    asm volatile("msr " ICC_EOIR1_EL1 ", %0" : : "r" (val));
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

static void gic_init_distributor(void)
{
    uint32_t typer = gicd_read(GICD_TYPER);
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;
    
    printk(KERN_INFO "GIC: Distributor supports %u IRQs\n", num_irqs);
    
    /* Disable distributor */
    gicd_write(GICD_CTLR, 0);
    
    /* Configure all SPIs as Group 1, edge-triggered */
    for (uint32_t i = GIC_SPI_START; i < num_irqs; i += 32) {
        /* Set all to Group 1 (non-secure) */
        gicd_write(GICD_IGROUPR + (i / 32) * 4, 0xFFFFFFFF);
        
        /* Disable all */
        gicd_write(GICD_ICENABLER + (i / 32) * 4, 0xFFFFFFFF);
        
        /* Clear pending */
        gicd_write(GICD_ICPENDR + (i / 32) * 4, 0xFFFFFFFF);
    }
    
    /* Set default priority for all SPIs */
    for (uint32_t i = GIC_SPI_START; i < num_irqs; i += 4) {
        gicd_write(GICD_IPRIORITYR + i, 0x80808080);
    }
    
    /* Enable distributor with affinity routing */
    gicd_write(GICD_CTLR, GICD_CTLR_ENABLE_G1NS | GICD_CTLR_ARE_NS);
    
    printk(KERN_INFO "GIC: Distributor initialized\n");
}

static void gic_init_redistributor(void)
{
    /* Wake up redistributor */
    uint32_t waker = gicr_read(GICR_WAKER);
    waker &= ~GICR_WAKER_PROCESSOR_SLEEP;
    gicr_write(GICR_WAKER, waker);
    
    /* Wait for children to wake */
    while (gicr_read(GICR_WAKER) & GICR_WAKER_CHILDREN_ASLEEP) {
        /* Spin */
    }
    
    /* Configure PPIs and SGIs in SGI_base */
    uint64_t sgi_base = GICR_BASE + GICR_SGI_BASE;
    
    /* All PPIs/SGIs to Group 1 */
    *(volatile uint32_t *)(sgi_base + GICR_IGROUPR0) = 0xFFFFFFFF;
    
    /* Disable all PPIs/SGIs */
    *(volatile uint32_t *)(sgi_base + GICR_ICENABLER0) = 0xFFFFFFFF;
    
    /* Clear pending */
    *(volatile uint32_t *)(sgi_base + 0x0280) = 0xFFFFFFFF;
    
    /* Set default priority */
    for (int i = 0; i < 32; i += 4) {
        *(volatile uint32_t *)(sgi_base + GICR_IPRIORITYR + i) = 0x80808080;
    }
    
    printk(KERN_INFO "GIC: Redistributor initialized\n");
}

static void gic_init_cpu_interface(void)
{
    /* Enable system register interface */
    uint64_t sre = gic_read_icc_sre();
    sre |= 0x7;  /* SRE, DFB, DIB */
    gic_write_icc_sre(sre);
    asm volatile("isb");
    
    /* Set priority mask to allow all priorities */
    gic_write_icc_pmr(0xFF);
    
    /* Enable Group 1 interrupts */
    gic_write_icc_igrpen1(1);
    
    asm volatile("isb");
    
    printk(KERN_INFO "GIC: CPU interface initialized\n");
}

void gic_init(void)
{
    printk(KERN_INFO "GIC: Initializing GICv3\n");
    
    /* Clear handler table */
    for (int i = 0; i < GIC_MAX_IRQS; i++) {
        irq_table[i].handler = NULL;
        irq_table[i].data = NULL;
    }
    
    gic_init_distributor();
    gic_init_redistributor();
    gic_init_cpu_interface();
    
    printk(KERN_INFO "GIC: Initialization complete\n");
}

/* Initialize GIC for secondary CPUs (SMP support) */
void gic_cpu_init(void)
{
    /* Each secondary CPU needs to initialize its redistributor and CPU interface */
    gic_init_redistributor();
    gic_init_cpu_interface();
}

/* ===================================================================== */
/* IRQ management */
/* ===================================================================== */

void gic_enable_irq(uint32_t irq)
{
    if (irq >= GIC_MAX_IRQS) {
        return;
    }
    
    if (irq < GIC_SPI_START) {
        /* SGI/PPI - use redistributor */
        uint64_t sgi_base = GICR_BASE + GICR_SGI_BASE;
        *(volatile uint32_t *)(sgi_base + GICR_ISENABLER0) = (1 << irq);
    } else {
        /* SPI - use distributor */
        uint32_t reg = irq / 32;
        uint32_t bit = irq % 32;
        gicd_write(GICD_ISENABLER + reg * 4, (1 << bit));
    }
}

void gic_disable_irq(uint32_t irq)
{
    if (irq >= GIC_MAX_IRQS) {
        return;
    }
    
    if (irq < GIC_SPI_START) {
        uint64_t sgi_base = GICR_BASE + GICR_SGI_BASE;
        *(volatile uint32_t *)(sgi_base + GICR_ICENABLER0) = (1 << irq);
    } else {
        uint32_t reg = irq / 32;
        uint32_t bit = irq % 32;
        gicd_write(GICD_ICENABLER + reg * 4, (1 << bit));
    }
}

void gic_set_priority(uint32_t irq, uint8_t priority)
{
    if (irq >= GIC_MAX_IRQS) {
        return;
    }
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t mask = 0xFF << shift;
    
    if (irq < GIC_SPI_START) {
        uint64_t sgi_base = GICR_BASE + GICR_SGI_BASE;
        uint32_t val = *(volatile uint32_t *)(sgi_base + GICR_IPRIORITYR + reg * 4);
        val = (val & ~mask) | (priority << shift);
        *(volatile uint32_t *)(sgi_base + GICR_IPRIORITYR + reg * 4) = val;
    } else {
        uint32_t val = gicd_read(GICD_IPRIORITYR + reg * 4);
        val = (val & ~mask) | (priority << shift);
        gicd_write(GICD_IPRIORITYR + reg * 4, val);
    }
}

uint32_t gic_acknowledge(void)
{
    return gic_read_icc_iar1();
}

void gic_end_interrupt(uint32_t irq)
{
    gic_write_icc_eoir1(irq);
}

int gic_register_handler(uint32_t irq, irq_handler_t handler, void *data)
{
    if (irq >= GIC_MAX_IRQS) {
        return -1;
    }
    
    irq_table[irq].handler = handler;
    irq_table[irq].data = data;
    
    return 0;
}

void gic_send_sgi(uint32_t cpu_mask, uint32_t irq)
{
    if (irq > 15) {
        return;
    }
    
    /* Generate SGI using ICC_SGI1R_EL1 */
    uint64_t val = ((uint64_t)cpu_mask & 0xFFFF) | ((uint64_t)irq << 24);
    asm volatile("msr S3_0_C12_C11_5, %0" : : "r" (val));
    asm volatile("isb");
}

/* ===================================================================== */
/* IRQ dispatch - called from exception handler */
/* ===================================================================== */

void handle_irq(void *regs)
{
    (void)regs;
    
    /* Acknowledge interrupt */
    uint32_t irq = gic_acknowledge();
    
    /* Check for spurious interrupt */
    if (irq >= 1020) {
        return;
    }
    
    /* Call registered handler */
    if (irq < GIC_MAX_IRQS && irq_table[irq].handler) {
        irq_table[irq].handler(irq, irq_table[irq].data);
    } else {
        printk(KERN_WARNING "GIC: Unhandled IRQ %u\n", irq);
    }
    
    /* End of interrupt */
    gic_end_interrupt(irq);
}
