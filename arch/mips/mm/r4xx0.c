/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * r4xx0.c: R4000 processor variant specific MMU/Cache routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle ralf@gnu.org
 *
 * To do:
 *
 *  - this code is a overbloated pig
 *  - many of the bug workarounds are not efficient at all, but at
 *    least they are functional ...
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/bcache.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance. */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "nop; nop; nop; nop; nop; nop;\n\t" \
				     ".set reorder\n\t")

/* Primary cache parameters. */
static int icache_size, dcache_size; /* Size in bytes */
static int ic_lsize, dc_lsize;       /* LineSize in bytes */

/* Secondary cache (if present) parameters. */
static unsigned int scache_size, sc_lsize;	/* Again, in bytes */

#include <asm/cacheops.h>
#include <asm/r4kcache.h>

#undef DEBUG_CACHE

/*
 * Dummy cache handling routines for machines without boardcaches
 */
static void no_sc_noop(void) {}

static struct bcache_ops no_sc_ops = {
	(void *)no_sc_noop, (void *)no_sc_noop,
	(void *)no_sc_noop, (void *)no_sc_noop
};

struct bcache_ops *bcops = &no_sc_ops;

/*
 * On processors with QED R4600 style two set assosicative cache
 * this is the bit which selects the way in the cache for the
 * indexed cachops.
 */
#define icache_waybit (icache_size >> 1)
#define dcache_waybit (dcache_size >> 1)

/*
 * If you think for one second that this stuff coming up is a lot
 * of bulky code eating too many kernel cache lines.  Think _again_.
 *
 * Consider:
 * 1) Taken branches have a 3 cycle penalty on R4k
 * 2) The branch itself is a real dead cycle on even R4600/R5000.
 * 3) Only one of the following variants of each type is even used by
 *    the kernel based upon the cache parameters we detect at boot time.
 *
 * QED.
 */

static inline void r4k_flush_cache_all_s16d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache16();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s32d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache32();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s64d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache64();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s128d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache128();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s32d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache32();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s64d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache64();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s128d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache128();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32();
	__restore_flags(flags);
}

static void
r4k_flush_cache_range_s16d16i16(struct mm_struct *mm,
                                unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s16d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache16_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s32d16i16(struct mm_struct *mm,
                                unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s32d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache32_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_s64d16i16(struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s64d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache64_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_s128d16i16(struct mm_struct *mm,
					     unsigned long start,
					     unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s128d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache128_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_s32d32i32(struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s32d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache32_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_s64d32i32(struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s64d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache64_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_s128d32i32(struct mm_struct *mm,
					     unsigned long start,
					     unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (vma) {
		if (mm->context != current->active_mm->context) {
			r4k_flush_cache_all_s128d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache128_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void r4k_flush_cache_range_d16i16(struct mm_struct *mm,
					 unsigned long start,
					 unsigned long end)
{
	if (mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		__save_and_cli(flags);
		blast_dcache16(); blast_icache16();
		__restore_flags(flags);
	}
}

static void r4k_flush_cache_range_d32i32(struct mm_struct *mm,
					 unsigned long start,
					 unsigned long end)
{
	if (mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		__save_and_cli(flags);
		blast_dcache32(); blast_icache32();
		__restore_flags(flags);
	}
}

/*
 * On architectures like the Sparc, we could get rid of lines in
 * the cache created only by a certain context, but on the MIPS
 * (and actually certain Sparc's) we cannot.
 */
static void r4k_flush_cache_mm_s16d16i16(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s16d16i16();
	}
}

static void r4k_flush_cache_mm_s32d16i16(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s32d16i16();
	}
}

static void r4k_flush_cache_mm_s64d16i16(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s64d16i16();
	}
}

static void r4k_flush_cache_mm_s128d16i16(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s128d16i16();
	}
}

static void r4k_flush_cache_mm_s32d32i32(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s32d32i32();
	}
}

static void r4k_flush_cache_mm_s64d32i32(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s64d32i32();
	}
}

static void r4k_flush_cache_mm_s128d32i32(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s128d32i32();
	}
}

static void r4k_flush_cache_mm_d16i16(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_d16i16();
	}
}

static void r4k_flush_cache_mm_d32i32(struct mm_struct *mm)
{
	if (mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_d32i32();
	}
}

static void r4k_flush_cache_page_s16d16i16(struct vm_area_struct *vma,
					   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache16_page_indexed(page);
	} else
		blast_scache16_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s32d16i16(struct vm_area_struct *vma,
					   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache32_page_indexed(page);
	} else
		blast_scache32_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s64d16i16(struct vm_area_struct *vma,
					   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache64_page_indexed(page);
	} else
		blast_scache64_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s128d16i16(struct vm_area_struct *vma,
					    unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache128_page_indexed(page);
	} else
		blast_scache128_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s32d32i32(struct vm_area_struct *vma,
					   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache32_page_indexed(page);
	} else
		blast_scache32_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s64d32i32(struct vm_area_struct *vma,
					   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache64_page_indexed(page);
	} else
		blast_scache64_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_s128d32i32(struct vm_area_struct *vma,
					    unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache128_page_indexed(page);
	} else
		blast_scache128_page(page);
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_d16i16(struct vm_area_struct *vma,
					unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm == current->active_mm) {
		blast_dcache16_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache16_page_indexed(page);
	}
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_d32i32(struct vm_area_struct *vma,
					unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_PRESENT))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if ((mm == current->active_mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		blast_dcache32_page(page);
	} else {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache32_page_indexed(page);
	}
out:
	__restore_flags(flags);
}

