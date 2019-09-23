/*
*
* bootloader.S
*
* This program is called from the standard RPI boot firmware and loads the
* minix kernel and services into fixed real memory locations.
*
* A cpio backup file containing:
*	kernel.bin
*	mod01_ds
*	mod02_rs
*	mod03_pm
*	mod04_sched
*	mod05_vfs
*	mod06_memory
*	mod07_tty
*	mod08_mib
*	mod09_vm
*	mod10_pfs
*	mod11_mfs
*	mod12_init
* Is concatenated to the end of the bootloader binary. It copies the cpio file 
* out of the way into higher memory then reads it and loads the minix system
* into memory. Memory locations and sizes are fixed so if the size of the
* minix services grows this program may need to be changed.
*
* The original author is unknown, it was found on github and modified by 
* steven stewart <sstewart6@yahoo.com>
*
*/
#define ARM_MODE_HYP 0x1a
#define ARM_MODE_MASK 0x1f
#define ARM_MODE_SVC 0x13
#define PSR_FIQ 0x40
#define PSR_IRQ 0x80
#define PSR_ABORT 0x100
	
	.section .text 
	.balign 8
	.global vectors
vectors:
	b	reset_handler
	b	undefinded_handler
	b	svc_handler
	b	prefetch_handler
	b	abort_handler
	b	hypervisor_handler
	b	irq_handler
	b	fiq_handler

undefinded_handler:
	b	undefinded_handler
svc_handler:
	b	svc_handler
prefetch_handler:
	b	prefetch_handler
abort_handler:
	b	abort_handler
hypervisor_handler:
	b	hypervisor_handler
irq_handler:
	b	irq_handler
fiq_handler:
	b	fiq_handler

reset_handler:
	ldr	r9, =device_tree
	str r2, [r9]			/* save r2 which may be a device tree */
	/* exit hypmode */
	mrs r9, cpsr
	and r9, r9, #ARM_MODE_MASK
	teq	r9, #ARM_MODE_HYP
	bne	not_hyp_mode		/* not hyp mode just continue */
	bic r9, r9, #ARM_MODE_MASK
	orr r9, r9, #(PSR_FIQ | PSR_IRQ | ARM_MODE_SVC)
	orr r9, r9, #PSR_ABORT	/* mask imprecise abort */
	add	lr, pc, #8			/* lr points to instruction after eret */
							/* pc points 8 bytes ahead */
	msr	SPSR_hyp, r9		/* load spsr with svc mode. */
	msr ELR_hyp, lr
	eret

not_hyp_mode:

	/* Assign our vectors. */
	ldr	r0, =vectors
	mcr	p15, 0, r0, c12, c0, 0

	/* Set bit 6 of ACTLR (join SMP) */
	mrc	p15, 0, r0, c1, c0, 1
	mov	r1, r0
	orr	r0, r0, #0x040
	cmp	r0, r1
	mcrne	p15, 0, r0, c1, c0, 1

	/* Disable caches, mmu,  branch prediction. */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(0x1 << 12)	/* Instruction cache disabled */
	bic	r0, r0, #(0x1 << 11)	/* Branch prediction disabled */
	bic	r0, r0, #(0x1 << 2)		/* Data cache disabled */
	bic	r0, r0, #(0x1 << 1)		/* Alignment check disabled */
	bic	r0, r0, #0x1			/* MMU disabled */
	mcr	p15, 0, r0, c1, c0, 0

	/* disable high vectors */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 13)			/* clear V bit */
	mcr	p15, 0, r0, c1, c0, 0

	/* Initialize primary cpu */
	mrc	p15, 0, r0, c0, c0, 5	/* read cpu id reg */
	ands	r0, r0, #0x03		/* mask off all but cpu id bits */

	beq	primary_cpu
	bne	secondary_cpu

primary_cpu:
	b	bootloader

	/* Wait in the holding pen */
secondary_cpu:
	wfi
	b	secondary_cpu

	.global bootloader
