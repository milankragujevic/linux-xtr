/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1999 by Ralf Baechle
 * Copyright (C) 1996, 1999 Silicon Graphics, Inc.
 *
 * Definitions commonly used in SGI style code.
 */
#ifndef _ASM_SGIDEFS_H
#define _ASM_SGIDEFS_H

/*
 * Definitions for the ISA level
 */
#define _MIPS_ISA_MIPS1 1
#define _MIPS_ISA_MIPS2 2
#define _MIPS_ISA_MIPS3 3
#define _MIPS_ISA_MIPS4 4
#define _MIPS_ISA_MIPS5 5

/*
 * Subprogram calling convention
 *
 * At the moment only _MIPS_SIM_ABI32 is in use.  This will change rsn.
 * Until GCC 2.8.0 is released don't rely on this definitions because the
 * 64bit code is essentially using the 32bit interface model just with
 * 64bit registers.
 */
#define _MIPS_SIM_ABI32		1
#define _MIPS_SIM_NABI32	2
#define _MIPS_SIM_ABI64		3

#endif /* _ASM_SGIDEFS_H */
