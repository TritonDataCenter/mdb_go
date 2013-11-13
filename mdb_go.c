/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

/*
 * mdb(1M) module for debugging Go.
 */

#include <stdlib.h>
#include <sys/mdb_modapi.h>

#include "mdb_go_types.h"

static int
load_current_context(uintptr_t *frameptr, uintptr_t *insptr,
    uintptr_t *stackptr)
{
	mdb_reg_t regfp, regip, regsp;

#ifdef __amd64
	if (mdb_getareg(1, "rbp", &regfp) != 0 ||
	    mdb_getareg(1, "rip", &regip) != 0 ||
	    mdb_getareg(1, "rsp", &regsp) != 0) {
#else
#ifdef __i386
	if (mdb_getareg(1, "ebp", &regfp) != 0 ||
	    mdb_getareg(1, "eip", &regip) != 0 ||
	    mdb_getareg(1, "esp", &regsp) != 0) {
#else
#error Unrecognized microprocessor
#endif
#endif
		mdb_warn("failed to load current context\n");
		return (-1);
	}

	if (frameptr != NULL)
		*frameptr = (uintptr_t)regfp;

	if (insptr != NULL)
		*insptr = (uintptr_t)regip;

	if (stackptr != NULL)
		*stackptr = (uintptr_t)regsp;

	return (0);
}

/*
 * If anyone is adding ARM support, the quantum there is 4.
 */
#if defined(__i386) || defined(__amd64)
#define	GO_PC_QUANTUM	1
#else
#error Unrecognized microprocessor
#endif

/*
 * Go stores a 'pclntab' entry in the binary which contains function
 * information.  At the start is a header containing the following.
 */
struct pctabhdr {
	uint32_t magic;		/* 0xfffffffb */
	uint16_t zeros;		/* 0x0000 */
	uint8_t quantum;	/* 1 on x86, 4 on ARM */
	uint8_t ptrsize;	/* sizeof(uintptr_t) */
	uintptr_t tabsize;	/* size of function symbol table */
};
/*
 * After the header is a function symbol table, containing entries of the
 * following type.
 */
typedef struct go_func_table {
	uintptr_t entry;
	uintptr_t offset;
} go_functbl_t;

/*
 * In-memory function information.
 */
typedef struct go_func {
	uintptr_t entry;
	uint32_t nameoff;
	uint32_t args;
	uint32_t frame;
	uint32_t pcsp;
	uint32_t pcfile;
	uint32_t pcln;
	uint32_t npcdata;
	uint32_t nfuncdata;
} go_func_t;

/*
 * Global storage.
 */
uintptr_t pclntab;
uintptr_t ftabsize;

#define	GO_FUNCTABLE_OFFSET	(pclntab + sizeof (struct pctabhdr))
#define	GO_FUNCTABLE_SIZE	(ftabsize * sizeof (go_functbl_t))

#define	PC_TAB_OFFSET(x)	(pclntab + x)

/*
 * Find a corresponding function to an address in the ftab.
 */
uintptr_t
findfunc(uintptr_t addr)
{
	go_functbl_t *ftbl;
	unsigned int nf, n;
	ssize_t len;

	if (ftabsize == 0)
		return (NULL);

	ftbl = mdb_alloc(GO_FUNCTABLE_SIZE, UM_SLEEP);
	len = mdb_vread(ftbl, GO_FUNCTABLE_SIZE, GO_FUNCTABLE_OFFSET);

	if (len == -1)
		return (NULL);

	if (addr < ftbl[0].entry || addr >= ftbl[ftabsize - 1].entry) {
		mdb_free(ftbl, GO_FUNCTABLE_SIZE);
		return (NULL);
	}

	nf = ftabsize;

	while (nf > 0) {
		n = nf/2;
		if (ftbl[n].entry <= addr && addr < ftbl[n+1].entry) {
			return (ftbl[n].offset);
		} else if (addr < ftbl[n].entry)
			nf = n;
		else {
			ftbl += n+1;
			nf -= n+1;
		}
	}

	mdb_warn("unable to find go function at 0x%x\n", addr);
	mdb_free(ftbl, GO_FUNCTABLE_SIZE);
	return (NULL);
}

static int
do_goframe(uintptr_t addr, char *prop)
{
	uintptr_t offset;
	char buf[512]; // XXX
	go_func_t f;
	ssize_t len;

	offset = findfunc(addr);
	if (offset == NULL) {
		return (DCMD_ERR);
	}

	len = mdb_vread(&f, sizeof (go_func_t), PC_TAB_OFFSET(offset));
	if (len == -1) {
		mdb_warn("Could not load function from function table\n");
		return (DCMD_ERR);
	}
	len = mdb_vread(&buf, sizeof (buf), PC_TAB_OFFSET(f.nameoff));
	if (prop != NULL && strcmp(prop, "name") == 0) {
		mdb_printf("%s()\n", buf);
		return (DCMD_OK);
	}
	mdb_printf("%p = {\n", f);
	mdb_inc_indent(8);
	mdb_printf("entry = %p,\n", f.entry);
	mdb_printf("nameoff = %p (name = %s),\n", f.nameoff, buf);
	mdb_printf("args = %p,\n", f.args);
	mdb_printf("frame = %p,\n", f.frame);
	mdb_printf("pcsp = %p,\n", f.pcsp);
	mdb_printf("pcfile = %p,\n", f.pcfile);
	mdb_printf("pcln = %p,\n", f.pcln);
	mdb_printf("npcdata = %p,\n", f.npcdata);
	mdb_printf("nfuncdata = %p,\n", f.nfuncdata);
	mdb_dec_indent(8);
	mdb_printf("}\n");

	return (DCMD_OK);
}

static int
dcmd_goframe(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uintptr_t p;
	char *opt_p = NULL;

	if (mdb_getopts(argc, argv,
	    'p', MDB_OPT_STR, &opt_p, NULL) != argc)
		return (DCMD_USAGE);

	if (!(flags & DCMD_ADDRSPEC)) {
		if (load_current_context(NULL, NULL, &addr) != 0)
			return (DCMD_ERR);
	}

	if (mdb_vread(&p, sizeof (p), addr) == -1) {
		mdb_warn("Could not load function from function table\n");
		return (DCMD_ERR);
	}

	do_goframe(p, opt_p);

	return (DCMD_OK);
}

static int
walk_goframes_init(mdb_walk_state_t *wsp)
{
	if (wsp->walk_addr != NULL)
		return (WALK_NEXT);

	if (load_current_context(NULL, NULL, &wsp->walk_addr) != 0)
		return (WALK_ERR);

	return (WALK_NEXT);
}

static int
walk_goframes_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr, offset, p;
	go_func_t f;
	int rv;
	ssize_t len;

	addr = wsp->walk_addr;
	rv = wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata);

	if (rv != WALK_NEXT)
		return (rv);

	len = mdb_vread(&p, sizeof (p), addr);
	offset = findfunc(p);
	if (offset == NULL) {
		return (DCMD_ERR);
	}

	len = mdb_vread(&f, sizeof (go_func_t), PC_TAB_OFFSET(offset));
	if (len == -1) {
		mdb_warn("Could not load function from function table\n");
		return (DCMD_ERR);
	}

	/* Skip over current frame */
	addr += f.frame;

	if (addr == NULL)
		return (WALK_DONE);

	wsp->walk_addr = addr;
	return (WALK_NEXT);
}