bootloader:
	ldr	sp, =stack_top
	mov r9, r2

	/* Print banner */
	ldr	r0, =msg_empty
	bl	puts
	ldr	r0, =msg_empty
	bl	puts
	ldr	r0, =msg_banner
	bl	puts

	ldr	sp, =stack_top

	/* In supervisor mode */
	ldr	r0, =msg_svcmode
	bl	puts

	/* Move CPIO archive out of the way */
	ldr	r0, =0xA000000
	ldr	r1, =stack_top
	mov	r2, #0x2000000
	bl	memcpy

load_files:
	ldr	r0, =msg_extract
	bl	puts
	/* Load kernel */
	ldr	r0, =0x0A000000
	ldr	r1, =0x00200000
	bl	load_file
	/* Load module */
	ldr	r4, =0x02000000
	ldr	r5, =0x00800000
load_files_loop:
	mov	r1, r4
	bl	load_file
	cmp	r0, #0
	beq	final_preparations
	add	r4, r4, r5
	b	load_files_loop
final_preparations:
	/* Set up args and jump */
	mov	r0, #2
	ldr	r1, =argv
	ldr r2, device_tree 	/* restore r2 possible device tree */
	b	0x00200000

/*
 * Load a file from CPIO archive.
 * r0: address of CPIO archive
 * r1: address to copy the file to
 * Return: address of next entry, or 0 if loading failed
 */
