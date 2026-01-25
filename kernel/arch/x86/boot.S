/*
 * x86 32-bit Boot Code
 * Entry point from bootloader
 */

.section .text
.code32

.global _start
.type _start, @function

_start:
    /* Disable interrupts */
    cli
    
    /* Set up stack */
    movl $stack_top, %esp
    movl %esp, %ebp
    
    /* Clear direction flag */
    cld
    
    /* Save multiboot info (if any) */
    pushl %ebx  /* Multiboot info pointer */
    pushl %eax  /* Multiboot magic */
    
    /* Set up GDT */
    lgdt gdt_descriptor
    
    /* Reload segment registers */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    /* Jump to reload CS */
    ljmp $0x08, $reload_cs
    
reload_cs:
    /* Set up IDT */
    lidt idt_descriptor
    
    /* Enable paging (will be done in C code) */
    
    /* Call kernel main */
    call kernel_main
    
    /* Halt if kernel returns */
halt:
    cli
    hlt
    jmp halt

/* ===================================================================== */
/* GDT (Global Descriptor Table) */
/* ===================================================================== */

.align 16
gdt_start:
    /* Null descriptor */
    .quad 0x0000000000000000
    
    /* Code segment (0x08) */
    .word 0xFFFF        /* Limit low */
    .word 0x0000        /* Base low */
    .byte 0x00          /* Base middle */
    .byte 0x9A          /* Access: present, ring 0, code, executable, readable */
    .byte 0xCF          /* Flags: 4KB granularity, 32-bit */
    .byte 0x00          /* Base high */
    
    /* Data segment (0x10) */
    .word 0xFFFF        /* Limit low */
    .word 0x0000        /* Base low */
    .byte 0x00          /* Base middle */
    .byte 0x92          /* Access: present, ring 0, data, writable */
    .byte 0xCF          /* Flags: 4KB granularity, 32-bit */
    .byte 0x00          /* Base high */
    
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1   /* Size */
    .long gdt_start                  /* Offset */

/* ===================================================================== */
/* IDT (Interrupt Descriptor Table) - initially empty */
/* ===================================================================== */

.align 16
idt_start:
    .fill 256, 8, 0     /* 256 entries, 8 bytes each, filled with 0 */
idt_end:

idt_descriptor:
    .word idt_end - idt_start - 1   /* Size */
    .long idt_start                  /* Offset */

/* ===================================================================== */
/* Stack */
/* ===================================================================== */

.section .bss
.align 16
stack_bottom:
    .skip 16384  /* 16KB stack */
stack_top:
