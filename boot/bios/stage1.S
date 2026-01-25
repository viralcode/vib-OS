/*
 * Vib-OS BIOS Bootloader - Stage 1
 * 
 * This is the Master Boot Record (MBR) that loads stage 2
 * Loaded by BIOS at 0x7C00
 */

.code16
.section .text
.global _start

_start:
    /* ================================================================= */
    /* Set up segments and stack */
    /* ================================================================= */
    cli
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x7C00, %sp
    sti
    
    /* ================================================================= */
    /* Print boot message */
    /* ================================================================= */
    mov     $msg_boot, %si
    call    print_string
    
    /* ================================================================= */
    /* Load stage 2 from disk */
    /* ================================================================= */
    /* Stage 2 is located at sector 2 (after MBR) */
    /* Load 16 sectors (8KB) to 0x7E00 */
    
    mov     $0x02, %ah          # BIOS read sectors function
    mov     $16, %al            # Number of sectors to read
    mov     $0x0000, %ch        # Cylinder 0
    mov     $0x02, %cl          # Start from sector 2
    mov     $0x00, %dh          # Head 0
    # DL already contains drive number from BIOS
    mov     $0x7E00, %bx        # Load to 0x7E00
    int     $0x13
    
    jc      disk_error
    
    /* ================================================================= */
    /* Jump to stage 2 */
    /* ================================================================= */
    mov     $msg_stage2, %si
    call    print_string
    
    jmp     0x7E00
    
/* ===================================================================== */
/* Error Handling */
/* ===================================================================== */

disk_error:
    mov     $msg_error, %si
    call    print_string
    jmp     halt

halt:
    cli
    hlt
    jmp     halt

/* ===================================================================== */
/* Print String Function */
/* Input: SI = pointer to null-terminated string */
/* ===================================================================== */

print_string:
    push    %ax
    push    %bx
    
.print_loop:
    lodsb                       # Load byte from SI into AL
    test    %al, %al            # Check if null terminator
    jz      .print_done
    
    mov     $0x0E, %ah          # BIOS teletype output
    mov     $0x07, %bx          # Page 0, light gray
    int     $0x10               # BIOS video interrupt
    
    jmp     .print_loop
    
.print_done:
    pop     %bx
    pop     %ax
    ret

/* ===================================================================== */
/* Data */
/* ===================================================================== */

msg_boot:
    .asciz  "Vib-OS Stage 1\r\n"

msg_stage2:
    .asciz  "Loading Stage 2...\r\n"

msg_error:
    .asciz  "Disk read error!\r\n"

/* ===================================================================== */
/* Boot Signature */
/* ===================================================================== */

.org 510
.word 0xAA55