static void r4k_flush_cache_page_d32i32_r4600(struct vm_area_struct *vma,
					      unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_PRESENT))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if ((mm == current->active_mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		blast_dcache32_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_dcache32_page_indexed(page ^ dcache_waybit);
	}
out:
	__restore_flags(flags);
}

/* If the addresses passed to these routines are valid, they are
 * either:
 *
 * 1) In KSEG0, so we can do a direct flush of the page.
 * 2) In KSEG2, and since every process can translate those
 *    addresses all the time in kernel mode we can do a direct
 *    flush.
 * 3) In KSEG1, no flush necessary.
 */
static void r4k_flush_page_to_ram_s16(struct page *page)
{
	blast_scache16_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s32(struct page *page)
{
	blast_scache32_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s64(struct page *page)
{
	blast_scache64_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s128(struct page *page)
{
	blast_scache128_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_d16(struct page *page)
{
	blast_dcache16_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_d32(struct page *page)
{
	blast_dcache32_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_d32_r4600(struct page *page)
{
	unsigned long flags;

	__save_and_cli(flags);			/* For R4600 v1.7 bug.  */
	blast_dcache32_page((unsigned long)page_address(page));
	__restore_flags(flags);
}

static void
r4k_flush_icache_page_s(struct vm_area_struct *vma, struct page *page)
{
	/*
	 * We did an scache flush therefore PI is already clean.
	 */
}

static void
r4k_flush_icache_range(unsigned long start, unsigned long end)
{
	flush_cache_all();
}

/*
 * Ok, this seriously sucks.  We use them to flush a user page but don't
 * know the virtual address, so we have to blast away the whole icache
 * which is significantly more expensive than the real thing.
 */
static void
r4k_flush_icache_page_p(struct vm_area_struct *vma, struct page *page)
{
	if (!(vma->vm_flags & VM_EXEC))
		return;

	flush_cache_all();
}

/*
 * Writeback and invalidate the primary cache dcache before DMA.
 *
 * R4600 v2.0 bug: "The CACHE instructions Hit_Writeback_Inv_D,
 * Hit_Writeback_D, Hit_Invalidate_D and Create_Dirty_Exclusive_D will only
 * operate correctly if the internal data cache refill buffer is empty.  These
 * CACHE instructions should be separated from any potential data cache miss
 * by a load instruction to an uncached address to empty the response buffer."
 * (Revision 2.0 device errata from IDT available on http://www.idt.com/
 * in .pdf format.)
 */
static void
r4k_dma_cache_wback_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
		/* Workaround for R4600 bug.  See comment above. */
		__save_and_cli(flags);
		*(volatile unsigned long *)KSEG1;

		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}
	bc_wback_inv(addr, size);
}

static void
r4k_dma_cache_wback_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_all();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a);	/* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
r4k_dma_cache_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
		/* Workaround for R4600 bug.  See comment above. */
		__save_and_cli(flags);
		*(volatile unsigned long *)KSEG1;

		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}

	bc_inv(addr, size);
}

static void
r4k_dma_cache_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_all();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a); /* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
r4k_dma_cache_wback(unsigned long addr, unsigned long size)
{
	panic("r4k_dma_cache called - should not happen.\n");
}

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void r4k_flush_cache_sigtramp(unsigned long addr)
{
	__asm__ __volatile__("nop;nop;nop;nop");	/* R4600 V1.7 */
	protected_writeback_dcache_line(addr & ~(dc_lsize - 1));
	protected_flush_icache_line(addr & ~(ic_lsize - 1));
}

