OUTPUT_FORMAT("elf32-shbig-linux")
OUTPUT_ARCH(sh)
ENTRY(_start)

PHDRS
{
	load PT_LOAD FILEHDR PHDRS FLAGS(7);
	stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
	. = 0x00400000 + SIZEOF_HEADERS;
	.text :
	{
		*(.text.start)
		*(.text .text.*)
		*(.rodata .rodata.*)
	} :load

	. = ALIGN(4);
	.data :
	{
		*(.data .data.*)
		*(.bss .bss.* COMMON)
	} :load

	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
	}
}
