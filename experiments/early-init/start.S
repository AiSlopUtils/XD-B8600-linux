	.section .text.start,"ax"
	.align 2
	.global _start
	.type _start,@function
_start:
	mov.l	.Lmain, r0
	jsr	@r0
	 nop
	mov	r0, r4
	mov.l	.Lexit, r0
	jmp	@r0
	 nop
	.align 2
.Lmain:
	.long	init_main
.Lexit:
	.long	linux_exit
	.size _start, .-_start

	.section .note.GNU-stack,"",@progbits
