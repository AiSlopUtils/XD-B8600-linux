	.text
	.align 2

	/* long write(int fd, const void *buffer, size_t count) */
	.global linux_write
	.type linux_write,@function
linux_write:
	mov	#4, r3
	trapa	#0x10
	 nop
	rts
	 nop
	.size linux_write, .-linux_write

	/* long pause(void) */
	.global linux_pause
	.type linux_pause,@function
linux_pause:
	mov	#29, r3
	trapa	#0x10
	 nop
	rts
	 nop
	.size linux_pause, .-linux_pause

	/* void exit(int status) -- should only be reached if init_main returns. */
	.global linux_exit
	.type linux_exit,@function
linux_exit:
	mov	#1, r3
	trapa	#0x10
	 nop
.Lexit_hang:
	bra	.Lexit_hang
	 nop
	.size linux_exit, .-linux_exit

	.section .note.GNU-stack,"",@progbits
