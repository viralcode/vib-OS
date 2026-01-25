/*
 * Vib-OS BIOS Bootloader - Stage 2
 * 
 * Loaded at 0x7E00 by stage 1
 * Switches to protected mode and loads the kernel
 */

.code16
.section .text
.global _start

_start:
    /* ================================================================= */
    /* Print stage 2 message */
    /* ================================================================= */
    mov     $msg_stage2, %si
    call    print_string
    
    /* ================================================================= */
    /* Enable A20 line */
    /* ================================================================= */
    call    enable_a20
    
    /* ================================================================= */
    /* Load GDT */
    /* ================================================================= */
    lgdt    gdt_descriptor
    
    /* ================================================================= */
    /* Switch to protected mode */
    /* ================================================================= */
    mov     %cr0, %eax
    or      $1, %eax
    mov     %eax, %cr0
    
    /* Far jump to flush pipeline and load CS */
    ljmp    $0x08, $protected_mode

/* ===================================================================== */
/* Enable A20 Line */
/* ===================================================================== */

enable_a20:
    /* Try BIOS method first */
    mov     $0x2401, %ax
    int     $0x15
    jnc     .a20_done
    
    /* Try keyboard controller method */
    call    .a20_wait
    mov     $0xAD, %al          # Disable keyboard
    out     %al, $0x64
    
    call    .a20_wait
    mov     $0xD0, %al          # Read output port
    out     %al, $0x64
    
    call    .a20_wait2
    in      $0x60, %al
    push    %ax
    
    call    .a20_wait
    mov     $0xD1, %al          # Write output port
    out     %al, $0x64
    
    call    .a20_wait
    pop     %ax
    or      $2, %al             # Set A20 bit
    out     %al, $0x60
    
    call    .a20_wait
    mov     $0xAE, %al          # Enable keyboard
    out     %al, $0x64
    
    call    .a20_wait
    
.a20_done:
    ret

.a20_wait:
    in      $0x64, %al
    test    $2, %al
    jnz     .a20_wait
    ret

.a20_wait2:
    in      $0x64, %al
    test    $1, %al
    jz      .a20_wait2
    ret

/* ===================================================================== */
/* Print String (16-bit real mode) */
/* ===================================================================== */

print_string:
    push    %ax
    push    %bx
    
.print_loop:
    lodsb
    test    %al, %al
    jz      .print_done
    
    mov     $0x0E, %ah
    mov     $0x07, %bx
    int     $0x10
    
    jmp     .print_loop
    
.print_done:
    pop     %bx
    pop     %ax
    ret

/* ===================================================================== */
/* 32-bit Protected Mode Code */
/* ===================================================================== */

.code32
protected_mode:
    /* Set up segments */
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss
    
    /* Set up stack */
    mov     $0x90000, %esp
    
    /* Print protected mode message */
    mov     $msg_protected, %esi
    call    print_string_32
    
    /* TODO: Load kernel from disk */
    /* For now, halt */
    cli
    hlt
    jmp     .-2

/* Print string in 32-bit mode (VGA text mode) */
print_string_32:
    push    %eax
    push    %ebx
    mov     $0xB8000, %ebx      # VGA text buffer
    mov     $0x0F, %ah          # White on black
    
.print_loop_32:
    lodsb
    test    %al, %al
    jz      .print_done_32
    
    mov     %ax, (%ebx)
    add     $2, %ebx
    
    jmp     .print_loop_32
    
.print_done_32:
    pop     %ebx
    pop     %eax
    ret

/* ===================================================================== */
/* Global Descriptor Table */
/* ===================================================================== */

.align 16
gdt_start:
    /* Null descriptor */
    .quad   0x0000000000000000
    
    /* Code segment (0x08) */
    .word   0xFFFF              # Limit 0-15
    .word   0x0000              # Base 0-15
    .byte   0x00                # Base 16-23
    .byte   0x9A                # Access: present, ring 0, code, executable, readable
    .byte   0xCF                # Flags: 4KB granularity, 32-bit
    .byte   0x00                # Base 24-31
    
    /* Data segment (0x10) */
    .word   0xFFFF              # Limit 0-15
    .word   0x0000              # Base 0-15
    .byte   0x00                # Base 16-23
    .byte   0x92                # Access: present, ring 0, data, writable
    .byte   0xCF                # Flags: 4KB granularity, 32-bit
    .byte   0x00                # Base 24-31

gdt_end:

gdt_descriptor:
    .word   gdt_end - gdt_start - 1
    .long   gdt_start

/* ===================================================================== */
/* Data */
/* ===================================================================== */

msg_stage2:
    .asciz  "Vib-OS Stage 2\r\n"

msg_protected:
    .asciz  "Protected Mode OK"
