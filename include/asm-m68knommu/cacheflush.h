#ifndef _M68KNOMMU_CACHEFLUSH_H
#define _M68KNOMMU_CACHEFLUSH_H

/*
 * (C) Copyright 2000-2002, Greg Ungerer <gerg@snapgear.com>
 */

#include <asm/io.h>

/*
 * Cache handling functions
 */

#define flush_cache_all() __flush_cache_all()

extern inline void __flush_cache_all(void)
{
#ifdef CONFIG_M5407
	__asm__ __volatile__ (
		"nop\n\t"
		"clrl	%%d0\n\t"
		"1:\n\t"
		"movel	%%d0,%%a0\n\t"
		"2:\n\t"
		".word	0xf4e8\n\t"
		"addl	#0x10,%%a0\n\t"
		"cmpl	#0x00000800,%%a0\n\t"
		"blt	2b\n\t"
		"addql	#1,%%d0\n\t"
		"cmpil	#4,%%d0\n\t"
		"bne	1b\n\t"
		"movel  #0x01040100,%%d0\n\t"
		"movec  %%d0,%%CACR\n\t"
		"nop\n\t"
		"movel  #0x86088400,%%d0\n\t"
		"movec  %%d0,%%CACR\n\t"
		: : : "d0", "a0" );
#endif /* CONFIG_M5407 */
#ifdef CONFIG_M5272
	__asm__ __volatile__ (
        	"movel	#0x01000000, %%d0\n\t"
        	"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
        	"movel	#0x80000100, %%d0\n\t"
        	"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M5272 */
#if 0 /* CONFIG_M5249 */
	__asm__ __volatile__ (
        	"movel	#0x01000000, %%d0\n\t"
        	"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
        	"movel	#0xa0000200, %%d0\n\t"
        	"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M5249 */
}

/*
 *	FIXME: we could do better than an entire flush in most cases.
 *	But this will always work :-)
 */
#define	flush_cache_all()		__flush_cache_all()
#define	flush_cache_mm(mm)		__flush_cache_all()
#define	flush_cache_range(vma,a,b)	__flush_cache_all()
#define	flush_cache_page(vma,p)		__flush_cache_all()
#define	flush_page_to_ram(page)		__flush_cache_all()
#define	flush_dcache_page(page)		__flush_cache_all()
#define	flush_icache()			__flush_cache_all()
#define	flush_icache_page(page)		__flush_cache_all()
#define	flush_icache_range(start,len)	__flush_cache_all()
#define	cache_push_v(vaddr,len)		__flush_cache_all()
#define	cache_push(paddr,len)		__flush_cache_all()
#define	cache_clear(paddr,len)		__flush_cache_all()

#define	flush_dcache_range(a,b)

#define	flush_icache_user_range(vma,page,addr,len)	__flush_cache_all()

#endif /* _M68KNOMMU_CACHEFLUSH_H */