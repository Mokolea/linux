/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/kernel/entry-common.S
 *
 *  Copyright (C) 2000 Russell King
 */

#include <asm/assembler.h>
#include <asm/unistd.h>
#include <asm/ftrace.h>
#include <asm/unwind.h>
#include <asm/page.h>
#ifdef CONFIG_AEABI
#include <asm/unistd-oabi.h>
#endif

	.equ	NR_syscalls, __NR_syscalls

#include "entry-header.S"

saved_psr	.req	r8
#if defined(CONFIG_TRACE_IRQFLAGS) || defined(CONFIG_CONTEXT_TRACKING_USER)
saved_pc	.req	r9
#define TRACE(x...) x
#else
saved_pc	.req	lr
#define TRACE(x...)
#endif

	.section .entry.text,"ax",%progbits
	.align	5
#if !(IS_ENABLED(CONFIG_TRACE_IRQFLAGS) || IS_ENABLED(CONFIG_CONTEXT_TRACKING_USER) || \
	IS_ENABLED(CONFIG_DEBUG_RSEQ))
/*
 * This is the fast syscall return path.  We do as little as possible here,
 * such as avoiding writing r0 to the stack.  We only use this path if we
 * have tracing, context tracking and rseq debug disabled - the overheads
 * from those features make this path too inefficient.
 */
ret_fast_syscall:
__ret_fast_syscall:
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	disable_irq_notrace			@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
	movs	r1, r1, lsl #16
	bne	fast_work_pending

	restore_user_regs fast = 1, offset = S_OFF
 UNWIND(.fnend		)
ENDPROC(ret_fast_syscall)

	/* Ok, we need to do extra processing, enter the slow path. */
