/*
 * VibeOS Userspace Linker Script
 *
 * Programs are position-independent (PIE) - they can load anywhere.
 * Base is 0x0, kernel adds actual load address at runtime.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* R-X */
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = 0x0;  /* Base at 0 - kernel decides actual load address */

    .text : {
        *(.text.start)
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    .data : {
        *(.data*)
    } :text

    .got : {
        *(.got)
        *(.got.plt)
    } :text

    .dynamic : {
        *(.dynamic)
    } :text :dynamic

    .bss : {
        *(.bss*)
        *(COMMON)
    } :text

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.*)
    }
}