static int
dcmd_gostack(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uintptr_t insptr, stkptr;
	char *opt_p = NULL;

	if (mdb_getopts(argc, argv,
	    'p', MDB_OPT_STR, &opt_p,
	    NULL) != argc)
		return (DCMD_USAGE);

	load_current_context(&addr, &insptr, &stkptr);

	do_goframe(insptr, opt_p);

	if (mdb_pwalk_dcmd("goframe", "goframe", argc, argv, stkptr) == -1)
		return (DCMD_ERR);

	return (DCMD_OK);
}

static const char *
mdb_go_g_status(int16_t status)
{
	return (status == GS_Gidle ? "Gidle" :
	    status == GS_Grunnable ? "Grunnable" :
	    status == GS_Grunning ? "Grunning" :
	    status == GS_Gsyscall ? "Gsyscall" :
	    status == GS_Gwaiting ? "Gwaiting" :
	    status == GS_Gmoribund_unused ? "Gmoribund_unused" :
	    status == GS_Gdead ? "Gdead" :
	    "<UNKNOWN>");
}

static const char *
mdb_go_p_status(int16_t status)
{
	return (status == PS_Pidle ? "Pidle" :
	    status == PS_Prunning ? "Prunning" :
	    status == PS_Psyscall ? "Psyscall" :
	    status == PS_Pgcstop ? "Pgcstop" :
	    status == PS_Pdead ? "Pdead" :
	    "<UNKNOWN>");
}