static void r4600v20k_flush_cache_sigtramp(unsigned long addr)
{
	unsigned int flags;

	__save_and_cli(flags);

	/* Clear internal cache refill buffer */
	*(volatile unsigned int *)KSEG1;

	protected_writeback_dcache_line(addr & ~(dc_lsize - 1));
	protected_flush_icache_line(addr & ~(ic_lsize - 1));

	__restore_flags(flags);
}

#undef DEBUG_TLB
#undef DEBUG_TLBUPDATE

void flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = (get_entryhi() & 0xff);
	set_entryhi(KSEG0);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while(entry < mips_cpu.tlbsize) {
		set_index(entry);
		BARRIER;
		tlb_write_indexed();
		BARRIER;
		entry++;
	}
	BARRIER;
	set_entryhi(old_ctx);
	__restore_flags(flags);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		__save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
		if (mm == current->active_mm)
			set_entryhi(mm->context & 0xff);
		__restore_flags(flags);
	}
}

void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xff),
		       start, end);
#endif
		__save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if(size <= mips_cpu.tlbsize/2) {
			int oldpid = (get_entryhi() & 0xff);
			int newpid = (mm->context & 0xff);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				BARRIER;
				tlb_probe();
				BARRIER;
				idx = get_index();
				set_entrylo0(0);
				set_entrylo1(0);
				set_entryhi(KSEG0);
				BARRIER;
				if(idx < 0)
					continue;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
		} else {
			get_new_mmu_context(mm, asid_cache);
			if (mm == current->active_mm)
				set_entryhi(mm->context & 0xff);
		}
		__restore_flags(flags);
	}
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (vma->vm_mm->context & 0xff);
		page &= (PAGE_MASK << 1);
		__save_and_cli(flags);
		oldpid = (get_entryhi() & 0xff);
		set_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		BARRIER;
		idx = get_index();
		set_entrylo0(0);
		set_entrylo1(0);
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
		BARRIER;
		tlb_write_indexed();

	finish:
		BARRIER;
		set_entryhi(oldpid);
		__restore_flags(flags);
	}
}

void pgd_init(unsigned long page)
{
	unsigned long *p = (unsigned long *) page;
	int i;

	for(i = 0; i < USER_PTRS_PER_PGD; i+=8) {
		p[i + 0] = (unsigned long) invalid_pte_table;
		p[i + 1] = (unsigned long) invalid_pte_table;
		p[i + 2] = (unsigned long) invalid_pte_table;
		p[i + 3] = (unsigned long) invalid_pte_table;
		p[i + 4] = (unsigned long) invalid_pte_table;
		p[i + 5] = (unsigned long) invalid_pte_table;
		p[i + 6] = (unsigned long) invalid_pte_table;
		p[i + 7] = (unsigned long) invalid_pte_table;
	}
}

/* We will need multiple versions of update_mmu_cache(), one that just
 * updates the TLB with the new pte(s), and another which also checks
 * for the R4k "end of page" hardware bug and does the needy.
 */
