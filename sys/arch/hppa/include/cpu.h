/*	$OpenBSD: cpu.h,v 1.28 2002/03/14 01:26:32 millert Exp $	*/

/*
 * Copyright (c) 2000-2001 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/* 
 * Copyright (c) 1988-1994, The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: cpu.h 1.19 94/12/16$
 */

#ifndef	_MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#include <machine/trap.h>
#include <machine/frame.h>

/*
 * CPU types and features
 */
#define	HPPA_FTRS_BTLBS		0x00000001
#define	HPPA_FTRS_BTLBU		0x00000002
#define	HPPA_FTRS_HVT		0x00000004
#define	HPPA_FTRS_W32B		0x00000008

#ifndef _LOCORE
/* types */
enum hppa_cpu_type {
	hpcx, hpcxs, hpcxt, hpcxta, hpcxl, hpcxl2, hpcxu, hpcxu2, hpcxw
};
extern enum hppa_cpu_type cpu_type;
extern const char *cpu_typename;
#endif

/*
 * COPR/SFUs
 */
#define	HPPA_FPUS	0xc0
#define	HPPA_FPUVER(w)	(((w) & 0x003ff800) >> 11)
#define	HPPA_PMSFUS	0x20	/* ??? */

/*
 * Exported definitions unique to hp700/PA-RISC cpu support.
 */

#define	HPPA_PGALIAS	0x00100000
#define	HPPA_PGAMASK	0xfff00000
#define	HPPA_PGAOFF	0x000fffff

#define	HPPA_IOSPACE	0xf0000000
#define	HPPA_IOBCAST	0xfffc0000
#define	HPPA_PDC_LOW	0xef000000
#define	HPPA_PDC_HIGH	0xf1000000
#define	HPPA_FPA	0xfff80000
#define	HPPA_FLEX_DATA	0xfff80001
#define	HPPA_DMA_ENABLE	0x00000001
#define	HPPA_FLEX_MASK	0xfffc0000
#define	HPPA_FLEX(a)	(((a) & HPPA_FLEX_MASK) >> 18)
#define	HPPA_SPA_ENABLE	0x00000020
#define	HPPA_NMODSPBUS	64

#define	clockframe		trapframe
#define	CLKF_BASEPRI(framep)	((framep)->tf_eiem == ~0U)
#define	CLKF_PC(framep)		((framep)->tf_iioq_head)
#define	CLKF_INTR(framep)	((framep)->tf_flags & TFF_INTR)
#define	CLKF_USERMODE(framep)	((framep)->tf_flags & T_USER)
#define	CLKF_SYSCALL(framep)	((framep)->tf_flags & TFF_SYS)

#define	signotify(p)		(setsoftast())
#define	need_resched()		(want_resched = 1, setsoftast())
#define	need_proftick(p)	((p)->p_flag |= P_OWEUPC, setsoftast())

#ifndef _LOCORE
#ifdef _KERNEL
#define MD_CACHE_FLUSH 0
#define MD_CACHE_PURGE 1
#define MD_CACHE_CTL(a,s,t)	\
	(((t)? pdcache : fdcache) (HPPA_SID_KERNEL,(vaddr_t)(a),(s)))

extern int want_resched;

#define DELAY(x) delay(x)

extern int (*cpu_desidhash)(void);

void	delay(u_int us);
void	hppa_init(paddr_t start);
void	trap(int type, struct trapframe *frame);
int	spcopy(pa_space_t ssp, const void *src,
		    pa_space_t dsp, void *dst, size_t size);
int	spstrcpy(pa_space_t ssp, const void *src,
		      pa_space_t dsp, void *dst, size_t size, size_t *rsize);
int	copy_on_fault(void);
void	switch_trampoline(void);
void	switch_exit(struct proc *p);
int	cpu_dumpsize(void);
int	cpu_dump(void);
#endif

/*
 * Boot arguments stuff
 */

#define	BOOTARG_LEN	(NBPG)
#define	BOOTARG_OFF	(0x10000)

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
#define	CPU_MAXID		2	/* number of valid machdep ids */

#define CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
}
#endif

#endif /* _MACHINE_CPU_H_ */