static int
dcmd_go_p(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	P p;

	if (mdb_vread(&p, sizeof (p), addr) == -1) {
		mdb_warn("failed to read P from %p", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%p: goproc %d [%s]\n", addr, p.id,
	    mdb_go_p_status(p.status));
	mdb_printf("    runqsz %d\n", p.runqsize);
	mdb_printf("    m %p\n", p.m);

	return (DCMD_OK);
}

static int
dcmd_go_g(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	G g;

	if (mdb_vread(&g, sizeof (g), addr) == -1) {
		mdb_warn("failed to read G from %p", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%p: goroutine %d [%s]\n", addr, g.goid,
	    mdb_go_g_status(g.status));
	mdb_printf("      flags: %s %s %s\n", g.ispanic ? "panic" : "!panic",
	    g.issystem ? "system" : "!system",
	    g.isbackground ? "background" : "!background");
	mdb_printf("      create_pc %p (%a)\n", g.gopc, g.gopc);

	if (g.status == GS_Gsyscall) {
		mdb_printf("     stackbase: %p\n", g.syscallstack);
		mdb_printf("            sp: %p\n", g.syscallsp);
		mdb_printf("            pc: %p (%a)\n",
		    g.syscallpc, g.syscallpc);
		mdb_printf("    stackguard: %p\n", g.syscallguard);
	} else {
		mdb_printf("     stackbase: %p\n", g.stackbase);
		mdb_printf("            sp: %p\n", g.sched.sp);
		mdb_printf("            pc: %p (%a)\n", g.sched.pc, g.sched.pc);
		mdb_printf("    stackguard: %p\n", g.stackguard);
	}

	return (DCMD_OK);
}

static int
dcmd_go_m(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	M m;

	if (mdb_vread(&m, sizeof (m), addr) == -1) {
		mdb_warn("failed to read M from %p", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%p: gomach %d\n", addr, m.id);
	mdb_printf("    p %p nextp %p\n", m.p, m.nextp);
	mdb_printf("    curg %p\n", m.curg);
	mdb_printf("    gsignal %p caughtsig %p\n", m.gsignal, m.caughtsig);

	return (DCMD_OK);
}

static int
walk_go_g_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;
	uintptr_t allg;

	if (wsp->walk_addr != NULL)
		return (WALK_NEXT);

	if (mdb_lookup_by_name("runtime.allg", &sym) != 0) {
		mdb_warn("could not find runtime.allg");
		return (WALK_ERR);
	}

	if (mdb_vread(&allg, sizeof (allg), sym.st_value) == -1) {
		mdb_warn("could not load runtime.allg");
		return (WALK_ERR);
	}

	wsp->walk_addr = allg;

	return (WALK_NEXT);
}

static int
walk_go_g_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr;
	int rv;
	G g;

	addr = wsp->walk_addr;
	rv = wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata);

	if (rv != WALK_NEXT)
		return (rv);

	/*
	 * Load this G to get the next G
	 */
	if (mdb_vread(&g, sizeof (g), addr) == -1) {
		mdb_warn("could not read next P pointer");
		return (WALK_ERR);
	}

	if (g.alllink == NULL)
		return (WALK_DONE);
	wsp->walk_addr = (uintptr_t) g.alllink;

	return (WALK_NEXT);
}

static int
walk_go_m_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;
	uintptr_t allm;

	if (wsp->walk_addr != NULL)
		return (WALK_NEXT);

	if (mdb_lookup_by_name("runtime.allm", &sym) != 0) {
		mdb_warn("could not find runtime.allm");
		return (WALK_ERR);
	}

	if (mdb_vread(&allm, sizeof (allm), sym.st_value) == -1) {
		mdb_warn("could not load runtime.allm");
		return (WALK_ERR);
	}

	wsp->walk_addr = allm;

	return (WALK_NEXT);
}

