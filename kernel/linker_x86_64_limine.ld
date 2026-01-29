/*
 * Vib-OS Kernel Linker Script for Limine bootloader
 * Higher-half kernel for x86_64
 */

/* Virtual base at -2GB (canonical higher half) */
KERNEL_VIRT = 0xFFFFFFFF80000000;
KERNEL_PHYS = 0x100000;  /* 1MB physical load address */

ENTRY(_start)

PHDRS {
    limine   PT_LOAD FLAGS(4);  /* r-- (read-only requests) */
    text     PT_LOAD FLAGS(5);  /* r-x (code) */
    rodata   PT_LOAD FLAGS(4);  /* r-- (read-only data) */
    data     PT_LOAD FLAGS(6);  /* rw- (read-write data) */
}

SECTIONS {
    . = KERNEL_VIRT;
    __kernel_start = .;

    /* Limine requests section - must be first for bootloader scanning */
    .limine_requests ALIGN(4K) : AT(ADDR(.limine_requests) - KERNEL_VIRT + KERNEL_PHYS) {
        KEEP(*(.limine_requests))
    } :limine

    /* Code section */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT + KERNEL_PHYS) {
        *(.text .text.*)
    } :text

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT + KERNEL_PHYS) {
        *(.rodata .rodata.*)
    } :rodata

    /* Data section */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT + KERNEL_PHYS) {
        *(.data .data.*)
    } :data

    /* BSS section (uninitialized data) */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRT + KERNEL_PHYS) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    /* Stack */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 64K;  /* 64KB kernel stack */
    __stack_top = .;

    . = ALIGN(4K);
    __kernel_end = .;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
