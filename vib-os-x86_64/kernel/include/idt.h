/*
 * Minimal IDT/PIC interface
 */

#ifndef _IDT_H
#define _IDT_H

#include "types.h"

typedef struct interrupt_frame {
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} interrupt_frame_t;

typedef void (*isr_handler_t)(interrupt_frame_t *frame);

void idt_init(void);
void idt_register_handler(uint8_t vector, isr_handler_t handler);

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);

#endif /* _IDT_H */
