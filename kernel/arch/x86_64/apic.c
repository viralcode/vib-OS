/*
 * Vib-OS Kernel - x86_64 APIC (Advanced Programmable Interrupt Controller)
 * 
 * Supports Local APIC and I/O APIC
 */

#include "arch/arch.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* Local APIC Registers */
/* ===================================================================== */

#define LAPIC_BASE      0xFEE00000

#define LAPIC_ID        0x020
#define LAPIC_VER       0x030
#define LAPIC_TPR       0x080   /* Task Priority Register */
#define LAPIC_APR       0x090   /* Arbitration Priority Register */
#define LAPIC_PPR       0x0A0   /* Processor Priority Register */
#define LAPIC_EOI       0x0B0   /* End Of Interrupt */
#define LAPIC_RRD       0x0C0   /* Remote Read Register */
#define LAPIC_LDR       0x0D0   /* Logical Destination Register */
#define LAPIC_DFR       0x0E0   /* Destination Format Register */
#define LAPIC_SVR       0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR       0x100   /* In-Service Register */
#define LAPIC_TMR       0x180   /* Trigger Mode Register */
#define LAPIC_IRR       0x200   /* Interrupt Request Register */
#define LAPIC_ESR       0x280   /* Error Status Register */
#define LAPIC_ICRLO     0x300   /* Interrupt Command Register (low) */
#define LAPIC_ICRHI     0x310   /* Interrupt Command Register (high) */
#define LAPIC_TIMER     0x320   /* Timer Local Vector Table Entry */
#define LAPIC_THERMAL   0x330   /* Thermal Local Vector Table Entry */
#define LAPIC_PERF      0x340   /* Performance Counter LVT */
#define LAPIC_LINT0     0x350   /* Local Interrupt 0 LVT */
#define LAPIC_LINT1     0x360   /* Local Interrupt 1 LVT */
#define LAPIC_ERROR     0x370   /* Error LVT */
#define LAPIC_TICR      0x380   /* Timer Initial Count Register */
#define LAPIC_TCCR      0x390   /* Timer Current Count Register */
#define LAPIC_TDCR      0x3E0   /* Timer Divide Configuration Register */

/* SVR bits */
#define LAPIC_SVR_ENABLE    0x100

/* Timer bits */
#define LAPIC_TIMER_PERIODIC    0x20000
#define LAPIC_TIMER_MASKED      0x10000

/* ===================================================================== */
/* I/O APIC Registers */
/* ===================================================================== */

#define IOAPIC_BASE     0xFEC00000

#define IOAPIC_REG_ID       0x00
#define IOAPIC_REG_VER      0x01
#define IOAPIC_REG_ARB      0x02
#define IOAPIC_REG_REDTBL   0x10

/* ===================================================================== */
/* Static Data */
/* ===================================================================== */

static volatile uint32_t *lapic = (volatile uint32_t *)LAPIC_BASE;
static volatile uint32_t *ioapic = (volatile uint32_t *)IOAPIC_BASE;

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static inline uint32_t lapic_read(uint32_t reg)
{
    return lapic[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
    lapic[reg / 4] = value;
}

static inline uint32_t ioapic_read(uint8_t reg)
{
    ioapic[0] = reg;
    return ioapic[4];
}

static inline void ioapic_write(uint8_t reg, uint32_t value)
{
    ioapic[0] = reg;
    ioapic[4] = value;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void apic_init(void)
{
    printk(KERN_INFO "APIC: Initializing Local APIC\n");
    
    /* Enable Local APIC */
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE | 0xFF);
    
    /* Clear task priority to enable all interrupts */
    lapic_write(LAPIC_TPR, 0);
    
    /* Set up timer (periodic mode, IRQ 32) */
    lapic_write(LAPIC_TDCR, 0x03);  /* Divide by 16 */
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_PERIODIC | 32);
    lapic_write(LAPIC_TICR, 10000000);  /* Initial count */
    
    /* Mask all other LVT entries */
    lapic_write(LAPIC_LINT0, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_LINT1, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_ERROR, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_PERF, LAPIC_TIMER_MASKED);
    
    printk(KERN_INFO "APIC: Local APIC initialized\n");
    
    /* Initialize I/O APIC if present */
    /* TODO: Parse ACPI MADT to find I/O APIC address */
    
    printk(KERN_INFO "APIC: Initialization complete\n");
}

void apic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

/* ===================================================================== */
/* I/O APIC Configuration */
/* ===================================================================== */

void ioapic_set_irq(uint8_t irq, uint8_t vector, bool enable)
{
    uint32_t low = vector;
    uint32_t high = 0;
    
    if (!enable) {
        low |= (1 << 16);  /* Masked */
    }
    
    ioapic_write(IOAPIC_REG_REDTBL + irq * 2, low);
    ioapic_write(IOAPIC_REG_REDTBL + irq * 2 + 1, high);
}
