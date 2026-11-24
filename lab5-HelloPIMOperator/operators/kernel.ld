ENTRY(_start)
SECTIONS {
    . = 0;
    .text : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) *(.srodata*) }
    .data : { *(.sdata*) *(.data*) }
    .bss : { *(.sbss*) *(.bss*) *(COMMON) }
    ASSERT(. < 0xe000, "firmware overlaps the stack")
}
