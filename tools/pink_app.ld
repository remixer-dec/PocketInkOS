ENTRY(pinkEntry)

SECTIONS
{
  . = 0;

  .text : ALIGN(4)
  {
    *(.pink_entry.literal .pink_entry.literal.*)
    KEEP(*(.pink_entry))
    *(.literal .literal.*)
    *(.text .text.*)
  }

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4)
  {
    *(.data .data.*)
  }

  .bss : ALIGN(4)
  {
    *(.bss .bss.*)
    *(COMMON)
  }

  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
    *(.note .note.*)
    *(.xtensa.info)
  }
}
