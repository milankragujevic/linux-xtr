/*
 * Routines providing a simple monitor for use on the PowerMac.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/string.h>
#include "nonstdio.h"
#include "privinst.h"

#define scanhex	xmon_scanhex
#define skipbl	xmon_skipbl

static unsigned adrs;
static int size = 1;
static unsigned ndump = 64;
static unsigned nidump = 16;
static int termch;

static u_int bus_error_jmp[100];
#define setjmp xmon_setjmp
#define longjmp xmon_longjmp

/* Breakpoint stuff */
struct bpt {
	unsigned address;
	unsigned instr;
	unsigned count;
	unsigned char enabled;
};

#define NBPTS	16
static struct bpt bpts[NBPTS];
static struct bpt dabr;
static struct bpt iabr;
static unsigned bpinstr = 0x7fe00008;	/* trap */

/* Prototypes */
extern void (*debugger_fault_handler)(struct pt_regs *);
static int cmds(struct pt_regs *);
static int mread(unsigned, void *, int);
static int mwrite(unsigned, void *, int);
static void handle_fault(struct pt_regs *);
static void byterev(unsigned char *, int);
static void memex(void);
static int bsesc(void);
static void dump(void);
static void prdump(unsigned, int);
#ifdef __MWERKS__
static void prndump(unsigned, int);
static int nvreadb(unsigned);
#endif
static int ppc_inst_dump(unsigned, int);
void print_address(unsigned);
static int getsp(void);
static void dump_hash_table(void);
static void backtrace(struct pt_regs *);
static void excprint(struct pt_regs *);
static void prregs(struct pt_regs *);
static void memops(int);
static void memlocate(void);
static void memzcan(void);
static void memdiffs(unsigned char *, unsigned char *, unsigned, unsigned);
int skipbl(void);
int scanhex(unsigned *valp);
static void scannl(void);
static int hexdigit(int);
void getstring(char *, int);
static void flush_input(void);
static int inchar(void);
static void take_input(char *);
/* static void openforth(void); */
static unsigned read_spr(int);
static void write_spr(int, unsigned);
static void super_regs(void);
static void print_sysmap(void);
static void remove_bpts(void);
static void insert_bpts(void);
static struct bpt *at_breakpoint(unsigned pc);
static void bpt_cmds(void);
static void cacheflush(void);
static char *pretty_lookup_name(unsigned long addr);
static char *lookup_name(unsigned long addr);

extern int print_insn_big_powerpc(FILE *, unsigned long, unsigned);
extern void printf(const char *fmt, ...);
extern int putchar(int ch);
extern int setjmp(u_int *);
extern void longjmp(u_int *, int);

#define GETWORD(v)	(((v)[0] << 24) + ((v)[1] << 16) + ((v)[2] << 8) + (v)[3])

static char *help_string = "\
Commands:\n\
  d	dump bytes\n\
  di	dump instructions\n\
  df	dump float values\n\
  dd	dump double values\n\
  e	print exception information\n\
  h	dump hash table\n\
  m	examine/change memory\n\
  mm	move a block of memory\n\
  ms	set a block of memory\n\
  md	compare two blocks of memory\n\
  M	print System.map\n\
  r	print registers\n\
  S	print special registers\n\
  t	print backtrace\n\
  x	exit monitor\n\
";

static int xmon_trace;
#define SSTEP	1		/* stepping because of 's' command */
#define BRSTEP	2		/* stepping over breakpoint */

