/*
 * $QNXLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 * 
 * You must obtain a written license from and pay applicable license fees to QNX 
 * Software Systems before you may reproduce, modify or distribute this software, 
 * or any work that includes all or part of this software.   Free development 
 * licenses are available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *  
 * This file may contain contributions from others.  Please review this entire 
 * file for other proprietary rights or license notices, as well as the QNX 
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
 * for other information.
 * $
 */

/*
 * VFP coprocessor handling
 */

#include "externs.h"
#include <arm/vfp.h>

static inline unsigned
fpexc_get()
{
	unsigned	fpexc;

	__asm__ __volatile__("mrc	p10, 7, %0, c8, c0, 0" : "=r"(fpexc));
	return fpexc;
}

static inline void
fpexc_set(unsigned fpexc)
{
	__asm__ __volatile__("mcr	p10, 7, %0, c8, c0, 0" : : "r"(fpexc));
}

void vfp_disable();
asm (
	".globl	vfp_disable											\n"
	"vfp_disable:												\n"
	"	mrc		p10, 7, r0, c8, c0, 0		@ fmrx r0, fpexc	\n"
	"	bic		r0, r0, #(1<<30)			@ clear FPEXC_EN	\n"
	"	mcr		p10, 7, r0, c8, c0, 0		@ fmxr fpexc, r0	\n"
	"	mov		pc, lr											\n"
);

void
vfp_ctx_init(FPU_REGISTERS *fpu)
{
	/*
	 * Initialise VFP to RunFast mode
	 */
	fpu->un.vfp.fpexc = ARM_VFP_FPEXC_EN;
	fpu->un.vfp.fpscr = ARM_VFP_RUNFAST_MODE;
}

void vfp_v2_save(FPU_REGISTERS *fpu);
asm (
	".globl	vfp_v2_save											\n"
	"vfp_v2_save:												\n"
	"	mrc		p10, 7, r2, c8, c0, 0	@ fmrx r0, fpexc		\n"
	"	orr		r2, r2, #(1<<30)		@ set FPEXC_EN			\n"
	"	mov		r1, r1					@ preserve for below	\n"
	"	bic		r2, r2, #(1<<31)		@ clear FPEXC_EX		\n"
	"	mcr		p10, 7, r2, c8, c0, 0	@ fmxr fpexc, r0		\n"
	"	mov		ip, r0					@ register base address	\n"
#ifdef	VARIANT_v6
	"	stc		p11, c0, [ip], #32*4	@ fstmiad ip!, {d0-d15}	\n"
#else
	"	stc		p11, c0, [ip], #33*4	@ fstmiax ip!, {d0-d15}	\n"
#endif
	"	add		ip, r0, #32*8			@ &fpu->un.vfp.fpscr	\n"
	"	mrc		p10, 7, r0, c1, c0, 0	@ fmrx r0, fpscr		\n"
	"	mrc		p10, 7, r2, c9, c0, 0	@ fmrx r2, fpinst		\n"
	"	mrc		p10, 7, r3, c10, c0, 0	@ fmrx r3, fpinst2		\n"
	"	stmia	ip, {r0-r3}										\n"
	"	mov		pc, lr											\n"
);

void vfp_v2_restore(FPU_REGISTERS *fpu);
asm (
	".globl	vfp_v2_restore										\n"
	"vfp_v2_restore:											\n"
	"	mrc		p10, 7, r1, c8, c0, 0	@ fmrx r0, fpexc		\n"
	"	orr		r1, r1, #(1<<30)		@ set FPEXC_EN			\n"
	"	bic		r1, r1, #(1<<31)		@ clear FPEXC_EX		\n"
	"	mcr		p10, 7, r1, c8, c0, 0	@ fmxr fpexc, r0		\n"
	"	mov		ip, r0					@ register base address	\n"
#ifdef	VARIANT_v6
	"	ldc		p11, c0, [ip], #32*4	@ fldmiad ip!, {d0-d15}	\n"
#else
	"	ldc		p11, c0, [ip], #33*4	@ fldmiax ip!, {d0-d15}	\n"
#endif
	"	add		ip, r0, #32*8			@ &fpu->un.vfp.fpscr	\n"
	"	ldmia	ip, {r0-r3}										\n"
	"	mcr		p10, 7, r2, c9, c0, 0	@ fmxr fpinst, r2		\n"
	"	mcr		p10, 7, r3, c10, c0, 0	@ fmxr fpinst2, r3		\n"
	"	mcr		p10, 7, r0, c1, c0, 0	@ fmxr fpscr, r0		\n"
	"	mcr		p10, 7, r1, c8, c0, 0	@ fmxr fpexc, r1		\n"
	"	mov		pc, lr											\n"
);

/*
 * Package up the system registers that can only be read in a
 * privileged mode to pass up to the VFP support code.
 */
