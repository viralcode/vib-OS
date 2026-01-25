/*
 * Vib-OS Kernel Linker Script
 * x86 (32-bit) kernel linking configuration
 */

/* Entry point */
ENTRY(_start)

/* Kernel base address */
KERNEL_BASE = 0xC0000000;  /* 3GB virtual address */
KERNEL_PHYS = 0x100000;    /* 1MB physical address */

SECTIONS
{
    /* Start at 1MB physical address */
    . = KERNEL_PHYS;
    
    /* Keep track of kernel start */
    __kernel_start = .;
    
    /* =================================================================== */
    /* Code Section (.text) */
    /* =================================================================== */
    .text ALIGN(4K) : {
        __text_start = .;
        
        /* Boot code must come first */
        *(.text.boot)
        
        /* Then the rest of the kernel code */
        *(.text .text.*)
        
        __text_end = .;
    }
    
    /* =================================================================== */
    /* Read-Only Data Section (.rodata) */
    /* =================================================================== */
    .rodata ALIGN(4K) : {
        __rodata_start = .;
        
        *(.rodata .rodata.*)
        
        __rodata_end = .;
    }
    
    /* =================================================================== */
    /* Data Section (.data) */
    /* =================================================================== */
    .data ALIGN(4K) : {
        __data_start = .;
        
        *(.data .data.*)
        
        __data_end = .;
    }
    
    /* =================================================================== */
    /* BSS Section (Uninitialized Data) */
    /* =================================================================== */
    .bss ALIGN(4K) : {
        __bss_start = .;
        
        *(.bss .bss.*)
        *(COMMON)
        
        __bss_end = .;
    }
    
    /* =================================================================== */
    /* Kernel Stack */
    /* =================================================================== */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 64K;  /* 64KB kernel stack */
    __stack_top = .;
    
    /* =================================================================== */
    /* End of Kernel */
    /* =================================================================== */
    . = ALIGN(4K);
    __kernel_end = .;
    
    /* Calculate kernel size */
    __kernel_size = __kernel_end - __kernel_start;
    
    /* =================================================================== */
    /* Debug Sections (stripped in release) */
    /* =================================================================== */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