void
xmon(struct pt_regs *excp)
{
	struct pt_regs regs;
	int msr, cmd, i;
	unsigned *sp;

	if (excp == NULL) {
		asm volatile ("stw	0,0(%0)\n\
			lwz	0,0(1)\n\
			stw	0,4(%0)\n\
			stmw	2,8(%0)" : : "b" (&regs));
		regs.nip = regs.link = ((unsigned long *)regs.gpr[1])[1];
		regs.msr = get_msr();
		regs.ctr = get_ctr();
		regs.xer = get_xer();
		regs.ccr = get_cr();
		regs.trap = 0;
		excp = &regs;
	}

	prom_drawstring("xmon pc="); prom_drawhex(excp->nip);
	prom_drawstring(" lr="); prom_drawhex(excp->link);
	prom_drawstring(" msr="); prom_drawhex(excp->msr);
	prom_drawstring(" trap="); prom_drawhex(excp->trap);
	prom_drawstring(" sp="); prom_drawhex(excp->gpr[1]);
	sp = &excp->gpr[0];
	for (i = 0; i < 32; ++i) {
		if ((i & 7) == 0)
			prom_drawstring("\n");
		prom_drawstring(" ");
		prom_drawhex(sp[i]);
	}
	sp = (unsigned *) excp->gpr[1];
	for (i = 0; i < 64; ++i) {
		if ((i & 7) == 0) {
			prom_drawstring("\n");
			prom_drawhex(sp);
			prom_drawstring(" ");
		}
		prom_drawstring(" ");
		prom_drawhex(sp[i]);
	}
	prom_drawstring("\n");
	msr = get_msr();
	set_msr(msr & ~0x8000);	/* disable interrupts */
	remove_bpts();
	excprint(excp);
	cmd = cmds(excp);
	if (cmd == 's') {
		xmon_trace = SSTEP;
		excp->msr |= 0x400;
	} else if (at_breakpoint(excp->nip)) {
		xmon_trace = BRSTEP;
		excp->msr |= 0x400;
	} else {
		xmon_trace = 0;
		insert_bpts();
	}
	set_msr(msr);		/* restore interrupt enable */
}

void
xmon_irq(int irq, void *d, struct pt_regs *regs)
{
	printf("Keyboard interrupt\n");
	xmon(regs);
}

int
xmon_bpt(struct pt_regs *regs)
{
	struct bpt *bp;

	bp = at_breakpoint(regs->nip);
	if (!bp)
		return 0;
	if (bp->count) {
		--bp->count;
		remove_bpts();
		excprint(regs);
		xmon_trace = BRSTEP;
		regs->msr |= 0x400;
	} else {
		xmon(regs);
	}
	return 1;
}

int
xmon_sstep(struct pt_regs *regs)
{
	if (!xmon_trace)
		return 0;
	if (xmon_trace == BRSTEP) {
		xmon_trace = 0;
		insert_bpts();
	} else {
		xmon(regs);
	}
	return 1;
}

int
xmon_dabr_match(struct pt_regs *regs)
{
	if (dabr.enabled && dabr.count) {
		--dabr.count;
		remove_bpts();
		excprint(regs);
		xmon_trace = BRSTEP;
		regs->msr |= 0x400;
	} else {
		dabr.instr = regs->nip;
		xmon(regs);
	}
	return 1;
}

int
xmon_iabr_match(struct pt_regs *regs)
{
	if (iabr.enabled && iabr.count) {
		--iabr.count;
		remove_bpts();
		excprint(regs);
		xmon_trace = BRSTEP;
		regs->msr |= 0x400;
	} else {
		xmon(regs);
	}
	return 1;
}

static struct bpt *
at_breakpoint(unsigned pc)
{
	int i;
	struct bpt *bp;

	if (dabr.enabled && pc == dabr.instr)
		return &dabr;
	if (iabr.enabled && pc == iabr.address)
		return &iabr;
	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp)
		if (bp->enabled && pc == bp->address)
			return bp;
	return 0;
}

static void
insert_bpts()
{
	int i;
	struct bpt *bp;

	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if (!bp->enabled)
			continue;
		if (mread(bp->address, &bp->instr, 4) != 4
		    || mwrite(bp->address, &bpinstr, 4) != 4) {
			printf("Couldn't insert breakpoint at %x, disabling\n",
			       bp->address);
			bp->enabled = 0;
		}
	}
	if (dabr.enabled)
		set_dabr(dabr.address);
	if (iabr.enabled)
		set_iabr(iabr.address);
}

static void
remove_bpts()
{
	int i;
	struct bpt *bp;
	unsigned instr;

	set_dabr(0);
	set_iabr(0);
	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if (!bp->enabled)
			continue;
		if (mread(bp->address, &instr, 4) == 4
		    && instr == bpinstr
		    && mwrite(bp->address, &bp->instr, 4) != 4)
			printf("Couldn't remove breakpoint at %x\n",
			       bp->address);
	}
}

static char *last_cmd;

