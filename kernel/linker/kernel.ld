ENTRY(_start)

PHDRS {
    load PT_LOAD FLAGS(7);
}

SECTIONS {
    . = 0x401000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :load

    .rodata : {
        *(.rodata*)
    } :load

    .data : {
        *(.data*)
    } :load

    .bss : {
        *(.bss*)
        *(COMMON)
    } :load
}
