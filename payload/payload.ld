OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

SECTIONS
{
  . = 0xac800000;
  .text : ALIGN(4)
  {
    *(.text.start)
    *(.text*)
    *(.rodata*)
  }
  .data : ALIGN(4) { *(.data*) }
  .bss : ALIGN(4) { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