/* Command interpreting routine */
static int
cmds(struct pt_regs *excp)
{
	int cmd;

	last_cmd = NULL;
	for(;;) {
		printf("mon> ");
		fflush(stdout);
		flush_input();
		termch = 0;
		cmd = skipbl();
		if( cmd == '\n' ) {
			if (last_cmd == NULL)
				continue;
			take_input(last_cmd);
			last_cmd = NULL;
			cmd = inchar();
		}
		switch (cmd) {
		case 'm':
			cmd = inchar();
			switch (cmd) {
			case 'm':
			case 's':
			case 'd':
				memops(cmd);
				break;
			case 'l':
				memlocate();
				break;
			case 'z':
				memzcan();
				break;
			default:
				termch = cmd;
				memex();
			}
			break;
		case 'd':
			dump();
			break;
		case 'r':
			if (excp != NULL)
				prregs(excp);	/* print regs */
			break;
		case 'e':
			if (excp == NULL)
				printf("No exception information\n");
			else
				excprint(excp);
			break;
		case 'M':
			print_sysmap();
		case 'S':
			super_regs();
			break;
		case 't':
			backtrace(excp);
			break;
		case 'f':
			cacheflush();
			break;
		case 'h':
			dump_hash_table();
			break;
		case 's':
		case 'x':
		case EOF:
			return cmd;
		case '?':
			printf(help_string);
			break;
		default:
			printf("Unrecognized command: ");
			if( ' ' < cmd && cmd <= '~' )
				putchar(cmd);
			else
				printf("\\x%x", cmd);
			printf(" (type ? for help)\n");
			break;
		case 'b':
			bpt_cmds();
			break;
		}
	}
}

static void
bpt_cmds(void)
{
	int cmd;
	unsigned a;
	int mode, i;
	struct bpt *bp;

	cmd = inchar();
	switch (cmd) {
	case 'd':
		mode = 7;
		cmd = inchar();
		if (cmd == 'r')
			mode = 5;
		else if (cmd == 'w')
			mode = 6;
		else
			termch = cmd;
		dabr.address = 0;
		dabr.count = 0;
		dabr.enabled = scanhex(&dabr.address);
		scanhex(&dabr.count);
		if (dabr.enabled)
			dabr.address = (dabr.address & ~7) | mode;
		break;
	case 'i':
		iabr.address = 0;
		iabr.count = 0;
		iabr.enabled = scanhex(&iabr.address);
		if (iabr.enabled)
			iabr.address |= 3;
		scanhex(&iabr.count);
		break;
	case 'c':
		if (!scanhex(&a)) {
			/* clear all breakpoints */
			for (i = 0; i < NBPTS; ++i)
				bpts[i].enabled = 0;
			iabr.enabled = 0;
			dabr.enabled = 0;
			printf("All breakpoints cleared\n");
		} else {
			bp = at_breakpoint(a);
			if (bp == 0) {
				printf("No breakpoint at %x\n", a);
			} else {
				bp->enabled = 0;
			}
		}
		break;
	default:
		termch = cmd;
		if (!scanhex(&a)) {
			/* print all breakpoints */
			printf("type  address   count\n");
			if (dabr.enabled) {
				printf("data %.8x %8x [", dabr.address & ~7,
				       dabr.count);
				if (dabr.address & 1)
					printf("r");
				if (dabr.address & 2)
					printf("w");
				printf("]\n");
			}
			if (iabr.enabled)
				printf("inst %.8x %8x\n", iabr.address & ~3,
				       iabr.count);
			for (bp = bpts; bp < &bpts[NBPTS]; ++bp)
				if (bp->enabled)
					printf("trap %.8x %8x\n", bp->address,
					       bp->count);
			break;
		}
		bp = at_breakpoint(a);
		if (bp == 0) {
			for (bp = bpts; bp < &bpts[NBPTS]; ++bp)
				if (!bp->enabled)
					break;
			if (bp >= &bpts[NBPTS]) {
				printf("Sorry, no free breakpoints\n");
				break;
			}
		}
		bp->enabled = 1;
		bp->address = a;
		bp->count = 0;
		scanhex(&bp->count);
		break;
	}
}

