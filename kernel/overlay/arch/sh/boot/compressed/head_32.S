/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/sh/boot/compressed/head.S
 *
 *  Copyright (C) 1999 Stuart Menefy
 *  Copyright (C) 2003 SUGIOKA Toshinobu
 */

.text

#include <asm/page.h>

	.global	startup
startup:
	/* Load initial status register */
	mov.l   init_sr, r1
	ldc     r1, sr

#ifdef CONFIG_SH_EXWORD
	/*
	 * Preserve the firmware-initialized SH7305 KEYSC state before any
	 * Linux initialization runs.  The destination is in the unused tail of
	 * the reserved 0xac200000 framebuffer allocation, beyond visible VRAM.
	 */
	mov.l	exword_snapshot_addr, r2
	mov.l	exword_snapshot_magic, r0
	mov.l	r0, @r2
	add	#4, r2
	mov.l	exword_keysc_addr, r1
	mov	#15, r3
.Lexword_snapshot_keysc:
	mov.w	@r1+, r0
	mov.w	r0, @r2
	add	#2, r2
	dt	r3
	bf	.Lexword_snapshot_keysc
	add	#2, r2			! align the following longword
	mov.l	exword_mstp0_addr, r1
	mov.l	@r1, r0
	mov.l	r0, @r2
#endif

	/* Move myself to proper location if necessary */
	mova	1f, r0
	mov.l	1f, r2
	cmp/eq	r2, r0
	bt	clear_bss
	sub	r0, r2
	mov.l	bss_start_addr, r0
	mov	#0xffffffe0, r1
	and	r1, r0			! align cache line
	mov.l	text_start_addr, r3
	mov	r0, r1
	sub	r2, r1
3:
	mov.l	@r1, r4
	mov.l	@(4,r1), r5
	mov.l	@(8,r1), r6
	mov.l	@(12,r1), r7
	mov.l	@(16,r1), r8
	mov.l	@(20,r1), r9
	mov.l	@(24,r1), r10
	mov.l	@(28,r1), r11
	mov.l	r4, @r0
	mov.l	r5, @(4,r0)
	mov.l	r6, @(8,r0)
	mov.l	r7, @(12,r0)
	mov.l	r8, @(16,r0)
	mov.l	r9, @(20,r0)
	mov.l	r10, @(24,r0)
	mov.l	r11, @(28,r0)
#ifdef CONFIG_CPU_SH4
	ocbwb	@r0
#endif
	cmp/hi	r3, r0
	add	#-32, r0
	bt/s	3b
	 add	#-32, r1
#ifdef CONFIG_CPU_SH4A
	/*
	 * The add-in starts us through P2.  The relocation loop above writes
	 * the linked P1 image and cleans its data-cache lines, but a previous
	 * launch can still have instructions cached at the same P1 addresses.
	 * Invalidate the entire relocated compressed head before jumping into
	 * it; otherwise even the main-kernel cache fix can itself be stale.
	 */
	synco
	mov.l	text_start_addr, r0
	mov.l	bss_start_addr, r1
4:
	icbi	@r0
	add	#32, r0
	cmp/hi	r0, r1
	bt	4b
	synco
#endif
	mov.l	2f, r0
	jmp	@r0
	 nop

	.align 2
1:	.long	1b
2:	.long	clear_bss
text_start_addr:
	.long	startup

	/* Clear BSS */
clear_bss:
	mov.l	end_addr, r1
	mov.l	bss_start_addr, r2
	mov	#0, r0
l1:
	mov.l	r0, @-r1
	cmp/eq	r1,r2
	bf	l1

	/* Set the initial pointer. */
	mov.l	init_stack_addr, r0
	mov.l	@r0, r15

	/* Decompress the kernel */
	mov.l	decompress_kernel_addr, r0
	jsr	@r0
	nop

	/* Jump to the start of the decompressed kernel */
	mov.l	kernel_start_addr, r0
	jmp	@r0
	nop
	
	.align	2
bss_start_addr:
	.long	__bss_start
end_addr:
	.long	_end
init_sr:
	.long	0x500000F0	/* Privileged mode, Bank=0, Block=1, IMASK=0xF */
#ifdef CONFIG_SH_EXWORD
exword_snapshot_addr:
	.long	0xac252c00
exword_snapshot_magic:
	.long	0x45584b53	/* "EXKS" */
exword_keysc_addr:
	.long	0xa44b0000
exword_mstp0_addr:
	.long	0xa4150030
#endif
kexec_magic:
	.long	0x400000F0	/* magic used by kexec to parse zImage format */
init_stack_addr:
	.long	stack_start
decompress_kernel_addr:
	.long	decompress_kernel
kernel_start_addr:
#ifdef CONFIG_32BIT
	.long	___pa(_text+PAGE_SIZE)
#else
	.long	_text+PAGE_SIZE
#endif

	.align	9
fake_headers_as_bzImage:
	.word	0
	.ascii	"HdrS"		! header signature
	.word	0x0202		! header version number (>= 0x0105)
				! or else old loadlin-1.5 will fail)
	.word	0		! default_switch
	.word	0		! SETUPSEG
	.word	0x1000
	.word	0		! pointing to kernel version string
	.byte	0		! = 0, old one (LILO, Loadlin,
				! 0xTV: T=0 for LILO
				!       V = version
	.byte	1		! Load flags bzImage=1
	.word	0x8000		! size to move, when setup is not
	.long	0x100000	! 0x100000 = default for big kernel
	.long	0		! address of loaded ramdisk image
	.long	0		# its size in bytes
