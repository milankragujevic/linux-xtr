/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/ddb5477/pci_ops.c
 *     Define the pci_ops for DB5477.
 *
 * Much of the code is derived from the original DDB5074 port by 
 * Geert Uytterhoeven <geert@sonycom.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>

#include <asm/ddb5xxx/debug.h>
#include <asm/ddb5xxx/ddb5xxx.h>

/*
 * config_swap structure records what set of pdar/pmr are used
 * to access pci config space.  It also provides a place hold the
 * original values for future restoring.
 */
struct pci_config_swap {
	u32 	pdar;
	u32	pmr;
	u32	config_base;
	u32	config_size;
	u32	pdar_backup;
	u32	pmr_backup;
};

/*
 * On DDB5476, we have one set of swap registers
 */
struct pci_config_swap ext_pci_swap = {
	DDB_PCIW0,  
	DDB_PCIINIT0,
	DDB_PCI_CONFIG_BASE,
	DDB_PCI_CONFIG_SIZE
};

static int pci_config_workaround=1;

/*
 * access config space
 */	
static inline u32 ddb_access_config_base(struct pci_config_swap *swap,
					 u32 bus,/* 0 means top level bus */
					 u32 slot_num)
{
	u32 pci_addr = 0;
	u32 pciinit_offset = 0;
        u32 virt_addr = swap->config_base;
	u32 option;

	if (pci_config_workaround) {
	/* [jsun] work around Vrc5476 controller itself */
	if (slot_num == 12) slot_num = 0;

	/* BUG : skip P2P bridge for now */
	if (slot_num == 5) slot_num = 0;
	} else {
		if (slot_num == 12) return DDB_BASE + DDB_PCI_BASE;
	}

	/* minimum pdar (window) size is 2MB */
	MIPS_ASSERT(swap->config_size >= (2 << 20));

	MIPS_ASSERT(slot_num < (1 << 5));
	MIPS_ASSERT(bus < (1 << 8));

	/* backup registers */
	swap->pdar_backup = ddb_in32(swap->pdar);
	swap->pmr_backup = ddb_in32(swap->pmr);

	/* set the pdar (pci window) register */
	ddb_set_pdar(swap->pdar,
		     swap->config_base,
		     swap->config_size,
		     32,	/* 32 bit wide */
		     0,		/* not on local memory bus */
		     0);	/* not visible from PCI bus (N/A) */

	/* 
	 * calcuate the absolute pci config addr; 
	 * according to the spec, we start scanning from adr:11 (0x800)
	 */ 
	if (bus == 0) {
		/* type 0 config */
		pci_addr = 0x800 << slot_num;
	} else {
		/* type 1 config */
		pci_addr = (bus << 16) | (slot_num << 11);
		panic("ddb_access_config_base: we don't support type 1 config Yet");
	}

	/*
	 * if pci_addr is less than pci config window size,  we set
	 * pciinit_offset to 0 and adjust the virt_address.
	 * Otherwise we will try to adjust pciinit_offset.
	 */
	if (pci_addr < swap->config_size) {
		virt_addr = KSEG1ADDR(swap->config_base + pci_addr);
		pciinit_offset = 0;
	} else {
		MIPS_ASSERT( (pci_addr & (swap->config_size - 1)) == 0);
		virt_addr = KSEG1ADDR(swap->config_base);
		pciinit_offset = pci_addr;
	}

	/* set the pmr register */
	option = DDB_PCI_ACCESS_32;
	if (bus != 0) option |= DDB_PCI_CFGTYPE1;
	ddb_set_pmr(swap->pmr, DDB_PCICMD_CFG, pciinit_offset, option);

	return virt_addr;
}

static inline void ddb_close_config_base(struct pci_config_swap *swap)
{
	ddb_out32(swap->pdar, swap->pdar_backup);
	ddb_out32(swap->pmr, swap->pmr_backup);
}

static int read_config_dword(struct pci_config_swap *swap,
			     struct pci_dev *dev,
			     u32 where,
			     u32 *val)
{
	u32 bus, slot_num, func_num;
	u32 base;

	MIPS_ASSERT((where & 3) == 0);
	MIPS_ASSERT(where < (1 << 8));

	/* check if the bus is top-level */
	if (dev->bus->parent != NULL) {
		bus = dev->bus->number;
		MIPS_ASSERT(bus != 0);
	} else {
		bus = 0;
	}

	slot_num = PCI_SLOT(dev->devfn);
	func_num = PCI_FUNC(dev->devfn);
	base = ddb_access_config_base(swap, bus, slot_num);
	*val = *(volatile u32*) (base + (func_num << 8) + where);
	ddb_close_config_base(swap);
	return PCIBIOS_SUCCESSFUL;
}

static int read_config_word(struct pci_config_swap *swap,
			    struct pci_dev *dev,
			    u32 where,
			    u16 *val)
{
        int status;
        u32 result;

	MIPS_ASSERT((where & 1) == 0);

        status = read_config_dword(swap, dev, where & ~3, &result);
        if (where & 2) result >>= 16;
        *val = result & 0xffff;
        return status;
}

static int read_config_byte(struct pci_config_swap *swap,
			    struct pci_dev *dev,
			    u32 where,
			    u8 *val)
{
        int status;
        u32 result;

        status = read_config_dword(swap, dev, where & ~3, &result);
        if (where & 1) result >>= 8;
        if (where & 2) result >>= 16;
        *val = result & 0xff;
        return status;
}

static int write_config_dword(struct pci_config_swap *swap,
			      struct pci_dev *dev,
			      u32 where,
			      u32 val)
{
	u32 bus, slot_num, func_num;
	u32 base;

	MIPS_ASSERT((where & 3) == 0);
	MIPS_ASSERT(where < (1 << 8));

	/* check if the bus is top-level */
	if (dev->bus->parent != NULL) {
		bus = dev->bus->number;
		MIPS_ASSERT(bus != 0);
	} else {
		bus = 0;
	}

	slot_num = PCI_SLOT(dev->devfn);
	func_num = PCI_FUNC(dev->devfn);
	base = ddb_access_config_base(swap, bus, slot_num);
	*(volatile u32*) (base + (func_num << 8) + where) = val; 
	ddb_close_config_base(swap);
	return PCIBIOS_SUCCESSFUL;
}

static int write_config_word(struct pci_config_swap *swap,
			     struct pci_dev *dev,
			     u32 where,
			     u16 val)
{
	int status, shift=0;
	u32 result;

	MIPS_ASSERT((where & 1) == 0);

	status = read_config_dword(swap, dev, where & ~3, &result);
	if (status != PCIBIOS_SUCCESSFUL) return status;

        if (where & 2)
                shift += 16;
        result &= ~(0xffff << shift);
        result |= val << shift;
        return write_config_dword(swap, dev, where & ~3, result);
}

static int write_config_byte(struct pci_config_swap *swap,
			     struct pci_dev *dev,
			     u32 where,
			     u8 val)
{
	int status, shift=0;
	u32 result;

	status = read_config_dword(swap, dev, where & ~3, &result);
	if (status != PCIBIOS_SUCCESSFUL) return status;

        if (where & 2)
                shift += 16;
        if (where & 1)
                shift += 8;
        result &= ~(0xff << shift);
        result |= val << shift;
        return write_config_dword(swap, dev, where & ~3, result);
}

#define	MAKE_PCI_OPS(prefix, rw, unitname, unittype, pciswap) \
static int prefix##_##rw##_config_##unitname(struct pci_dev *dev, int where, unittype val) \
{ \
     return rw##_config_##unitname(pciswap, \
                                   dev, \
                                   where, \
                                   val); \
}

MAKE_PCI_OPS(extpci, read, byte, u8 *, &ext_pci_swap)
MAKE_PCI_OPS(extpci, read, word, u16 *, &ext_pci_swap)
MAKE_PCI_OPS(extpci, read, dword, u32 *, &ext_pci_swap)

MAKE_PCI_OPS(extpci, write, byte, u8, &ext_pci_swap)
MAKE_PCI_OPS(extpci, write, word, u16, &ext_pci_swap)
MAKE_PCI_OPS(extpci, write, dword, u32, &ext_pci_swap)

struct pci_ops ddb5476_ext_pci_ops ={
	extpci_read_config_byte,
	extpci_read_config_word,
	extpci_read_config_dword,
	extpci_write_config_byte,
	extpci_write_config_word,
	extpci_write_config_dword
};


#if defined(CONFIG_LL_DEBUG)
void jsun_scan_pci_bus(void)
{
	struct pci_bus bus;
	struct pci_dev dev;
	unsigned int devfn;
	int j;

	pci_config_workaround = 0;

	bus.parent = NULL;	/* we scan the top level only */
	dev.bus = &bus;
	dev.sysdata = NULL;

	/* scan ext pci bus and io pci bus*/
	for (j=0; j< 1; j++) {
		printk(KERN_INFO "scan ddb5476 external PCI bus:\n");
		bus.ops = &ddb5476_ext_pci_ops;
	
		for (devfn = 0; devfn < 0x100; devfn += 8) {
			u32 temp;
			u16 temp16;
			u8 temp8;
			int i;

			dev.devfn = devfn;
			MIPS_VERIFY(pci_read_config_dword(&dev, 0, &temp),
				    == PCIBIOS_SUCCESSFUL);
			if (temp == 0xffffffff) continue;

			printk(KERN_INFO "slot %d: (addr %d) \n", devfn/8,
			       11+devfn/8);

			/* verify read word and byte */
			MIPS_VERIFY(pci_read_config_word(&dev, 2, &temp16),
				    == PCIBIOS_SUCCESSFUL);
			MIPS_ASSERT(temp16 == (temp >> 16));
			MIPS_VERIFY(pci_read_config_byte(&dev, 3, &temp8),
				    == PCIBIOS_SUCCESSFUL);
			MIPS_ASSERT(temp8 == (temp >> 24));
			MIPS_VERIFY(pci_read_config_byte(&dev, 1, &temp8),
				    == PCIBIOS_SUCCESSFUL);
			MIPS_ASSERT(temp8 == ((temp >> 8) & 0xff));

			for (i=0; i < 16; i++) {
				if ((i%4) == 0)
					printk(KERN_INFO);
				MIPS_VERIFY(pci_read_config_dword(&dev, i*4, &temp),
					    == PCIBIOS_SUCCESSFUL);
				printk("\t%08X", temp);
				if ((i%4) == 3)
					printk("\n");
			}
		}
	}

	pci_config_workaround = 1;
}
#endif