static void
backtrace(struct pt_regs *excp)
{
	unsigned sp;
	unsigned stack[2];
	struct pt_regs regs;
	extern char ret_from_intercept, ret_from_syscall_1, ret_from_syscall_2;
	extern char lost_irq_ret, do_bottom_half_ret, do_signal_ret;
	extern char ret_from_except;

	if (excp != NULL)
		sp = excp->gpr[1];
	else
		sp = getsp();
	scanhex(&sp);
	scannl();
	for (; sp != 0; sp = stack[0]) {
		if (mread(sp, stack, sizeof(stack)) != sizeof(stack))
			break;
		printf("%x ", stack[1]);
		if (stack[1] == (unsigned) &ret_from_intercept
		    || stack[1] == (unsigned) &ret_from_except
		    || stack[1] == (unsigned) &ret_from_syscall_1
		    || stack[1] == (unsigned) &ret_from_syscall_2
		    || stack[1] == (unsigned) &lost_irq_ret
		    || stack[1] == (unsigned) &do_bottom_half_ret
		    || stack[1] == (unsigned) &do_signal_ret) {
			if (mread(sp+16, &regs, sizeof(regs)) != sizeof(regs))
				break;
			printf("\nexception:%x [%x] %x ", regs.trap, sp+16,
			       regs.nip);
			sp = regs.gpr[1];
			if (mread(sp, stack, sizeof(stack)) != sizeof(stack))
				break;
		}
	}
	printf("\n");
}

int
getsp()
{
    int x;

    asm("mr %0,1" : "=r" (x) :);
    return x;
}

void
excprint(struct pt_regs *fp)
{
	printf("vector: %x at pc = %x %s",
	       fp->trap, fp->nip, pretty_lookup_name(fp->nip));
	printf(", msr = %x, sp = %x [%x]\n",
	       fp->msr, fp->gpr[1], fp);
	if (fp->trap == 0x300 || fp->trap == 0x600)
		printf("dar = %x, dsisr = %x\n", fp->dar, fp->dsisr);
	if (current)
		printf("current = %x, pid = %d, comm = %s\n",
		       current, current->pid, current->comm);
}

void
prregs(struct pt_regs *fp)
{
	int n;
	unsigned base;

	if (scanhex(&base))
		fp = (struct pt_regs *) base;
	for (n = 0; n < 32; ++n)
		printf("R%.2d = %.8x%s", n, fp->gpr[n],
		       (n & 3) == 3? "\n": "   ");
	printf("pc  = %.8x   msr = %.8x   lr  = %.8x   cr  = %.8x\n",
	       fp->nip, fp->msr, fp->link, fp->ccr);
	printf("ctr = %.8x   xer = %.8x   trap = %4x\n",
	       fp->ctr, fp->xer, fp->trap);
}

void
cacheflush(void)
{
	int cmd;
	unsigned nflush;

	cmd = inchar();
	if (cmd != 'i')
		termch = cmd;
	scanhex(&adrs);
	if (termch != '\n')
		termch = 0;
	nflush = 1;
	scanhex(&nflush);
	nflush = (nflush + 31) / 32;
	if (cmd != 'i') {
		for (; nflush > 0; --nflush, adrs += 0x20)
			cflush((void *) adrs);
	} else {
		for (; nflush > 0; --nflush, adrs += 0x20)
			cinval((void *) adrs);
	}
}

unsigned int
read_spr(int n)
{
    unsigned int instrs[2];
    int (*code)(void);

    instrs[0] = 0x7c6002a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
    instrs[1] = 0x4e800020;
    store_inst(instrs);
    store_inst(instrs+1);
    code = (int (*)(void)) instrs;
    return code();
}

void
write_spr(int n, unsigned int val)
{
    unsigned int instrs[2];
    int (*code)(unsigned int);

    instrs[0] = 0x7c6003a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
    instrs[1] = 0x4e800020;
    store_inst(instrs);
    store_inst(instrs+1);
    code = (int (*)(unsigned int)) instrs;
    code(val);
}

static unsigned int regno;
extern char exc_prolog;
extern char dec_exc;

void
print_sysmap(void)
{
	extern char *sysmap;
	if ( sysmap )
		printf("System.map: \n%s", sysmap);
}

void
super_regs()
{
	int i, cmd;
	unsigned val;

	cmd = skipbl();
	if (cmd == '\n') {
		printf("msr = %x, pvr = %x\n", get_msr(), get_pvr());
		printf("sprg0-3 = %x %x %x %x\n", get_sprg0(), get_sprg1(),
		       get_sprg2(), get_sprg3());
		printf("srr0 = %x, srr1 = %x\n", get_srr0(), get_srr1());
		printf("sr0-15 =");
		for (i = 0; i < 16; ++i)
			printf(" %x", get_sr(i));
		printf("\n");
		asm("mr %0,1" : "=r" (i) :);
		printf("sp = %x ", i);
		asm("mr %0,2" : "=r" (i) :);
		printf("toc = %x\n", i);
		return;
	}

	scanhex(&regno);
	switch (cmd) {
	case 'w':
		val = read_spr(regno);
		scanhex(&val);
		write_spr(regno, val);
		/* fall through */
	case 'r':
		printf("spr %x = %x\n", regno, read_spr(regno));
		break;
	case 's':
		val = get_sr(regno);
		scanhex(&val);
		set_sr(regno, val);
		break;
	case 'm':
		val = get_msr();
		scanhex(&val);
		set_msr(val);
		break;
	}
	scannl();
}