static int
walk_go_m_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr;
	int rv;
	M m;

	addr = wsp->walk_addr;
	rv = wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata);

	if (rv != WALK_NEXT)
		return (rv);

	/*
	 * Load this M to get the next M
	 */
	if (mdb_vread(&m, sizeof (m), addr) == -1) {
		mdb_warn("could not read next M pointer");
		return (WALK_ERR);
	}

	if (m.alllink == NULL)
		return (WALK_DONE);
	wsp->walk_addr = (uintptr_t) m.alllink;

	return (WALK_NEXT);
}

static int
walk_go_p_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;
	uintptr_t allp;

	if (wsp->walk_addr != NULL)
		return (WALK_NEXT);

	if (mdb_lookup_by_name("runtime.allp", &sym) != 0) {
		mdb_warn("could not find runtime.allp");
		return (WALK_ERR);
	}

	if (mdb_vread(&allp, sizeof (allp), sym.st_value) == -1) {
		mdb_warn("could not load runtime.allp");
		return (WALK_ERR);
	}

	wsp->walk_addr = allp;

	return (WALK_NEXT);
}

static int
walk_go_p_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr;
	int rv;
	P p;

	addr = wsp->walk_addr;
	rv = wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata);

	if (rv != WALK_NEXT)
		return (rv);

	/*
	 * Load this P to get the next P
	 */
	if (mdb_vread(&p, sizeof (p), addr) == -1) {
		mdb_warn("could not read next P pointer");
		return (WALK_ERR);
	}

	if (p.link == NULL)
		return (WALK_DONE);
	wsp->walk_addr = (uintptr_t) p.link;

	return (WALK_NEXT);
}

static int
dcmd_go_sigtab(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	GElf_Sym sym;
	SigTab sigtab[73];
	int i;
	int start, stop;

	if (mdb_lookup_by_name("runtime.sigtab", &sym) != 0) {
		mdb_warn("could not find sigtab");
		return (DCMD_ERR);
	}

	if (mdb_vread(&sigtab, sizeof (sigtab), sym.st_value) == -1) {
		mdb_warn("failed to read sigtab");
		return (DCMD_ERR);
	}

	if (argc >= 1) {
		if (argv[0].a_type != MDB_TYPE_STRING) {
			mdb_warn("arg was not string type\n");
			return (DCMD_ERR);
		}
		stop = start = atoi(argv[0].a_un.a_str);
	} else {
		start = 0;
		stop = 72;
	}

	mdb_printf("printing sigtab:\n");
	for (i = start; i <= stop; i++) {
		int printed = 0;
		char buf[500];
		ssize_t sz;

		if ((sz = mdb_readstr(buf, 500, (uintptr_t)sigtab[i].name)) == -1) {
			mdb_warn("could not read");
			continue;
		}

		mdb_printf("    [%d] %s\n", i, buf);
		/*
		 * Print flags:
		 */
		mdb_printf("       flags:  ");
		if (sigtab[i].flags == 0)
			mdb_printf("%s%s", printed++ ? " | " : "", "NONE");
		if (sigtab[i].flags & SigNotify)
			mdb_printf("%s%s", printed++ ? " | " : "", "NOTIFY");
		if (sigtab[i].flags & SigKill)
			mdb_printf("%s%s", printed++ ? " | " : "", "KILL");
		if (sigtab[i].flags & SigThrow)
			mdb_printf("%s%s", printed++ ? " | " : "", "THROW");
		if (sigtab[i].flags & SigPanic)
			mdb_printf("%s%s", printed++ ? " | " : "", "PANIC");
		if (sigtab[i].flags & SigDefault)
			mdb_printf("%s%s", printed++ ? " | " : "", "DEFAULT");
		if (sigtab[i].flags & SigHandling)
			mdb_printf("%s%s", printed++ ? " | " : "", "HANDLING");
		if (sigtab[i].flags & SigIgnored)
			mdb_printf("%s%s", printed++ ? " | " : "", "IGNORED");
		mdb_printf("\n");
	}

	return (DCMD_OK);
}

