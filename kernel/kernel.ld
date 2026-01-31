/* kernel.ld - simple linker script for a flat kernel image */
ENTRY(_start)
SECTIONS
{
  . = 0x0;
  .text : {
    KEEP(*(.text.startup))
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