#if 0
static void
openforth()
{
    int c;
    char *p;
    char cmd[1024];
    int args[5];
    extern int (*prom_entry)(int *);

    p = cmd;
    c = skipbl();
    while (c != '\n') {
	*p++ = c;
	c = inchar();
    }
    *p = 0;
    args[0] = (int) "interpret";
    args[1] = 1;
    args[2] = 1;
    args[3] = (int) cmd;
    (*prom_entry)(args);
    printf("\n");
    if (args[4] != 0)
	printf("error %x\n", args[4]);
}
#endif

static void
dump_hash_table_seg(unsigned seg, unsigned start, unsigned end)
{
	extern void *Hash;
	extern unsigned long Hash_size;
	unsigned *htab = Hash;
	unsigned hsize = Hash_size;
	unsigned v, hmask, va, last_va;
	int found, last_found, i;
	unsigned *hg, w1, last_w2, last_va0;

	last_found = 0;
	hmask = hsize / 64 - 1;
	va = start;
	start = (start >> 12) & 0xffff;
	end = (end >> 12) & 0xffff;
	for (v = start; v < end; ++v) {
		found = 0;
		hg = htab + (((v ^ seg) & hmask) * 16);
		w1 = 0x80000000 | (seg << 7) | (v >> 10);
		for (i = 0; i < 8; ++i, hg += 2) {
			if (*hg == w1) {
				found = 1;
				break;
			}
		}
		if (!found) {
			w1 ^= 0x40;
			hg = htab + ((~(v ^ seg) & hmask) * 16);
			for (i = 0; i < 8; ++i, hg += 2) {
				if (*hg == w1) {
					found = 1;
					break;
				}
			}
		}
		if (!(last_found && found && (hg[1] & ~0x180) == last_w2 + 4096)) {
			if (last_found) {
				if (last_va != last_va0)
					printf(" ... %x", last_va);
				printf("\n");
			}
			if (found) {
				printf("%x to %x", va, hg[1]);
				last_va0 = va;
			}
			last_found = found;
		}
		if (found) {
			last_w2 = hg[1] & ~0x180;
			last_va = va;
		}
		va += 4096;
	}
	if (last_found)
		printf(" ... %x\n", last_va);
}
static unsigned hash_ctx;
static unsigned hash_start;
static unsigned hash_end;

static void
dump_hash_table()
{
	int seg;
	unsigned seg_start, seg_end;

	hash_ctx = 0;
	hash_start = 0;
	hash_end = 0xfffff000;
	scanhex(&hash_ctx);
	scanhex(&hash_start);
	scanhex(&hash_end);
	printf("Mappings for context %x\n", hash_ctx);
	seg_start = hash_start;
	for (seg = hash_start >> 28; seg <= hash_end >> 28; ++seg) {
		seg_end = (seg << 28) | 0x0ffff000;
		if (seg_end > hash_end)
			seg_end = hash_end;
		dump_hash_table_seg((hash_ctx << 4) + seg, seg_start, seg_end);
		seg_start = seg_end + 0x1000;
	}
}

/*
 * Stuff for reading and writing memory safely
 */
extern inline void sync(void)
{
	asm volatile("sync; isync");
}

extern inline void __delay(unsigned int loops)
{
	if (loops != 0)
		__asm__ __volatile__("mtctr %0; 1: bdnz 1b" : :
				     "r" (loops) : "ctr");
}

int
mread(unsigned adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if( setjmp(bus_error_jmp) == 0 ){
		debugger_fault_handler = handle_fault;
		sync();
		p = (char *) adrs;
		q = (char *) buf;
		switch (size) {
		case 2: *(short *)q = *(short *)p;	break;
		case 4: *(int *)q = *(int *)p;		break;
		default:
			for( ; n < size; ++n ) {
				*q++ = *p++;
				sync();
			}
		}
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	}
	debugger_fault_handler = 0;
	return n;
}

