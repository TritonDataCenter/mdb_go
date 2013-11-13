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

#include <sys/mdb_modapi.h>

static int
load_current_context(uintptr_t *frameptr, uintptr_t *insptr, uintptr_t *stackptr)
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

#define	GO_FUNCTABLE_OFFSET	(pclntab + sizeof(struct pctabhdr))
#define	GO_FUNCTABLE_SIZE	(ftabsize * sizeof(go_functbl_t))

#define	PC_TAB_OFFSET(x)	(pclntab + x)

/*
 * Find a corresponding function to an address in the ftab.
 */
uintptr_t
findfunc(uintptr_t addr)
{
	go_functbl_t *ftbl; //, *f;
	unsigned int nf, n;
	ssize_t len;

	if (ftabsize == 0) {
		return NULL;
	}

	ftbl = mdb_alloc(GO_FUNCTABLE_SIZE, UM_SLEEP);
	len = mdb_vread(ftbl, GO_FUNCTABLE_SIZE, GO_FUNCTABLE_OFFSET);
	if (len == -1) {
		return NULL;
	}

	if (addr < ftbl[0].entry || addr >= ftbl[ftabsize - 1].entry) {
		mdb_free(ftbl, GO_FUNCTABLE_SIZE);
		return NULL;
	}

	nf = ftabsize;

	while (nf > 0) {
		n = nf/2;
		if (ftbl[n].entry <= addr && addr < ftbl[n+1].entry) {
			return ftbl[n].offset;
		} else if (addr < ftbl[n].entry)
			nf = n;
		else {
			ftbl += n+1;
			nf -= n+1;
		}
	}

	mdb_warn("unable to find go function at 0x%x\n", addr);
	mdb_free(ftbl, GO_FUNCTABLE_SIZE);
	return NULL;
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

	len = mdb_vread(&f, sizeof(go_func_t), PC_TAB_OFFSET(offset));
	if (len == -1) {
		mdb_warn("Could not load function from function table\n");
		return (DCMD_ERR);
	}
	len = mdb_vread(&buf, sizeof(buf), PC_TAB_OFFSET(f.nameoff));
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

	if (mdb_vread(&p, sizeof(p), addr) == -1) {
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

	len = mdb_vread(&p, sizeof(p), addr);
	offset = findfunc(p);
	if (offset == NULL) {
		return (DCMD_ERR);
	}

	len = mdb_vread(&f, sizeof(go_func_t), PC_TAB_OFFSET(offset));
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

	if (mdb_vread(&phdr, sizeof(struct pctabhdr), pclntab) == -1) {
		mdb_warn("Could not load pclntab header\n");
		return;
	}

	if (phdr.magic != 0xfffffffb || phdr.zeros != 0 ||
	    phdr.quantum != GO_PC_QUANTUM ||
	    phdr.ptrsize != sizeof(uintptr_t *)) {
		mdb_warn("invalid pclntab header\n");
		return;
	}

	ftabsize = phdr.tabsize;

	mdb_printf("Configured Go support\n");
}

static const mdb_dcmd_t go_mdb_dcmds[] = {
	{ "gostack", "[-p property]", "print a Go stack trace", dcmd_gostack, NULL },
	{ "goframe", "[-p property]", "print a Go stack frame", dcmd_goframe, NULL },
	{ NULL }
};

static const mdb_walker_t go_mdb_walkers[] = {
	{ "goframe", "walk Go stack frames",
		walk_goframes_init, walk_goframes_step },
	{ NULL }
};

static const mdb_modinfo_t go_mdb = { MDB_API_VERSION, go_mdb_dcmds, go_mdb_walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	configure();
	return (&go_mdb);
}