static int
dcmd_go_timers(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	GElf_Sym sym;
	Timers timers;
	Timer timer;
	uintptr_t taddr;
	int i;

	if (mdb_lookup_by_name("timers", &sym) != 0) {
		mdb_warn("could not find timers");
		return (DCMD_ERR);
	}

	if (mdb_vread(&timers, sizeof (timers), sym.st_value) == -1) {
		mdb_warn("failed to read Timers from %p", addr);
		return (DCMD_ERR);
	}

	mdb_printf("go timers:\n");
	mdb_printf("  goroutine %p\n", timers.timerproc);
	mdb_printf("  len %d cap %d\n", timers.len, timers.cap);
	mdb_printf("  t %p\n", timers.t);
	mdb_printf("  sleeping %d resched %d\n",
	    timers.sleeping, timers.rescheduling);

	for (i = 0; i < timers.len; i++) {
		uintptr_t ttt;

		taddr = (uintptr_t)timers.t;
		taddr += (sizeof (void*)) * i;

		if (mdb_vread(&ttt, sizeof (uintptr_t), taddr) == -1) {
			mdb_warn("could not");
			continue;
		}

		if (mdb_vread(&timer, sizeof (timer), ttt) == -1) {
			mdb_warn("could not");
			continue;
		}

		mdb_printf("      when %lld period %lld\n",
		    timer.when, timer.period);
	}

	return (DCMD_OK);
}


static void
configure(void)
{
	GElf_Sym sym;
	struct pctabhdr phdr;

	/*
	 * Load and check pclntab header.
	 */
	if (mdb_lookup_by_name("pclntab", &sym) != 0)
		return;

	pclntab = sym.st_value;

	if (mdb_vread(&phdr, sizeof (struct pctabhdr), pclntab) == -1) {
		mdb_warn("Could not load pclntab header\n");
		return;
	}

	if (phdr.magic != 0xfffffffb || phdr.zeros != 0 ||
	    phdr.quantum != GO_PC_QUANTUM ||
	    phdr.ptrsize != sizeof (uintptr_t *)) {
		mdb_warn("invalid pclntab header\n");
		return;
	}

	ftabsize = phdr.tabsize;

	mdb_printf("Configured Go support\n");
}

static const mdb_dcmd_t go_mdb_dcmds[] = {
	{ "gostack", "[-p property]", "print a Go stack trace",
	    dcmd_gostack, NULL },
	{ "goframe", "[-p property]", "print a Go stack frame",
	    dcmd_goframe, NULL },
	{ "go_g", "...",
		"print some stuff about a G", dcmd_go_g },
	{ "go_p", "...",
		"print some stuff about a P", dcmd_go_p },
	{ "go_m", "...",
		"print some stuff about a M", dcmd_go_m },
	{ "go_timers", "...",
		"print some stuff about a Timer", dcmd_go_timers },
	{ "go_sigtab", "...",
		"print some stuff about the SigTab", dcmd_go_sigtab },
	{ NULL }
};

static const mdb_walker_t go_mdb_walkers[] = {
	{ "goframe", "walk Go stack frames",
		walk_goframes_init, walk_goframes_step },
	{ "go_g", "walk all G",
		walk_go_g_init, walk_go_g_step },
	{ "go_p", "walk all P",
		walk_go_p_init, walk_go_p_step },
	{ "go_m", "walk all M",
		walk_go_m_init, walk_go_m_step },
	{ NULL }
};

static const mdb_modinfo_t go_mdb =
    { MDB_API_VERSION, go_mdb_dcmds, go_mdb_walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	configure();
	return (&go_mdb);
}