int
mwrite(unsigned adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if( setjmp(bus_error_jmp) == 0 ){
		debugger_fault_handler = handle_fault;
		sync();
		p = (char *) adrs;
		q = (char *) buf;
		switch (size) {
		case 2: *(short *)p = *(short *)q;	break;
		case 4: *(int *)p = *(int *)q;		break;
		default:
			for( ; n < size; ++n ) {
				*p++ = *q++;
				sync();
			}
		}
		sync();
		n = size;
	} else {
		printf("*** Error writing address %x\n", adrs + n);
	}
	debugger_fault_handler = 0;
	return n;
}

static int fault_type;
static char *fault_chars[] = { "--", "**", "##" };

static void
handle_fault(struct pt_regs *regs)
{
	fault_type = regs->trap == 0x200? 0: regs->trap == 0x300? 1: 2;
	longjmp(bus_error_jmp, 1);
}

#define SWAP(a, b, t)	((t) = (a), (a) = (b), (b) = (t))

void
byterev(unsigned char *val, int size)
{
	int t;
	
	switch (size) {
	case 2:
		SWAP(val[0], val[1], t);
		break;
	case 4:
		SWAP(val[0], val[3], t);
		SWAP(val[1], val[2], t);
		break;
	}
}

static int brev;
static int mnoread;

void
memex()
{
    int cmd, inc, i, nslash;
    unsigned n;
    unsigned char val[4];

    last_cmd = "m\n";
    scanhex(&adrs);
    while ((cmd = skipbl()) != '\n') {
	switch( cmd ){
	case 'b':	size = 1;	break;
	case 'w':	size = 2;	break;
	case 'l':	size = 4;	break;
	case 'r': 	brev = !brev;	break;
	case 'n':	mnoread = 1;	break;
	case '.':	mnoread = 0;	break;
	}
    }
    if( size <= 0 )
	size = 1;
    else if( size > 4 )
	size = 4;
    for(;;){
	if (!mnoread)
	    n = mread(adrs, val, size);
	printf("%.8x%c", adrs, brev? 'r': ' ');
	if (!mnoread) {
	    if (brev)
		byterev(val, size);
	    putchar(' ');
	    for (i = 0; i < n; ++i)
		printf("%.2x", val[i]);
	    for (; i < size; ++i)
		printf("%s", fault_chars[fault_type]);
	}
	putchar(' ');
	inc = size;
	nslash = 0;
	for(;;){
	    if( scanhex(&n) ){
		for (i = 0; i < size; ++i)
		    val[i] = n >> (i * 8);
		if (!brev)
		    byterev(val, size);
		mwrite(adrs, val, size);
		inc = size;
	    }
	    cmd = skipbl();
	    if (cmd == '\n')
		break;
	    inc = 0;
	    switch (cmd) {
	    case '\'':
		for(;;){
		    n = inchar();
		    if( n == '\\' )
			n = bsesc();
		    else if( n == '\'' )
			break;
		    for (i = 0; i < size; ++i)
			val[i] = n >> (i * 8);
		    if (!brev)
			byterev(val, size);
		    mwrite(adrs, val, size);
		    adrs += size;
		}
		adrs -= size;
		inc = size;
		break;
	    case ',':
		adrs += size;
		break;
	    case '.':
		mnoread = 0;
		break;
	    case ';':
		break;
	    case 'x':
	    case EOF:
		scannl();
		return;
	    case 'b':
	    case 'v':
		size = 1;
		break;
	    case 'w':
		size = 2;
		break;
	    case 'l':
		size = 4;
		break;
	    case '^':
		adrs -= size;
		break;
		break;
	    case '/':
		if (nslash > 0)
		    adrs -= 1 << nslash;
		else
		    nslash = 0;
		nslash += 4;
		adrs += 1 << nslash;
		break;
	    case '\\':
		if (nslash < 0)
		    adrs += 1 << -nslash;
		else
		    nslash = 0;
		nslash -= 4;
		adrs -= 1 << -nslash;
		break;
	    case 'm':
		scanhex(&adrs);
		break;
	    case 'n':
		mnoread = 1;
		break;
	    case 'r':
		brev = !brev;
		break;
	    case '<':
		n = size;
		scanhex(&n);
		adrs -= n;
		break;
	    case '>':
		n = size;
		scanhex(&n);
		adrs += n;
		break;
	    }
	}
	adrs += inc;
    }
}