void
vfp_fpemu_prep(ARM_FPEMU_CONTEXT *ctx)
{
	unsigned	tmp;

	__asm__ __volatile__("mrc	p10, 7, %0, c8, c0, 0" : "=r"(tmp));
	ctx->un.vfp.fpexc = tmp;
	/*
	 * Make sure we clear all exception bits before bouncing into emulator
	 */
	tmp &= ~(ARM_VFP_FPEXC_EN   |
			 ARM_VFP_FPEXC_FP2V |
			 ARM_VFP_FPEXC_INV  |
			 ARM_VFP_FPEXC_UFC  |
			 ARM_VFP_FPEXC_OFC  |
			 ARM_VFP_FPEXC_IOC);
	__asm__ __volatile__("mcr	p10, 7, %0, c8, c0, 0" : : "r"(tmp));

	__asm__ __volatile__("mrc	p10, 7, %0, c9, c0, 0" : "=r"(tmp));
	ctx->un.vfp.fpinst = tmp;

	__asm__ __volatile__("mrc	p10, 7, %0, c10, c0, 0" : "=r"(tmp));
	ctx->un.vfp.fpinst2 = tmp;
}

/*
 * VFP exception handler
 */
int
vfp_handler(void *t, unsigned inst, CPU_REGISTERS *reg, unsigned *signo)
{
	THREAD		*thp = t;
	unsigned	fpexc;

	/*
	 * Read FPEXC to see if VFP is enabled or not
	 */
	fpexc = fpexc_get();
	if (fpexc & ARM_VFP_FPEXC_EN) {
		/*
		 * VFP is enabled and raised h/w exception.
		 * Need to bounce into support code to process the exception.
		 *
		 * Set signo so that the thread is terminated with a signal value
		 * that indicates whether the fpemu.so implementation is missing
		 * or doesn't perform VFP exception handling support. 
		 */
		*signo = MAKE_SIGCODE(SIGFPE, FPE_NOFPU, FLTNOFPU);
		return ARM_COPROC_EMULATE;
	}

	/*
	 * VFP is disabled.
	 */
	if (thp->fpudata == 0) {
		/*
		 * Need to allocate FPU save area via __ker_exit()
		 */
		atomic_set(&thp->async_flags, _NTO_ATF_FPUSAVE_ALLOC);
		return ARM_COPROC_HANDLED;
	}
	
	lock_kernel();
	if (actives_fpu[RUNCPU] == thp) {
		/*
		 * FPU state is still active - simply re-enable VFP
		 */
		fpexc_set(fpexc | ARM_VFP_FPEXC_EN);
	}
	else {
		THREAD			*act = actives_fpu[RUNCPU];
		FPU_REGISTERS	*fpu;

		/*
		 * Need to save active VFP context and restore thp's VFP context
		 */
		if (act) {
			fpu = FPUDATA_PTR(act->fpudata);
			fpu_ctx_save(fpu);
#ifdef	VARIANT_smp
			act->fpudata = fpu;			// clear BUSY/CPU bits
#endif
			actives_fpu[RUNCPU] = 0;
		}

#ifdef	VARIANT_smp
		fpu = thp->fpudata;
		if (FPUDATA_INUSE(fpu) && FPUDATA_CPU(fpu) != RUNCPU) {
			unsigned	fpd;

			/*
			 * Context is active on another cpu.
			 * Send an IPI and wait for the context to be saved
			 *
			 * Unlock the kernel while spinning to reduce scheduling latency
			 * and set INKERNEL_EXIT so that we restart the instruction if
			 * we get preempted.
			 */
			bitset_inkernel(INKERNEL_EXIT);
			unlock_kernel();
			SENDIPI(FPUDATA_CPU(fpu), IPI_CONTEXT_SAVE);
			__asm__ __volatile__(
			"0:	ldr		%0, [%1];	\n"
			"	tst		%0, %2;		\n"
			"	bne		0b;			\n"
			: "=&r" (fpd)
			: "r" (&thp->fpudata), "r" (FPUDATA_BUSY)
			);
			lock_kernel();
		}
#endif
		fpu = FPUDATA_PTR(thp->fpudata);
		vfp_v2_restore(fpu);
#ifdef	VARIANT_smp
		/*
		 * Indicate fpudata is active on this cpu
		 */
		thp->fpudata = (void *)((uintptr_t)fpu | FPUDATA_BUSY | RUNCPU);
#endif
		actives_fpu[RUNCPU] = thp;
	}
	return ARM_COPROC_HANDLED;
}

void
vfp_init()
{
	/*
	 * FIXME: need to check VFP version and use appropriate context routines
	 */
	fpu_ctx_init    = vfp_ctx_init;
	fpu_ctx_save    = vfp_v2_save;
	fpu_ctx_restore = vfp_v2_restore;
	fpu_disable     = vfp_disable;
	fpu_fpemu_prep  = vfp_fpemu_prep;

	(void) coproc_attach(10, vfp_handler);
	(void) coproc_attach(11, vfp_handler);
}

__SRCVERSION("vfp.c $Rev: 153052 $");
