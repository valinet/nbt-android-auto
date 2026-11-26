/* Process entry for QNX 6.5.0 x86 executables. Written for this project; behaviour matches the start code of
 * binaries built with the original SDP: the loader passes its exit hook in %edx and argc/argv/envp/auxv on the
 * stack; libc is initialised with _init_libc, then the init/fini arrays and .init run, then main and exit. */
	.text
	.globl	_start
	.type	_start, @function
_start:
	movl	%edx, %ebx
	movl	(%esp), %esi
	leal	4(%esp), %ecx
	leal	(%ecx,%esi,4), %eax
	leal	4(%eax), %edi
	leal	8(%eax), %edx
	cmpl	$0, 4(%eax)
	je	2f
1:	movl	(%edx), %eax
	addl	$4, %edx
	testl	%eax, %eax
	jne	1b
2:	pushl	%ebx
	pushl	%edx
	pushl	%edi
	pushl	%ecx
	pushl	%esi
	call	_init_libc
	pushl	$__preinit_array_end
	pushl	$__preinit_array_start
	call	_preinit_array
	addl	$8, %esp
	pushl	$_fini
	call	atexit
	addl	$4, %esp
	pushl	$__fini_array_end
	pushl	$__fini_array_start
	call	_fini_array
	addl	$8, %esp
	call	_init
	pushl	$__init_array_end
	pushl	$__init_array_start
	call	_init_array
	addl	$8, %esp
	movl	$0, errno
	call	main
	pushl	%eax
	call	exit
	int3
	.size	_start, .-_start

	/* main thread errno; libc.so.3 refers to it */
	.globl	errno
	.type	errno, @object
	.size	errno, 4
	.comm	errno, 4, 4
	.section .note.GNU-stack,"",@progbits
