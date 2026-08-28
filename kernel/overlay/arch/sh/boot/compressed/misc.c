// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/boot/compressed/misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SH by Stuart Menefy, Aug 1999
 *
 * Modified to use standard LinuxSH BIOS by Greg Banks 7Jul2000
 */

#include <linux/uaccess.h>
#include <asm/addrspace.h>
#include <asm/page.h>

/*
 * gzip declarations
 */

#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)     memset ((s), 0, (n))

/* cache.c */
#define CACHE_ENABLE      0
#define CACHE_DISABLE     1
int cache_control(unsigned int command);

extern char input_data[];
extern int input_len;
static unsigned char *output;

/*
 * The EX-word loader starts each zImage through the P2 uncached alias.  The
 * decompressor consequently writes the kernel through P2 and then jumps to
 * the corresponding P1 cached address.  SH-4A does not make that transition
 * instruction-cache coherent automatically: an older kernel's P1 cache lines
 * can survive a second launch and supply stale literals or branch targets.
 *
 * Linux's generic compressed boot path never had to account for a firmware
 * add-in repeatedly launching different kernels without resetting the CPU.
 * Invalidate every 32-byte P1 line that the gzip stream produced before the
 * handoff.  The gzip trailer's ISIZE field is checked as part of successful
 * decompression and gives the exact uncompressed byte count.
 */
static void sync_decompressed_kernel_icache(unsigned long p2_start,
					     unsigned long size)
{
#ifdef CONFIG_CPU_SH4A
	unsigned long address = P1SEGADDR(p2_start) & ~31UL;
	unsigned long end = (P1SEGADDR(p2_start) + size + 31UL) & ~31UL;

	__asm__ volatile("synco" ::: "memory");
	while (address < end) {
		__asm__ volatile("icbi @%0" : : "r" (address) : "memory");
		address += 32;
	}
	__asm__ volatile("synco" ::: "memory");
#endif
}

static unsigned long gzip_uncompressed_size(void)
{
	const unsigned char *trailer =
		(const unsigned char *)input_data + input_len - 4;

	return (unsigned long)trailer[0] |
	       ((unsigned long)trailer[1] << 8) |
	       ((unsigned long)trailer[2] << 16) |
	       ((unsigned long)trailer[3] << 24);
}

static void error(char *m);

#ifdef CONFIG_SH_EXWORD
static void exword_marker(unsigned short color)
{
	volatile unsigned short *vram = (volatile unsigned short *)0xac200000;
	volatile unsigned short *lcdc = (volatile unsigned short *)0xb4000000;
	volatile unsigned char *portr = (volatile unsigned char *)0xa405013c;
	volatile unsigned long *sar3 = (volatile unsigned long *)0xfe008050;
	volatile unsigned long *dar3 = (volatile unsigned long *)0xfe008054;
	volatile unsigned long *tcr3 = (volatile unsigned long *)0xfe008058;
	volatile unsigned long *chcr3 = (volatile unsigned long *)0xfe00805c;
	volatile unsigned short *dmaor = (volatile unsigned short *)0xfe008060;
	unsigned int i;
	unsigned int timeout = 1000000;

	for (i = 0; i < 528 * 12; ++i)
		vram[i] = color;
	*portr &= 0xef;
	__asm__ volatile("synco" ::: "memory");
	*lcdc = 0x2c;
	__asm__ volatile("synco" ::: "memory");
	*portr |= 0x10;
	__asm__ volatile("synco" ::: "memory");
	*tcr3 = (528 * 320) >> 4;
	*sar3 = 0x0c200000;
	*dar3 = 0x14000000;
	*dmaor &= 0xfffe;
	*chcr3 = 0x40101401;
	*dmaor |= 1;
	while (!(*chcr3 & 2) && --timeout)
		;
}
#else
static inline void exword_marker(unsigned short color) { }
#endif

int puts(const char *);

extern int _text;		/* Defined in vmlinux.lds.S */
extern int _end;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

/* Only the selected bzip2 decompressor needs the 4 MiB workspace.  Testing
 * HAVE_KERNEL_BZIP2 reserves it even for gzip and cannot fit in this board's
 * 16 MiB RAM.  The contiguous-input gzip path uses less than 10 KiB here.
 */
#ifdef CONFIG_KERNEL_BZIP2
#define HEAP_SIZE	0x400000
#else
#define HEAP_SIZE	0x10000
#endif

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#include "../../../../lib/decompress_unxz.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

int puts(const char *s)
{
	/* This should be updated to use the sh-sci routines */
	return 0;
}

void* memset(void* s, int c, size_t n)
{
	int i;
	char *ss = (char*)s;

	for (i=0;i<n;i++) ss[i] = c;
	return s;
}

void* memcpy(void* __dest, __const void* __src,
			    size_t __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
	return __dest;
}

static void error(char *x)
{
	exword_marker(0xf800);
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

const unsigned long __stack_chk_guard = 0x000a0dff;

void __stack_chk_fail(void)
{
	error("stack-protector: Kernel stack is corrupted\n");
}

/* Needed because vmlinux.lds.h references this */
void ftrace_stub(void)
{
}
void arch_ftrace_ops_list_func(void)
{
}

#define stackalign	4

#define STACK_SIZE (4096)
long __attribute__ ((aligned(stackalign))) user_stack[STACK_SIZE];
long *stack_start = &user_stack[STACK_SIZE];

void decompress_kernel(void)
{
	unsigned long output_addr;

	output_addr = __pa((unsigned long)&_text+PAGE_SIZE);
#if defined(CONFIG_29BIT)
	output_addr |= P2SEG;
#endif

	output = (unsigned char *)output_addr;
	free_mem_ptr = (unsigned long)&_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	exword_marker(0x07ff);
	puts("Uncompressing Linux... ");
	cache_control(CACHE_ENABLE);
	__decompress(input_data, input_len, NULL, NULL, output, 0, NULL, error);
	cache_control(CACHE_DISABLE);
	exword_marker(0xffe0);
	sync_decompressed_kernel_icache(output_addr,
					gzip_uncompressed_size());
	exword_marker(0x07e0);
	puts("Ok, booting the kernel.\n");
}
