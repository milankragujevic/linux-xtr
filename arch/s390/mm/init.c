/*
 *  arch/s390/mm/init.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/pagemap.h>
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/lowcore.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

mmu_gather_t mmu_gathers[NR_CPUS];

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__((__aligned__(PAGE_SIZE)));
char  empty_zero_page[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

void show_mem(void)
{
        int i, total = 0, reserved = 0;
        int shared = 0, cached = 0;

        printk("Mem-info:\n");
        show_free_areas();
        printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
        i = max_mapnr;
        while (i-- > 0) {
                total++;
                if (PageReserved(mem_map+i))
                        reserved++;
                else if (PageSwapCache(mem_map+i))
                        cached++;
                else if (page_count(mem_map+i))
                        shared += atomic_read(&mem_map[i].count) - 1;
        }
        printk("%d pages of RAM\n",total);
        printk("%d reserved pages\n",reserved);
        printk("%d pages shared\n",shared);
        printk("%d pages swap cached\n",cached);
}

/* References to section boundaries */

extern unsigned long _text;
extern unsigned long _etext;
extern unsigned long _edata;
extern unsigned long __bss_start;
extern unsigned long _end;

extern unsigned long __init_begin;
extern unsigned long __init_end;

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 * paging_init will erase this initial mapping
 */

unsigned long last_valid_pfn;

void __init paging_init(void)
{
        pgd_t * pg_dir;
        pte_t * pg_table;
        pte_t   pte;
	int     i;
        unsigned long tmp;
        unsigned long pfn = 0;
        unsigned long pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERNSEG_TABLE;
        static const int ssm_mask = 0x04000000L;

	/* unmap whole virtual address space */

        pg_dir = swapper_pg_dir;

	for (i=0;i<KERNEL_PGD_PTRS;i++) 
	        pmd_clear((pmd_t*)pg_dir++);

	/*
	 * map whole physical memory to virtual memory (identity mapping) 
	 */

        pg_dir = swapper_pg_dir;

        while (pfn < max_low_pfn) {
                /*
                 * pg_table is physical at this point
                 */
		pg_table = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);

                pg_dir->pgd0 =  (_PAGE_TABLE | __pa(pg_table));
                pg_dir->pgd1 =  (_PAGE_TABLE | (__pa(pg_table)+1024));
                pg_dir->pgd2 =  (_PAGE_TABLE | (__pa(pg_table)+2048));
                pg_dir->pgd3 =  (_PAGE_TABLE | (__pa(pg_table)+3072));
                pg_dir++;

                for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
                        pte = pfn_pte(pfn, PAGE_KERNEL);
                        if (pfn >= max_low_pfn)
                                pte_clear(&pte);
                        set_pte(pg_table, pte);
                        pfn++;
                }
        }

        /* enable virtual mapping in kernel mode */
        __asm__ __volatile__("    LCTL  1,1,%0\n"
                             "    LCTL  7,7,%0\n"
                             "    LCTL  13,13,%0\n"
                             "    SSM   %1" 
			     : : "m" (pgdir_k), "m" (ssm_mask));

        local_flush_tlb();

	{
		unsigned long zones_size[MAX_NR_ZONES] = { 0, 0, 0};

		zones_size[ZONE_DMA] = max_low_pfn;
		free_area_init(zones_size);
	}

        return;
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;

        max_mapnr = num_physpages = max_low_pfn;
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

        /* clear the zero-page */
        memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

	reservedpages = 0;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;
        printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
                (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
                max_mapnr << (PAGE_SHIFT-10),
                codesize >> 10,
                reservedpages << (PAGE_SHIFT-10),
                datasize >>10,
                initsize >> 10);
}

void free_initmem(void)
{
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		totalram_pages++;
        }
        printk ("Freeing unused kernel memory: %dk freed\n",
		(&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
        if (start < end)
                printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
        for (; start < end; start += PAGE_SIZE) {
                ClearPageReserved(virt_to_page(start));
                set_page_count(virt_to_page(start), 1);
                free_page(start);
                totalram_pages++;
        }
}
#endif