fast_work_pending:
	str	r0, [sp, #S_R0+S_OFF]!		@ returned r0
	/* fall through to work_pending */
#else
/*
 * The "replacement" ret_fast_syscall for when tracing, context tracking,
 * or rseq debug is enabled.  As we will need to call out to some C functions,
 * we save r0 first to avoid needing to save registers around each C function
 * call.
 */
ret_fast_syscall:
__ret_fast_syscall:
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
#if IS_ENABLED(CONFIG_DEBUG_RSEQ)
	/* do_rseq_syscall needs interrupts enabled. */
	mov	r0, sp				@ 'regs'
	bl	do_rseq_syscall
#endif
	disable_irq_notrace			@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
	movs	r1, r1, lsl #16
	beq	no_work_pending
 UNWIND(.fnend		)
ENDPROC(ret_fast_syscall)

	/* Slower path - fall through to work_pending */
#endif

	tst	r1, #_TIF_SYSCALL_WORK
	bne	__sys_trace_return_nosave
slow_work_pending:
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	bl	do_work_pending
	cmp	r0, #0
	beq	no_work_pending
	movlt	scno, #(__NR_restart_syscall - __NR_SYSCALL_BASE)
	str	scno, [tsk, #TI_ABI_SYSCALL]	@ make sure tracers see update
	ldmia	sp, {r0 - r6}			@ have to reload r0 - r6
	b	local_restart			@ ... and off we go
ENDPROC(ret_fast_syscall)

/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 * IRQs may be enabled here, so always disable them.  Note that we use the
 * "notrace" version to avoid calling into the tracing code unnecessarily.
 * do_work_pending() will update this state if necessary.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
#if IS_ENABLED(CONFIG_DEBUG_RSEQ)
	/* do_rseq_syscall needs interrupts enabled. */
	enable_irq_notrace			@ enable interrupts
	mov	r0, sp				@ 'regs'
	bl	do_rseq_syscall
#endif
	disable_irq_notrace			@ disable interrupts
ENTRY(ret_to_user_from_irq)
	ldr	r1, [tsk, #TI_FLAGS]
	movs	r1, r1, lsl #16
	bne	slow_work_pending
no_work_pending:
	asm_trace_hardirqs_on save = 0

	ct_user_enter save = 0

#ifdef CONFIG_KSTACK_ERASE
	bl	stackleak_erase_on_task_stack
#endif
	restore_user_regs fast = 0, offset = 0
ENDPROC(ret_to_user_from_irq)
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cmp	r5, #0
	movne	r0, r4
	badrne	lr, 1f
	retne	r5
1:	get_thread_info tsk
	b	ret_slow_syscall
ENDPROC(ret_from_fork)

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

	.align	5
#ifdef CONFIG_HARDEN_BRANCH_HISTORY
ENTRY(vector_bhb_loop8_swi)
	sub	sp, sp, #PT_REGS_SIZE
	stmia	sp, {r0 - r12}
	mov	r8, #8
1:	b	2f
2:	subs	r8, r8, #1
	bne	1b
	dsb	nsh
	isb
	b	3f
ENDPROC(vector_bhb_loop8_swi)

	.align	5
ENTRY(vector_bhb_bpiall_swi)
	sub	sp, sp, #PT_REGS_SIZE
	stmia	sp, {r0 - r12}
	mcr	p15, 0, r8, c7, c5, 6	@ BPIALL
	isb
	b	3f
ENDPROC(vector_bhb_bpiall_swi)
#endif
	.align	5
ENTRY(vector_swi)
#ifdef CONFIG_CPU_V7M
	v7m_exception_entry
#else
	sub	sp, sp, #PT_REGS_SIZE
	stmia	sp, {r0 - r12}			@ Calling r0 - r12
3:
 ARM(	add	r8, sp, #S_PC		)
 ARM(	stmdb	r8, {sp, lr}^		)	@ Calling sp, lr
 THUMB(	mov	r8, sp			)
 THUMB(	store_user_sp_lr r8, r10, S_SP	)	@ calling sp, lr
	mrs	saved_psr, spsr			@ called from non-FIQ mode, so ok.
 TRACE(	mov	saved_pc, lr		)
	str	saved_pc, [sp, #S_PC]		@ Save calling PC
	str	saved_psr, [sp, #S_PSR]		@ Save CPSR
	str	r0, [sp, #S_OLD_R0]		@ Save OLD_R0
#endif
	reload_current r10, ip
	zero_fp
	alignment_trap r10, ip, cr_alignment
	asm_trace_hardirqs_on save=0
	enable_irq_notrace
	ct_user_exit save=0

	/*
	 * Get the system call number.
	 */

#if defined(CONFIG_OABI_COMPAT)

	/*
	 * If we have CONFIG_OABI_COMPAT then we need to look at the swi
	 * value to determine if it is an EABI or an old ABI call.
	 */
#ifdef CONFIG_ARM_THUMB
	tst	saved_psr, #PSR_T_BIT
	movne	r10, #0				@ no thumb OABI emulation
 USER(	ldreq	r10, [saved_pc, #-4]	)	@ get SWI instruction
#else
 USER(	ldr	r10, [saved_pc, #-4]	)	@ get SWI instruction
#endif
 ARM_BE8(rev	r10, r10)			@ little endian instruction

#elif defined(CONFIG_AEABI)

	/*
	 * Pure EABI user space always put syscall number into scno (r7).
	 */
#elif defined(CONFIG_ARM_THUMB)
	/* Legacy ABI only, possibly thumb mode. */
	tst	saved_psr, #PSR_T_BIT		@ this is SPSR from save_user_regs
	addne	scno, r7, #__NR_SYSCALL_BASE	@ put OS number in
 USER(	ldreq	scno, [saved_pc, #-4]	)

#else
	/* Legacy ABI only. */
 USER(	ldr	scno, [saved_pc, #-4]	)	@ get SWI instruction
#endif

	/* saved_psr and saved_pc are now dead */

	uaccess_disable tbl
	get_thread_info tsk

	adr	tbl, sys_call_table		@ load syscall table pointer

#if defined(CONFIG_OABI_COMPAT)
	/*
	 * If the swi argument is zero, this is an EABI call and we do nothing.
	 *
	 * If this is an old ABI call, get the syscall number into scno and
	 * get the old ABI syscall table address.
	 */
	bics	r10, r10, #0xff000000
	strne	r10, [tsk, #TI_ABI_SYSCALL]
	streq	scno, [tsk, #TI_ABI_SYSCALL]
	eorne	scno, r10, #__NR_OABI_SYSCALL_BASE
	ldrne	tbl, =sys_oabi_call_table
#elif !defined(CONFIG_AEABI)
	bic	scno, scno, #0xff000000		@ mask off SWI op-code
	str	scno, [tsk, #TI_ABI_SYSCALL]
	eor	scno, scno, #__NR_SYSCALL_BASE	@ check OS number
#else
	str	scno, [tsk, #TI_ABI_SYSCALL]
#endif
	/*
	 * Reload the registers that may have been corrupted on entry to
	 * the syscall assembly (by tracing or context tracking.)
	 */
 TRACE(	ldmia	sp, {r0 - r3}		)

local_restart:
	ldr	r10, [tsk, #TI_FLAGS]		@ check for syscall tracing
	stmdb	sp!, {r4, r5}			@ push fifth and sixth args

	tst	r10, #_TIF_SYSCALL_WORK		@ are we tracing syscalls?
	bne	__sys_trace

	invoke_syscall tbl, scno, r10, __ret_fast_syscall

	add	r1, sp, #S_OFF
2:	cmp	scno, #(__ARM_NR_BASE - __NR_SYSCALL_BASE)
	eor	r0, scno, #__NR_SYSCALL_BASE	@ put OS number back
	bcs	arm_syscall
	mov	why, #0				@ no longer a real syscall
	b	sys_ni_syscall			@ not private func

#if defined(CONFIG_OABI_COMPAT) || !defined(CONFIG_AEABI)
	/*
	 * We failed to handle a fault trying to access the page
	 * containing the swi instruction, but we're not really in a
	 * position to return -EFAULT. Instead, return back to the
	 * instruction and re-enter the user fault handling path trying
	 * to page it in. This will likely result in sending SEGV to the
	 * current task.
	 */
9001:
	sub	lr, saved_pc, #4
	str	lr, [sp, #S_PC]
	get_thread_info tsk
	b	ret_fast_syscall
#endif
ENDPROC(vector_swi)
	.ltorg

	/*
	 * This is the really slow path.  We're going to be doing
	 * context switches, and waiting for our parent to respond.
	 */
__sys_trace:
	add	r0, sp, #S_OFF
	bl	syscall_trace_enter
	mov	scno, r0
	invoke_syscall tbl, scno, r10, __sys_trace_return, reload=1
	cmp	scno, #-1			@ skip the syscall?
	bne	2b
	add	sp, sp, #S_OFF			@ restore stack

__sys_trace_return_nosave:
	enable_irq_notrace
	mov	r0, sp
	bl	syscall_trace_exit
	b	ret_slow_syscall

__sys_trace_return:
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
	mov	r0, sp
	bl	syscall_trace_exit
	b	ret_slow_syscall

	.macro	syscall_table_start, sym
	.equ	__sys_nr, 0
	.type	\sym, #object
ENTRY(\sym)
	.endm

	.macro	syscall, nr, func
	.ifgt	__sys_nr - \nr
	.error	"Duplicated/unorded system call entry"
	.endif
	.rept	\nr - __sys_nr
	.long	sys_ni_syscall
	.endr
	.long	\func
	.equ	__sys_nr, \nr + 1
	.endm

	.macro	syscall_table_end, sym
	.ifgt	__sys_nr - __NR_syscalls
	.error	"System call table too big"
	.endif
	.rept	__NR_syscalls - __sys_nr
	.long	sys_ni_syscall
	.endr
	.size	\sym, . - \sym
	.endm

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
#define __SYSCALL(nr, func) syscall nr, func

/*
 * This is the syscall table declaration for native ABI syscalls.
 * With EABI a couple syscalls are obsolete and defined as sys_ni_syscall.
 */
	syscall_table_start sys_call_table
#ifdef CONFIG_AEABI
#include <calls-eabi.S>
#else
#include <calls-oabi.S>
#endif
	syscall_table_end sys_call_table

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r8 = syscall table
sys_syscall:
		bic	scno, r0, #__NR_OABI_SYSCALL_BASE
		cmp	scno, #__NR_syscall - __NR_SYSCALL_BASE
		cmpne	scno, #NR_syscalls	@ check range
#ifdef CONFIG_CPU_SPECTRE
		movhs	scno, #0
		csdb
#endif
		stmialo	sp, {r5, r6}		@ shuffle args
		movlo	r0, r1
		movlo	r1, r2
		movlo	r2, r3
		movlo	r3, r4
		ldrlo	pc, [tbl, scno, lsl #2]
		b	sys_ni_syscall
ENDPROC(sys_syscall)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	sys_sigreturn
ENDPROC(sys_sigreturn_wrapper)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn_wrapper)

sys_statfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_statfs64
ENDPROC(sys_statfs64_wrapper)

sys_fstatfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_fstatfs64
ENDPROC(sys_fstatfs64_wrapper)

/*
 * Note: off_4k (r5) is always units of 4K.  If we can't do the requested
 * offset, we return EINVAL.
 */
sys_mmap2:
		str	r5, [sp, #4]
		b	sys_mmap_pgoff
ENDPROC(sys_mmap2)

#ifdef CONFIG_OABI_COMPAT

/*
 * These are syscalls with argument register differences
 */

sys_oabi_pread64:
		stmia	sp, {r3, r4}
		b	sys_pread64
ENDPROC(sys_oabi_pread64)

sys_oabi_pwrite64:
		stmia	sp, {r3, r4}
		b	sys_pwrite64
ENDPROC(sys_oabi_pwrite64)

sys_oabi_truncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_truncate64
ENDPROC(sys_oabi_truncate64)

sys_oabi_ftruncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_ftruncate64
ENDPROC(sys_oabi_ftruncate64)

sys_oabi_readahead:
		str	r3, [sp]
		mov	r3, r2
		mov	r2, r1
		b	sys_readahead
ENDPROC(sys_oabi_readahead)

/*
 * Let's declare a second syscall table for old ABI binaries
 * using the compatibility syscall entries.
 */
	syscall_table_start sys_oabi_call_table
#undef __SYSCALL_WITH_COMPAT
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)
#include <calls-oabi.S>
	syscall_table_end sys_oabi_call_table

#endif

