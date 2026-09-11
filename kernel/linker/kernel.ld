ENTRY(_start)

PHDRS {
    load PT_LOAD FLAGS(7);
}

SECTIONS {
    . = 0x401000;
    __kernel_start = .;
    __text_start = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :load

    . = ALIGN(4096);
    __text_end = .;
    __rodata_start = .;

    .rodata : {
        *(.rodata*)
    } :load

    . = ALIGN(4096);
    __rodata_end = .;
    __data_start = .;

    .data : {
        *(.data*)
    } :load

    . = ALIGN(4096);
    __bss_start = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    } :load

    __kernel_end = .;
}
