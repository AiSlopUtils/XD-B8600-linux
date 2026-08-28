	.section .text.start,"ax"
	.align 2
	.global _start
	.type _start,@function
_start:
	mov.l .Lpayload_main, r0
	jmp @r0
	 nop
	.align 2
.Lpayload_main:
	.long _payload_main
	.size _start,.-_start