int
bsesc()
{
	int c;

	c = inchar();
	switch( c ){
	case 'n':	c = '\n';	break;
	case 'r':	c = '\r';	break;
	case 'b':	c = '\b';	break;
	case 't':	c = '\t';	break;
	}
	return c;
}

#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))
void
dump()
{
	int c;

	c = inchar();
	if ((isxdigit(c) && c != 'f' && c != 'd') || c == '\n')
		termch = c;
	scanhex(&adrs);
	if( termch != '\n')
		termch = 0;
	if( c == 'i' ){
		scanhex(&nidump);
		if( nidump == 0 )
			nidump = 16;
		adrs += ppc_inst_dump(adrs, nidump);
		last_cmd = "di\n";
	} else {
		scanhex(&ndump);
		if( ndump == 0 )
			ndump = 64;
		prdump(adrs, ndump);
		adrs += ndump;
		last_cmd = "d\n";
	}
}

void
prdump(unsigned adrs, int ndump)
{
	register int n, m, c, r, nr;
	unsigned char temp[16];

	for( n = ndump; n > 0; ){
		printf("%.8x", adrs);
		putchar(' ');
		r = n < 16? n: 16;
		nr = mread(adrs, temp, r);
		adrs += nr;
		for( m = 0; m < r; ++m ){
			putchar((m & 3) == 0 && m > 0? '.': ' ');
			if( m < nr )
				printf("%.2x", temp[m]);
			else
				printf("%s", fault_chars[fault_type]);
		}
		for(; m < 16; ++m )
			printf("   ");
		printf("  |");
		for( m = 0; m < r; ++m ){
			if( m < nr ){
				c = temp[m];
				putchar(' ' <= c && c <= '~'? c: '.');
			} else
				putchar(' ');
		}
		n -= r;
		for(; m < 16; ++m )
			putchar(' ');
		printf("|\n");
		if( nr < r )
			break;
	}
}

int
ppc_inst_dump(unsigned adr, int count)
{
	int nr, dotted;
	unsigned first_adr;
	unsigned long inst, last_inst;
	unsigned char val[4];

	dotted = 0;
	for (first_adr = adr; count > 0; --count, adr += 4){
		nr = mread(adr, val, 4);
		if( nr == 0 ){
			const char *x = fault_chars[fault_type];
			printf("%.8x  %s%s%s%s\n", adr, x, x, x, x);
			break;
		}
		inst = GETWORD(val);
		if (adr > first_adr && inst == last_inst) {
			if (!dotted) {
				printf(" ...\n");
				dotted = 1;
			}
			continue;
		}
		dotted = 0;
		last_inst = inst;
		printf("%.8x  ", adr);
		printf("%.8x\t", inst);
		print_insn_big_powerpc(stdout, inst, adr);	/* always returns 4 */
		printf("\n");
	}
	return adr - first_adr;
}

void
print_address(addr)
unsigned addr;
{
	printf("0x%x", addr);
}

/*
 * Memory operations - move, set, print differences
 */
static unsigned mdest;		/* destination address */
static unsigned msrc;		/* source address */
static unsigned mval;		/* byte value to set memory to */
static unsigned mcount;		/* # bytes to affect */
static unsigned mdiffs;		/* max # differences to print */

void
memops(int cmd)
{
	scanhex(&mdest);
	if( termch != '\n' )
		termch = 0;
	scanhex(cmd == 's'? &mval: &msrc);
	if( termch != '\n' )
		termch = 0;
	scanhex(&mcount);
	switch( cmd ){
	case 'm':
		memmove((void *)mdest, (void *)msrc, mcount);
		break;
	case 's':
		memset((void *)mdest, mval, mcount);
		break;
	case 'd':
		if( termch != '\n' )
			termch = 0;
		scanhex(&mdiffs);
		memdiffs((unsigned char *)mdest, (unsigned char *)msrc, mcount, mdiffs);
		break;
	}
}

void
memdiffs(unsigned char *p1, unsigned char *p2, unsigned nb, unsigned maxpr)
{
	unsigned n, prt;

	prt = 0;
	for( n = nb; n > 0; --n )
		if( *p1++ != *p2++ )
			if( ++prt <= maxpr )
				printf("%.8x %.2x # %.8x %.2x\n", (unsigned)p1 - 1,
					p1[-1], (unsigned)p2 - 1, p2[-1]);
	if( prt > maxpr )
		printf("Total of %d differences\n", prt);
}

static unsigned mend;
static unsigned mask;

