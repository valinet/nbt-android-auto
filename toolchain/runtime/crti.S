/* .init/.fini prologues (crtbegin/crtend add their calls, crtn.S ends them). */
	.section .init,"ax",@progbits
	.globl	_init
	.type	_init, @function
_init:
	.section .fini,"ax",@progbits
	.globl	_fini
	.type	_fini, @function
_fini:
	.section .note.GNU-stack,"",@progbits