void update_mmu_cache(struct vm_area_struct * vma,
				 unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	pid = get_entryhi() & 0xff;

#ifdef DEBUG_TLB
	if((pid != (vma->vm_mm->context & 0xff)) || (vma->vm_mm->context == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d tlbpid=%d\n",
		       (int) (vma->vm_mm->context & 0xff), pid);
	}
#endif

	__save_and_cli(flags);
	address &= (PAGE_MASK << 1);
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	BARRIER;
	tlb_probe();
	BARRIER;
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	BARRIER;
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	set_entryhi(address | (pid));
	BARRIER;
	if(idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	BARRIER;
	set_entryhi(pid);
	BARRIER;
	__restore_flags(flags);
}

#if 0
static void r4k_update_mmu_cache_hwbug(struct vm_area_struct * vma,
				       unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx;

	__save_and_cli(flags);
	address &= (PAGE_MASK << 1);
	set_entryhi(address | (get_entryhi() & 0xff));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	BARRIER;
	if(idx < 0)
		tlb_write_random();
	else
		tlb_write_indexed();
	BARRIER;
	__restore_flags(flags);
}
#endif

void show_regs(struct pt_regs * regs)
{
	/* Saved main processor registers. */
	printk("$0 : %08lx %08lx %08lx %08lx\n",
	       0UL, regs->regs[1], regs->regs[2], regs->regs[3]);
	printk("$4 : %08lx %08lx %08lx %08lx\n",
               regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	printk("$8 : %08lx %08lx %08lx %08lx\n",
	       regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	printk("$12: %08lx %08lx %08lx %08lx\n",
               regs->regs[12], regs->regs[13], regs->regs[14], regs->regs[15]);
	printk("$16: %08lx %08lx %08lx %08lx\n",
	       regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	printk("$20: %08lx %08lx %08lx %08lx\n",
               regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	printk("$24: %08lx %08lx\n",
	       regs->regs[24], regs->regs[25]);
	printk("$28: %08lx %08lx %08lx %08lx\n",
	       regs->regs[28], regs->regs[29], regs->regs[30], regs->regs[31]);

	/* Saved cp0 registers. */
	printk("epc   : %08lx\nStatus: %08lx\nCause : %08lx\n",
	       regs->cp0_epc, regs->cp0_status, regs->cp0_cause);
}
			
void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				      unsigned long entryhi, unsigned long pagemask)
{
        unsigned long flags;
        unsigned long wired;
        unsigned long old_pagemask;
        unsigned long old_ctx;

        __save_and_cli(flags);
        /* Save old context and create impossible VPN2 value */
        old_ctx = (get_entryhi() & 0xff);
        old_pagemask = get_pagemask();
        wired = get_wired();
        set_wired (wired + 1);
        set_index (wired);
        BARRIER;    
        set_pagemask (pagemask);
        set_entryhi(entryhi);
        set_entrylo0(entrylo0);
        set_entrylo1(entrylo1);
        BARRIER;    
        tlb_write_indexed();
        BARRIER;    
    
        set_entryhi(old_ctx);
        BARRIER;    
        set_pagemask (old_pagemask);
        flush_tlb_all();    
        __restore_flags(flags);
}

/* Detect and size the various r4k caches. */
static void __init probe_icache(unsigned long config)
{
        switch (mips_cpu.cputype) {
        case CPU_VR41XX:
                icache_size = 1 << (10 + ((config >> 9) & 7));
                break;
        default:
                icache_size = 1 << (12 + ((config >> 9) & 7));
                break;
        }
	ic_lsize = 16 << ((config >> 5) & 1);

	printk("Primary instruction cache %dkb, linesize %d bytes.\n",
	       icache_size >> 10, ic_lsize);
}

static void __init probe_dcache(unsigned long config)
{
        switch (mips_cpu.cputype) {
        case CPU_VR41XX:
                dcache_size = 1 << (10 + ((config >> 6) & 7));
                break;
        default:
                dcache_size = 1 << (12 + ((config >> 6) & 7));
                break;
        }
	dc_lsize = 16 << ((config >> 4) & 1);

	printk("Primary data cache %dkb, linesize %d bytes.\n",
	       dcache_size >> 10, dc_lsize);
}


/* If you even _breathe_ on this function, look at the gcc output
 * and make sure it does not pop things on and off the stack for
 * the cache sizing loop that executes in KSEG1 space or else
 * you will crash and burn badly.  You have been warned.
 */
static int __init probe_scache(unsigned long config)
{
	extern unsigned long stext;
	unsigned long flags, addr, begin, end, pow2;
	int tmp;

	tmp = ((config >> 17) & 1);
	if(tmp)
		return 0;
	tmp = ((config >> 22) & 3);
	switch(tmp) {
	case 0:
		sc_lsize = 16;
		break;
	case 1:
		sc_lsize = 32;
		break;
	case 2:
		sc_lsize = 64;
		break;
	case 3:
		sc_lsize = 128;
		break;
	}

	begin = (unsigned long) &stext;
	begin &= ~((4 * 1024 * 1024) - 1);
	end = begin + (4 * 1024 * 1024);

	/* This is such a bitch, you'd think they would make it
	 * easy to do this.  Away you daemons of stupidity!
	 */
	__save_and_cli(flags);

	/* Fill each size-multiple cache line with a valid tag. */
	pow2 = (64 * 1024);
	for(addr = begin; addr < end; addr = (begin + pow2)) {
		unsigned long *p = (unsigned long *) addr;
		__asm__ __volatile__("nop" : : "r" (*p)); /* whee... */
		pow2 <<= 1;
	}

	/* Load first line with zero (therefore invalid) tag. */
	set_taglo(0);
	set_taghi(0);
	__asm__ __volatile__("nop; nop; nop; nop;"); /* avoid the hazard */
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 8, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 9, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 11, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));

	/* Now search for the wrap around point. */
	pow2 = (128 * 1024);
	tmp = 0;
	for(addr = (begin + (128 * 1024)); addr < (end); addr = (begin + pow2)) {
		__asm__ __volatile__("\n\t.set noreorder\n\t"
				     ".set mips3\n\t"
				     "cache 7, (%0)\n\t"
				     ".set mips0\n\t"
				     ".set reorder\n\t" : : "r" (addr));
		__asm__ __volatile__("nop; nop; nop; nop;"); /* hazard... */
		if(!get_taglo())
			break;
		pow2 <<= 1;
	}
	__restore_flags(flags);
	addr -= begin;
	printk("Secondary cache sized at %dK linesize %d bytes.\n",
	       (int) (addr >> 10), sc_lsize);
	scache_size = addr;
	return 1;
}

static void __init setup_noscache_funcs(void)
{
	unsigned int prid;

	switch(dc_lsize) {
	case 16:
		_clear_page = r4k_clear_page_d16;
		_copy_page = r4k_copy_page_d16;
		_flush_cache_all = r4k_flush_cache_all_d16i16;
		_flush_cache_mm = r4k_flush_cache_mm_d16i16;
		_flush_cache_range = r4k_flush_cache_range_d16i16;
		_flush_cache_page = r4k_flush_cache_page_d16i16;
		_flush_page_to_ram = r4k_flush_page_to_ram_d16;
		break;
	case 32:
		prid = read_32bit_cp0_register(CP0_PRID) & 0xfff0;
		if (prid == 0x2010) {			/* R4600 V1.7 */
			_clear_page = r4k_clear_page_r4600_v1;
			_copy_page = r4k_copy_page_r4600_v1;
			_flush_page_to_ram = r4k_flush_page_to_ram_d32_r4600;
		} else if (prid == 0x2020) {		/* R4600 V2.0 */
			_clear_page = r4k_clear_page_r4600_v2;
			_copy_page = r4k_copy_page_r4600_v2;
			_flush_page_to_ram = r4k_flush_page_to_ram_d32;
		} else {
			_clear_page = r4k_clear_page_d32;
			_copy_page = r4k_copy_page_d32;
			_flush_page_to_ram = r4k_flush_page_to_ram_d32;
		}
		_flush_cache_all = r4k_flush_cache_all_d32i32;
		_flush_cache_mm = r4k_flush_cache_mm_d32i32;
		_flush_cache_range = r4k_flush_cache_range_d32i32;
		_flush_cache_page = r4k_flush_cache_page_d32i32;
		break;
	}
	___flush_cache_all = _flush_cache_all;

	_flush_icache_page = r4k_flush_icache_page_p;

	_dma_cache_wback_inv = r4k_dma_cache_wback_inv_pc;
	_dma_cache_wback = r4k_dma_cache_wback;
	_dma_cache_inv = r4k_dma_cache_inv_pc;
}

static void __init setup_scache_funcs(void)
{
	switch(sc_lsize) {
	case 16:
		switch(dc_lsize) {
		case 16:
			_flush_cache_all = r4k_flush_cache_all_s16d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s16d16i16;
			_flush_cache_range = r4k_flush_cache_range_s16d16i16;
			_flush_cache_page = r4k_flush_cache_page_s16d16i16;
			break;
		case 32:
			panic("Invalid cache configuration detected");
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s16;
		_clear_page = r4k_clear_page_s16;
		_copy_page = r4k_copy_page_s16;
		break;
	case 32:
		switch(dc_lsize) {
		case 16:
			_flush_cache_all = r4k_flush_cache_all_s32d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s32d16i16;
			_flush_cache_range = r4k_flush_cache_range_s32d16i16;
			_flush_cache_page = r4k_flush_cache_page_s32d16i16;
			break;
		case 32:
			_flush_cache_all = r4k_flush_cache_all_s32d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s32d32i32;
			_flush_cache_range = r4k_flush_cache_range_s32d32i32;
			_flush_cache_page = r4k_flush_cache_page_s32d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s32;
		_clear_page = r4k_clear_page_s32;
		_copy_page = r4k_copy_page_s32;
		break;
	case 64:
		switch(dc_lsize) {
		case 16:
			_flush_cache_all = r4k_flush_cache_all_s64d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s64d16i16;
			_flush_cache_range = r4k_flush_cache_range_s64d16i16;
			_flush_cache_page = r4k_flush_cache_page_s64d16i16;
			break;
		case 32:
			_flush_cache_all = r4k_flush_cache_all_s64d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s64d32i32;
			_flush_cache_range = r4k_flush_cache_range_s64d32i32;
			_flush_cache_page = r4k_flush_cache_page_s64d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s64;
		_clear_page = r4k_clear_page_s64;
		_copy_page = r4k_copy_page_s64;
		break;
	case 128:
		switch(dc_lsize) {
		case 16:
			_flush_cache_all = r4k_flush_cache_all_s128d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s128d16i16;
			_flush_cache_range = r4k_flush_cache_range_s128d16i16;
			_flush_cache_page = r4k_flush_cache_page_s128d16i16;
			break;
		case 32:
			_flush_cache_all = r4k_flush_cache_all_s128d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s128d32i32;
			_flush_cache_range = r4k_flush_cache_range_s128d32i32;
			_flush_cache_page = r4k_flush_cache_page_s128d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s128;
		_clear_page = r4k_clear_page_s128;
		_copy_page = r4k_copy_page_s128;
		break;
	}
	___flush_cache_all = _flush_cache_all;
	_flush_icache_page = r4k_flush_icache_page_s;
	_dma_cache_wback_inv = r4k_dma_cache_wback_inv_sc;
	_dma_cache_wback = r4k_dma_cache_wback;
	_dma_cache_inv = r4k_dma_cache_inv_sc;
}

typedef int (*probe_func_t)(unsigned long);

static inline void __init setup_scache(unsigned int config)
{
	probe_func_t probe_scache_kseg1;
	int sc_present = 0;

	/* Maybe the cpu knows about a l2 cache? */
	probe_scache_kseg1 = (probe_func_t) (KSEG1ADDR(&probe_scache));
	sc_present = probe_scache_kseg1(config);

	if (sc_present) {
		setup_scache_funcs();
		return;
	}

	setup_noscache_funcs();
}

void __init ld_mmu_r4xx0(void)
{
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

#ifdef CONFIG_MIPS_UNCACHED
	change_cp0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
#else
	change_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NONCOHERENT);
#endif

	probe_icache(config);
	probe_dcache(config);
	setup_scache(config);

	switch(mips_cpu.cputype) {
	case CPU_R4600:			/* QED style two way caches? */
	case CPU_R4700:
	case CPU_R5000:
	case CPU_NEVADA:
		_flush_cache_page = r4k_flush_cache_page_d32i32_r4600;
	}

	_flush_cache_sigtramp = r4k_flush_cache_sigtramp;
	_flush_icache_range = r4k_flush_icache_range;	/* Ouch */
	if ((read_32bit_cp0_register(CP0_PRID) & 0xfff0) == 0x2020) {
		_flush_cache_sigtramp = r4600v20k_flush_cache_sigtramp;
	}

	__flush_cache_all();
	write_32bit_cp0_register(CP0_WIRED, 0);

	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	set_pagemask(PM_4K);
	flush_tlb_all();
}
