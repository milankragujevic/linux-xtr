#ifndef __PPC_ELF_H
#define __PPC_ELF_H

/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>

#define ELF_NGREG	48	/* includes nip, msr, lr, etc. */
#define ELF_NFPREG	33	/* includes fpscr */

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_PPC)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_PPC
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB;

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (2 * TASK_SIZE / 3)

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define ELF_CORE_COPY_REGS(gregs, regs) \
	memcpy(gregs, regs, \
	       sizeof(struct pt_regs) < sizeof(elf_gregset_t)? \
	       sizeof(struct pt_regs): sizeof(elf_gregset_t));

#endif
