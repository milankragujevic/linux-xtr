/*
 *  linux/include/asm-i386/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *
 * $Id: keyboard.h,v 1.6 1998/10/28 12:40:06 ralf Exp $
 */

/*
 *  This file contains the i386 architecture specific keyboard definitions
 */

#ifndef _I386_KEYBOARD_H
#define _I386_KEYBOARD_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/io.h>

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_pretranslate	pckbd_pretranslate
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw
#define kbd_sysrq_xlate		pckbd_sysrq_xlate

#define SYSRQ_KEY 0x54

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { SLOW_DOWN_IO; } while(0)

/* Get the keyboard controller registers (incomplete decode) */
#define kbd_request_region() request_region(0x60, 16, "keyboard")

#define kbd_request_irq() request_irq(KEYBOARD_IRQ, keyboard_interrupt, 0, \
                                      "keyboard", NULL);

/*
 * Machine specific bits for the PS/2 driver
 */

#define AUX_IRQ 12

#ifdef CONFIG_MCA

#define aux_request_irq(handler, dev_id) request_irq(AUX_IRQ, handler, \
	MCA_bus ? SA_SHIRQ : 0, "PS/2 Mouse", dev_id)
#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)

#else /* !defined(CONFIG_MCA) */

#define aux_request_irq(handler, dev_id) request_irq(AUX_IRQ, handler, 0, \
	"PS/2 Mouse", NULL)
#define aux_free_irq(dev_id) free_irq(AUX_IRQ, NULL)

#endif

#endif /* __KERNEL__ */
#endif /* __ASM_i386_KEYBOARD_H */
