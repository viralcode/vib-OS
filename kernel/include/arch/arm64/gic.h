/*
 * UnixOS Kernel - GIC (Generic Interrupt Controller) Header
 * 
 * ARM GICv3 support for Apple Silicon and QEMU virt.
 */

#ifndef _ARCH_ARM64_GIC_H
#define _ARCH_ARM64_GIC_H

#include "types.h"

/* ===================================================================== */
/* GICv3 Constants */
/* ===================================================================== */

/* Interrupt types */
#define GIC_SPI_START       32      /* Shared Peripheral Interrupts */
#define GIC_PPI_START       16      /* Private Peripheral Interrupts */
#define GIC_SGI_START       0       /* Software Generated Interrupts */

/* Maximum interrupts */
#define GIC_MAX_IRQS        1020

/* Interrupt priorities */
#define GIC_PRIO_LOWEST     0xF0
#define GIC_PRIO_HIGHEST    0x00
#define GIC_PRIO_DEFAULT    0x80

/* ===================================================================== */
/* GIC Distributor registers (GICD) */
/* ===================================================================== */

#define GICD_CTLR           0x0000  /* Distributor Control */
#define GICD_TYPER          0x0004  /* Interrupt Controller Type */
#define GICD_IIDR           0x0008  /* Implementer Identification */
#define GICD_IGROUPR        0x0080  /* Interrupt Group (banked) */
#define GICD_ISENABLER      0x0100  /* Interrupt Set-Enable */
#define GICD_ICENABLER      0x0180  /* Interrupt Clear-Enable */
#define GICD_ISPENDR        0x0200  /* Interrupt Set-Pending */
#define GICD_ICPENDR        0x0280  /* Interrupt Clear-Pending */
#define GICD_ISACTIVER      0x0300  /* Interrupt Set-Active */
#define GICD_ICACTIVER      0x0380  /* Interrupt Clear-Active */
#define GICD_IPRIORITYR     0x0400  /* Interrupt Priority */
#define GICD_ITARGETSR      0x0800  /* Interrupt Processor Targets */
#define GICD_ICFGR          0x0C00  /* Interrupt Configuration */
#define GICD_IROUTER        0x6000  /* Interrupt Routing (GICv3) */

/* GICD_CTLR bits */
#define GICD_CTLR_ENABLE_G0     (1 << 0)
#define GICD_CTLR_ENABLE_G1NS   (1 << 1)
#define GICD_CTLR_ENABLE_G1S    (1 << 2)
#define GICD_CTLR_ARE_S         (1 << 4)
#define GICD_CTLR_ARE_NS        (1 << 5)
#define GICD_CTLR_DS            (1 << 6)

/* ===================================================================== */
/* GIC Redistributor registers (GICR) - GICv3 */
/* ===================================================================== */

#define GICR_CTLR           0x0000
#define GICR_IIDR           0x0004
#define GICR_TYPER          0x0008
#define GICR_WAKER          0x0014
#define GICR_IGROUPR0       0x0080
#define GICR_ISENABLER0     0x0100
#define GICR_ICENABLER0     0x0180
#define GICR_IPRIORITYR     0x0400
#define GICR_ICFGR0         0x0C00
#define GICR_ICFGR1         0x0C04

/* GICR_WAKER bits */
#define GICR_WAKER_PROCESSOR_SLEEP  (1 << 1)
#define GICR_WAKER_CHILDREN_ASLEEP  (1 << 2)

/* Redistributor SGI base offset */
#define GICR_SGI_BASE       0x10000

/* ===================================================================== */
/* GIC CPU Interface (system registers for GICv3) */
/* ===================================================================== */

/* System register access via MRS/MSR */
#define ICC_IAR0_EL1        "S3_0_C12_C8_0"     /* Interrupt Acknowledge (Group 0) */
#define ICC_IAR1_EL1        "S3_0_C12_C12_0"    /* Interrupt Acknowledge (Group 1) */
#define ICC_EOIR0_EL1       "S3_0_C12_C8_1"     /* End of Interrupt (Group 0) */
#define ICC_EOIR1_EL1       "S3_0_C12_C12_1"    /* End of Interrupt (Group 1) */
#define ICC_PMR_EL1         "S3_0_C4_C6_0"      /* Priority Mask Register */
#define ICC_BPR0_EL1        "S3_0_C12_C8_3"     /* Binary Point (Group 0) */
#define ICC_BPR1_EL1        "S3_0_C12_C12_3"    /* Binary Point (Group 1) */
#define ICC_CTLR_EL1        "S3_0_C12_C12_4"    /* Control Register */
#define ICC_SRE_EL1         "S3_0_C12_C12_5"    /* System Register Enable */
#define ICC_IGRPEN0_EL1     "S3_0_C12_C12_6"    /* Interrupt Group 0 Enable */
#define ICC_IGRPEN1_EL1     "S3_0_C12_C12_7"    /* Interrupt Group 1 Enable */

/* ===================================================================== */
/* Interrupt handler callback type */
/* ===================================================================== */

typedef void (*irq_handler_t)(uint32_t irq, void *data);

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * gic_init - Initialize the Generic Interrupt Controller
 */
void gic_init(void);

/**
 * gic_cpu_init - Initialize GIC for secondary CPUs (SMP)
 */
void gic_cpu_init(void);

/**
 * gic_enable_irq - Enable an interrupt
 * @irq: Interrupt number
 */
void gic_enable_irq(uint32_t irq);

/**
 * gic_disable_irq - Disable an interrupt
 * @irq: Interrupt number
 */
void gic_disable_irq(uint32_t irq);

/**
 * gic_set_priority - Set interrupt priority
 * @irq: Interrupt number
 * @priority: Priority (0=highest, 0xFF=lowest)
 */
void gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * gic_acknowledge - Acknowledge interrupt (read IAR)
 * 
 * Return: Interrupt ID
 */
uint32_t gic_acknowledge(void);

/**
 * gic_end_interrupt - Signal end of interrupt (write EOIR)
 * @irq: Interrupt number
 */
void gic_end_interrupt(uint32_t irq);

/**
 * gic_register_handler - Register interrupt handler
 * @irq: Interrupt number
 * @handler: Handler function
 * @data: Data to pass to handler
 * 
 * Return: 0 on success, negative on error
 */
int gic_register_handler(uint32_t irq, irq_handler_t handler, void *data);

/**
 * gic_send_sgi - Send Software Generated Interrupt
 * @cpu_mask: Target CPUs
 * @irq: SGI number (0-15)
 */
void gic_send_sgi(uint32_t cpu_mask, uint32_t irq);

#endif /* _ARCH_ARM64_GIC_H */