void
memlocate()
{
	unsigned a, n;
	unsigned char val[4];

	last_cmd = "ml";
	scanhex(&mdest);
	if (termch != '\n') {
		termch = 0;
		scanhex(&mend);
		if (termch != '\n') {
			termch = 0;
			scanhex(&mval);
			mask = ~0;
			if (termch != '\n') termch = 0;
			scanhex(&mask);
		}
	}
	n = 0;
	for (a = mdest; a < mend; a += 4) {
		if (mread(a, val, 4) == 4
			&& ((GETWORD(val) ^ mval) & mask) == 0) {
			printf("%.8x:  %.8x\n", a, GETWORD(val));
			if (++n >= 10)
				break;
		}
	}
}

static unsigned mskip = 0x1000;
static unsigned mlim = 0xffffffff;

void
memzcan()
{
	unsigned char v;
	unsigned a;
	int ok, ook;

	scanhex(&mdest);
	if (termch != '\n') termch = 0;
	scanhex(&mskip);
	if (termch != '\n') termch = 0;
	scanhex(&mlim);
	ook = 0;
	for (a = mdest; a < mlim; a += mskip) {
		ok = mread(a, &v, 1);
		if (ok && !ook) {
			printf("%.8x .. ", a);
			fflush(stdout);
		} else if (!ok && ook)
			printf("%.8x\n", a - mskip);
		ook = ok;
		if (a + mskip < a)
			break;
	}
	if (ook)
		printf("%.8x\n", a - mskip);
}

/* Input scanning routines */
int
skipbl()
{
	int c;

	if( termch != 0 ){
		c = termch;
		termch = 0;
	} else
		c = inchar();
	while( c == ' ' || c == '\t' )
		c = inchar();
	return c;
}

int
scanhex(vp)
unsigned *vp;
{
	int c, d;
	unsigned v;

	c = skipbl();
	d = hexdigit(c);
	if( d == EOF ){
		termch = c;
		return 0;
	}
	v = 0;
	do {
		v = (v << 4) + d;
		c = inchar();
		d = hexdigit(c);
	} while( d != EOF );
	termch = c;
	*vp = v;
	return 1;
}

void
scannl()
{
	int c;

	c = termch;
	termch = 0;
	while( c != '\n' )
		c = inchar();
}

int
hexdigit(c)
{
	if( '0' <= c && c <= '9' )
		return c - '0';
	if( 'A' <= c && c <= 'F' )
		return c - ('A' - 10);
	if( 'a' <= c && c <= 'f' )
		return c - ('a' - 10);
	return EOF;
}

void
getstring(char *s, int size)
{
	int c;

	c = skipbl();
	do {
		if( size > 1 ){
			*s++ = c;
			--size;
		}
		c = inchar();
	} while( c != ' ' && c != '\t' && c != '\n' );
	termch = c;
	*s = 0;
}

static char line[256];
static char *lineptr;

void
flush_input()
{
	lineptr = NULL;
}

int
inchar()
{
	if (lineptr == NULL || *lineptr == 0) {
		if (fgets(line, sizeof(line), stdin) == NULL) {
			lineptr = NULL;
			return EOF;
		}
		lineptr = line;
	}
	return *lineptr++;
}

void
take_input(str)
char *str;
{
	lineptr = str;
}

/*
 * We use this array a lot here.  We assume we don't have multiple
 * instances of xmon running and that we don't use the return value of
 * any functions other than printing them.
 *  -- Cort
 */
char last[64];
static char *pretty_lookup_name(unsigned long addr)
{
	if ( lookup_name(addr) )
	{
		sprintf(last, " (%s)", lookup_name(addr));
		return last;
	}
	else
		return NULL;
}


static char *lookup_name(unsigned long addr)
{
	extern char *sysmap;
	extern unsigned long sysmap_size;
	char *c = sysmap;
	unsigned long cmp;

	if ( !sysmap || !sysmap_size )
		return NULL;
	
	/* adjust if addr is relative to kernelbase */
	if ( addr < PAGE_OFFSET )
		addr += PAGE_OFFSET;

	cmp = simple_strtoul(c, &c, 8);
	strcpy( last, strsep( &c, "\n"));
	while ( c < (sysmap+sysmap_size) )
	{
		cmp = simple_strtoul(c, &c, 8);
		if ( cmp < addr )
			break;
		strcpy( last, strsep( &c, "\n"));
	}
	return last;
}