load_file:
	push	{r4-r8,lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2

	/* Check header : either 070701 or 070702 */
	ldr	r0, [r4]
	ldr	r1, =0x37303730
	cmp	r0, r1
	bne	load_file_failed
	ldr	r0, [r4, #4]
	ldr	r1, =0xFFFF
	and	r1, r1, r0
	ldr	r2, =0x3130
	cmp	r1, r2
	beq	load_file_header_ok
	ldr	r2, =0x3230
	cmp	r1, r2
	bne	load_file_failed
load_file_header_ok:
	/* Check for trailer */
	ldrh	r0, [r4, #0x6e]
	ldr	r1, =0x5254
	cmp	r1, r0
	beq	load_file_failed
	ldrh	r0, [r4, #0x6e]
	ldr	r1, =0x4941
	cmp	r1, r0
	beq	load_file_failed

	/* Get data size */
	add	r0, r4, #0x36
	bl	hex2bin
	mov	r7, r0
	/* Get filename size */
	add	r0, r4, #0x5e
	bl	hex2bin
	mov	r8, r0

	/* Print file, size and offset */
	add	r0, r4, #0x6e
	bl	print

	mov	r0, r8
load_file_name_padding_loop:
	cmp	r0, #24
	bgt	load_file_name_padding_loop_end
	push	{r0}
	mov	r0, #0x20
	bl	putc
	pop	{r0}
	add	r0, r0, #1
	b	load_file_name_padding_loop

load_file_name_padding_loop_end:
	ldr	r0, =msg_size
	bl	print
	mov	r0, r7
	bl	print_hex
	ldr	r0, =msg_addr
	bl	print
	mov	r0, r5
	bl	print_hex
	/* Add CR/LF */
	mov	r0, #0xd
	bl	putc
	mov	r0, #0xa
	bl	putc

	/* Get pointer to data source */
	add	r1, r4, #0x6e
	add	r1, r1, r8
	/* Align to 4 bytes */
	and	r2, r1, #0x3
	cmp	r2, #0
	mov	r0, #4
	subne	r2, r0, r2
	addne   r1, r1, r2

	mov	r0, r5
	mov	r2, r7
	push	{r1}
	bl	memcpy

	pop	{r0}
	add	r0, r7
	/* Align to 4 bytes */
	and	r2, r0, #0x3
	cmp	r2, #0
	mov	r1, #4
	subne	r2, r1, r2
	addne   r0, r0, r2

	pop	{r4-r8,lr}
	bx	lr
load_file_failed:
	mov	r0, #0
	pop	{r4-r8,lr}
	bx	lr

/*
 * Send one character to the PL011 UART.
 * r0: character to send
 */
putc:
	/* Loop as long as FIFO isn't empty */
	ldr	r1, =0x3f201018
	ldr	r1, [r1]
	and	r1, #0x80
	tst	r1, #0x80
	beq	putc

	# Write character
	ldr	r1, =0x3f201000
	str	r0, [r1]

	bx	lr

/*
 * Print an ASCIIZ string to the PL011 UART.
 * r0: address of string to print
 */
print:
	push	{lr}
	/* If end of string, add newline */
print_loop:
	ldrb	r1, [r0]
	cmp	r1, #0
	beq	print_end

	/* Send character */
	push	{r0}
	mov	r0, r1
	bl	putc
	pop	{r0}
	/* Advance to next character */
	add	r0, r0, #1
	b	print_loop

print_end:
	pop	{lr}
	bx	lr
/*
 * Print an ASCIIZ string to the PL011 UART, followed by CR/LF.
 * r0: address of string to print
 */
puts:
	push	{lr}
	bl	print

	/* Add CR/LF */
	mov	r0, #0xd
	bl	putc
	mov	r0, #0xa
	bl	putc

	pop	{lr}
	bx	lr


/*
 * Convert 8 hexadecimal characters to binary.
 * r0: address of string to convert
 * Return: the converted value
 */
hex2bin:
	mov	r1, #0
	mov	r2, #0
hex2bin_loop:
	ldrb	r3, [r0, r2]
	cmp	r3, #0x40
	sublt	r3, r3, #0x30
	blt	hex2bin_add_digit
	cmp	r3, #0x60
	sublt	r3, r3, #0x37
	blt	hex2bin_add_digit
	sub	r3, r3, #0x57
hex2bin_add_digit:
	lsl	r1, r1, #4
	add	r1, r1, r3
	add	r2, r2, #1
	cmp	r2, #8
	blt	hex2bin_loop
	mov	r0, r1
	bx	lr

/*
 * Print a value as hexadecimal.
 * r0: value to print
 */
print_hex:
	push	{lr}
	mov	r1, #8
print_hex_loop:
	cmp	r1, #0
	ble	print_hex_end

	lsr	r2, r0, #28
	cmp	r2, #10
	addlt	r2, r2, #0x30
	addge	r2, r2, #0x37

	push	{r0-r2}
	mov	r0, r2
	bl	putc
	pop	{r0-r2}

	sub	r1, r1, #1
	lsl	r0, r0, #4
	b	print_hex_loop
print_hex_end:
	pop	{lr}
	bx	lr

/*
 * Quickly copy a memory area to another.
 * r0: destination
 * r1: source
 * r2: length
 */
memcpy:
	push	{r5-r11}
	add	r2, r0, r2
memcpy_loop:
	cmp	r0, r2
	bge	memcpy_done
	ldm	r1!, {r3-r11}
	stm	r0!, {r3-r11}
	b	memcpy_loop
memcpy_done:
	pop	{r5-r11}
	bx	lr

	msg_empty:
	.asciz ""
msg_banner:
	.asciz "MINIX 3 unpacker for Raspberry Pi"
msg_svcmode:
	.asciz "Current mode: supervisor"
msg_hypmode:
	.asciz "Current mode: hypervisor. Switching to supervisor mode..."
msg_extract:
	.asciz "Extracting modules..."
msg_size:
	.asciz " size:0x"
msg_addr:
	.asciz "    address:0x"
.align 4
device_tree:
	.word	0x0
argv:
	.word	argv0
	.word	argv1
	.word	0x0
argv0:
	.asciz "0x00200000"
	.asciz "    "
argv1:
	.asciz "board_name=RPI_2_B console=tty00 rootdevname=c0d0p1 verbose=1 log_level=4"
.data
.align 4
	.space 256
stack_top:
