/*
 * x86 PIC (Programmable Interrupt Controller) Driver
 * 8259A PIC for x86 32-bit
 */

#include "types.h"

/* PIC ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC commands */
#define PIC_EOI         0x20    /* End of Interrupt */
#define ICW1_INIT       0x10
#define ICW1_ICW4       0x01
#define ICW4_8086       0x01

/* I/O port operations */
static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void)
{
    /* Port 0x80 is used for 'checkpoints' during POST */
    /* Writing to it causes a small delay */
    outb(0x80, 0);
}

void pic_init(void)
{
    /* Save masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    
    /* Start initialization sequence */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    /* Set vector offsets */
    outb(PIC1_DATA, 0x20);  /* Master PIC: IRQ 0-7 -> INT 0x20-0x27 */
    io_wait();
    outb(PIC2_DATA, 0x28);  /* Slave PIC: IRQ 8-15 -> INT 0x28-0x2F */
    io_wait();
    
    /* Tell Master PIC there's a slave at IRQ2 */
    outb(PIC1_DATA, 0x04);
    io_wait();
    /* Tell Slave PIC its cascade identity */
    outb(PIC2_DATA, 0x02);
    io_wait();
    
    /* Set 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    /* Restore masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_clear_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
