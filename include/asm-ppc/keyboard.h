/*
 *  linux/include/asm-ppc/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *  Modified for Power Macintosh by Paul Mackerras
 *
 * $Id: keyboard.h,v 1.3 1997/07/24 01:55:57 ralf Exp $
 */

/*
 * This file contains the ppc architecture specific keyboard definitions -
 * like the intel pc for prep systems, different for power macs.
 */

#ifndef __ASMPPC_KEYBOARD_H
#define __ASMPPC_KEYBOARD_H

#ifdef __KERNEL__

#include <asm/io.h>

#include <linux/config.h>

#ifdef CONFIG_MAC_KEYBOARD

extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_pretranslate(unsigned char scancode, char raw_mode);
extern int mackbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern int mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void mackbd_init_hw(void);

#define kbd_setkeycode		mackbd_setkeycode
#define kbd_getkeycode		mackbd_getkeycode
#define kbd_pretranslate	mackbd_pretranslate
#define kbd_translate		mackbd_translate
#define kbd_unexpected_up	mackbd_unexpected_up
#define kbd_leds		mackbd_leds
#define kbd_init_hw		mackbd_init_hw

#else /* CONFIG_MAC_KEYBOARD */

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

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_pretranslate	pckbd_pretranslate
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input() inb(KBD_DATA_REG)
#define kbd_read_status() inb(KBD_STATUS_REG)
#define kbd_write_output(val) outb(val, KBD_DATA_REG)
#define kbd_write_command(val) outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

#define INIT_KBD
#endif /* CONFIG_MAC_KEYBOARD */

#define keyboard_setup()						\
	request_region(0x60, 16, "keyboard")

/*
 * Machine specific bits for the PS/2 driver
 *
 * FIXME: does any PPC machine use the PS/2 driver at all?  If so,
 *        this should work, if not it's dead code ...
 */

#define AUX_IRQ 12

#define ps2_request_irq()						\
	request_irq(AUX_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL)

#define ps2_free_irq(inode) free_irq(AUX_IRQ, NULL)

#endif /* __KERNEL__ */

#endif /* __ASMPPC_KEYBOARD_H */
