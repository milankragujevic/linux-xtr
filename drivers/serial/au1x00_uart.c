/*
 *  linux/drivers/char/au1x00_uart.c
 *
 *  Driver for the 16550-like Au1x00 serial ports
 *  Basically a copy of 8250.c minus everything I could gut out
 *  quickly.
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  $Id: 8250.c,v 1.90 2002/07/28 10:03:27 rmk Exp $
 *
 * A note about mapbase / membase
 *
 *  mapbase is the physical address of the IO port.  Currently, we don't
 *  support this very well, and it may well be dropped from this driver
 *  in future.  As such, mapbase should be NULL.
 *
 *  membase is an 'ioremapped' cookie.  This is compatible with the old
 *  serial.c driver, and is currently the preferred form.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>

#if defined(CONFIG_SERIAL_AU1X00_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include "8250.h"

#define AU1X00_UART 0

/*
 * Configuration:
 *   share_irqs - whether we pass SA_SHIRQ to request_irq().  This option
 *                is unsafe when used on edge-triggered interrupts.
 */
unsigned int share_irqs = SERIAL8250_SHARE_IRQS;

/*
 * Debugging.
 */
#if 0
#define DEBUG_AUTOCONF(fmt...)	printk(fmt)
#else
#define DEBUG_AUTOCONF(fmt...)	do { } while (0)
#endif

#if 0
#define DEBUG_INTR(fmt...)	printk(fmt)
#else
#define DEBUG_INTR(fmt...)	do { } while (0)
#endif

#define PASS_LIMIT	256

/*
 * We default to IRQ0 for the "no irq" hack.   Some
 * machine types want others as well - they're free
 * to redefine this in their header file.
 */
#define is_real_interrupt(irq)	((irq) != 0)

/*
 * This converts from our new CONFIG_ symbols to the symbols
 * that asm/serial.h expects.  You _NEED_ to comment out the
 * linux/config.h include contained inside asm/serial.h for
 * this to work.
 */
#undef CONFIG_SERIAL_MANY_PORTS
#undef CONFIG_SERIAL_DETECT_IRQ
#undef CONFIG_SERIAL_MULTIPORT
#undef CONFIG_HUB6

#ifdef CONFIG_SERIAL_8250_DETECT_IRQ
#define CONFIG_SERIAL_DETECT_IRQ 1
#endif
#ifdef CONFIG_SERIAL_8250_MULTIPORT
#define CONFIG_SERIAL_MULTIPORT 1
#endif

#include <asm/serial.h>

static struct old_serial_port old_serial_port[] = {
	SERIAL_PORT_DFNS /* defined in asm/serial.h */
};

#define UART_NR	ARRAY_SIZE(old_serial_port)

struct uart_8250_port {
	struct uart_port	port;
	struct timer_list	timer;		/* "no irq" timer */
	struct list_head	list;		/* ports on this IRQ */
	unsigned char		acr;
	unsigned char		ier;
	unsigned char		rev;
	unsigned char		lcr;
	unsigned char		mcr_mask;	/* mask of user bits */
	unsigned char		mcr_force;	/* mask of forced bits */
	unsigned int		lsr_break_flag;

	/*
	 * We provide a per-port pm hook.
	 */
	void			(*pm)(struct uart_port *port,
				      unsigned int state, unsigned int old);
};

struct irq_info {
	spinlock_t		lock;
	struct list_head	*head;
};

static struct irq_info irq_lists[NR_IRQS];

/*
 * Here we define the default xmit fifo size used for each type of UART.
 */
static const struct serial_uart_config uart_config[1] = {
	{ "AU1X00_UART", 16, UART_CLEAR_FIFO | UART_USE_FIFO },
};


static _INLINE_ unsigned int serial_in(struct uart_8250_port *up, int offset)
{
	return au_readl((unsigned long)up->port.membase + offset);
}

static _INLINE_ void
serial_out(struct uart_8250_port *up, int offset, int value)
{
	au_writel(value, (unsigned long)up->port.membase + offset);
}

/*
 * We used to support using pause I/O for certain machines.  We
 * haven't supported this for a while, but just in case it's badly
 * needed for certain old 386 machines, I've left these #define's
 * in....
 */
#define serial_inp(up, offset)		serial_in(up, offset)
#define serial_outp(up, offset, value)	serial_out(up, offset, value)


/*
 * This routine is called by rs_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void autoconfig(struct uart_8250_port *up, unsigned int probeflags)
{
	unsigned char save_lcr, save_mcr;
	unsigned long flags;

	if (!up->port.iobase && !up->port.membase)
		return;

	DEBUG_AUTOCONF("ttyS%d: autoconf (0x%04x, 0x%08lx): ",
			up->port.line, up->port.iobase, up->port.membase);

	/*
	 * We really do need global IRQs disabled here - we're going to
	 * be frobbing the chips IRQ enable register to see if it exists.
	 */
	spin_lock_irqsave(&up->port.lock, flags);
//	save_flags(flags); cli();

	save_mcr = serial_in(up, UART_MCR);
	save_lcr = serial_in(up, UART_LCR);

	up->port.type = AU1X00_UART;
	serial_outp(up, UART_LCR, save_lcr);

	up->port.fifosize = uart_config[up->port.type].dfl_xmit_fifo_size;

	if (up->port.type == PORT_UNKNOWN)
		goto out;

	/*
	 * Reset the UART.
	 */
	serial_outp(up, UART_MCR, save_mcr);
	serial_outp(up, UART_FCR, (UART_FCR_ENABLE_FIFO |
				     UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	serial_outp(up, UART_FCR, 0);
	(void)serial_in(up, UART_RX);
	serial_outp(up, UART_IER, 0);

 out:	
	spin_unlock_irqrestore(&up->port.lock, flags);
//	restore_flags(flags);
	DEBUG_AUTOCONF("type=%s\n", uart_config[up->port.type].name);
}

static void serial_au1x00_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;

	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_au1x00_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_au1x00_stop_rx(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;

	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}

static void serial_au1x00_enable_ms(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;

	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
}

static _INLINE_ void
receive_chars(struct uart_8250_port *up, int *status, struct pt_regs *regs)
{
	struct tty_struct *tty = up->port.info->tty;
	unsigned char ch;
	int max_count = 256;

	do {
		if (unlikely(tty->flip.count >= TTY_FLIPBUF_SIZE)) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				return; // if TTY_DONT_FLIP is set
		}
		ch = serial_inp(up, UART_RX);
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
				       UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (*status & UART_LSR_PE)
				up->port.icount.parity++;
			else if (*status & UART_LSR_FE)
				up->port.icount.frame++;
			if (*status & UART_LSR_OE)
				up->port.icount.overrun++;

			/*
			 * Mask off conditions which should be ingored.
			 */
			*status &= up->port.read_status_mask;

#ifdef CONFIG_SERIAL_AU1X00_CONSOLE
			if (up->port.line == up->port.cons->index) {
				/* Recover the break flag from console xmit */
				*status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}
#endif
			if (*status & UART_LSR_BI) {
				DEBUG_INTR("handling break....");
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch, regs))
			goto ignore_char;
		if ((*status & up->port.ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((*status & UART_LSR_OE) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
	ignore_char:
		*status = serial_inp(up, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));
	tty_flip_buffer_push(tty);
}

static _INLINE_ void transmit_chars(struct uart_8250_port *up)
{
	struct circ_buf *xmit = &up->port.info->xmit;
	int count;

	if (up->port.x_char) {
		serial_outp(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		serial_au1x00_stop_tx(&up->port, 0);
		return;
	}

	count = up->port.fifosize;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	DEBUG_INTR("THRE...");

	if (uart_circ_empty(xmit))
		serial_au1x00_stop_tx(&up->port, 0);
}

static _INLINE_ void check_modem_status(struct uart_8250_port *up)
{
	int status;

	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		up->port.icount.rng++;
	if (status & UART_MSR_DDSR)
		up->port.icount.dsr++;
	if (status & UART_MSR_DDCD)
		uart_handle_dcd_change(&up->port, status & UART_MSR_DCD);
	if (status & UART_MSR_DCTS)
		uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

	wake_up_interruptible(&up->port.info->delta_msr_wait);
}

/*
 * This handles the interrupt from one port.
 */
static inline void
serial_au1x00_handle_port(struct uart_8250_port *up, struct pt_regs *regs)
{
	unsigned int status = serial_inp(up, UART_LSR);

	DEBUG_INTR("status = %x...", status);

	if (status & UART_LSR_DR)
		receive_chars(up, &status, regs);
	check_modem_status(up);
	if (status & UART_LSR_THRE)
		transmit_chars(up);
}

/*
 * This is the serial driver's interrupt routine.
 *
 * Arjan thinks the old way was overly complex, so it got simplified.
 * Alan disagrees, saying that need the complexity to handle the weird
 * nature of ISA shared interrupts.  (This is a special exception.)
 *
 * In order to handle ISA shared interrupts properly, we need to check
 * that all ports have been serviced, and therefore the ISA interrupt
 * line has been de-asserted.
 *
 * This means we need to loop through all ports. checking that they
 * don't have an interrupt pending.
 */
static void serial_au1x00_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct irq_info *i = dev_id;
	struct list_head *l, *end = NULL;
	int pass_counter = 0;

	DEBUG_INTR("serial_au1x00_interrupt(%d)...", irq);

	spin_lock(&i->lock);

	l = i->head;
	do {
		struct uart_8250_port *up;
		unsigned int iir;

		up = list_entry(l, struct uart_8250_port, list);

		iir = serial_in(up, UART_IIR);
		if (!(iir & UART_IIR_NO_INT)) {
			spin_lock(&up->port.lock);
			serial_au1x00_handle_port(up, regs);
			spin_unlock(&up->port.lock);

			end = NULL;
		} else if (end == NULL)
			end = l;

		l = l->next;

		if (l == i->head && pass_counter++ > PASS_LIMIT) {
			/* If we hit this, we're dead. */
			printk(KERN_ERR "serial_au1x00: too much work for "
				"irq%d\n", irq);
			break;
		}
	} while (l != end);

	spin_unlock(&i->lock);

	DEBUG_INTR("end.\n");
}

/*
 * To support ISA shared interrupts, we need to have one interrupt
 * handler that ensures that the IRQ line has been deasserted
 * before returning.  Failing to do this will result in the IRQ
 * line being stuck active, and, since ISA irqs are edge triggered,
 * no more IRQs will be seen.
 */
static void serial_do_unlink(struct irq_info *i, struct uart_8250_port *up)
{
	spin_lock_irq(&i->lock);

	if (!list_empty(i->head)) {
		if (i->head == &up->list)
			i->head = i->head->next;
		list_del(&up->list);
	} else {
		BUG_ON(i->head != &up->list);
		i->head = NULL;
	}

	spin_unlock_irq(&i->lock);
}

static int serial_link_irq_chain(struct uart_8250_port *up)
{
	struct irq_info *i = irq_lists + up->port.irq;
	int ret, irq_flags = up->port.flags & UPF_SHARE_IRQ ? SA_SHIRQ : 0;

	spin_lock_irq(&i->lock);

	if (i->head) {
		list_add(&up->list, i->head);
		spin_unlock_irq(&i->lock);

		ret = 0;
	} else {
		INIT_LIST_HEAD(&up->list);
		i->head = &up->list;
		spin_unlock_irq(&i->lock);

		ret = request_irq(up->port.irq, serial_au1x00_interrupt,
				  irq_flags, "serial", i);
		if (ret)
			serial_do_unlink(i, up);
	}

	return ret;
}

static void serial_unlink_irq_chain(struct uart_8250_port *up)
{
	struct irq_info *i = irq_lists + up->port.irq;

	BUG_ON(i->head == NULL);

	if (list_empty(i->head))
		free_irq(up->port.irq, i);

	serial_do_unlink(i, up);
}

/*
 * This function is used to handle ports that do not have an
 * interrupt.  This doesn't work very well for 16450's, but gives
 * barely passable results for a 16550A.  (Although at the expense
 * of much CPU overhead).
 */
static void serial_au1x00_timeout(unsigned long data)
{
	struct uart_8250_port *up = (struct uart_8250_port *)data;
	unsigned int timeout;
	unsigned int iir;

	iir = serial_in(up, UART_IIR);
	if (!(iir & UART_IIR_NO_INT)) {
		spin_lock(&up->port.lock);
		serial_au1x00_handle_port(up, NULL);
		spin_unlock(&up->port.lock);
	}

	timeout = up->port.timeout;
	timeout = timeout > 6 ? (timeout / 2 - 2) : 1;
	mod_timer(&up->timer, jiffies + timeout);
}

static unsigned int serial_au1x00_tx_empty(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int serial_au1x00_get_mctrl(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned long flags;
	unsigned char status;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	status = serial_in(up, UART_MSR);
	spin_unlock_irqrestore(&up->port.lock, flags);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial_au1x00_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned char mcr = 0;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	mcr = (mcr & up->mcr_mask) | up->mcr_force;

	serial_out(up, UART_MCR, mcr);
}

static void serial_au1x00_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int serial_au1x00_startup(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned long flags;
	int retval;


	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reeanbled in change_speed())
	 */
	if (uart_config[up->port.type].flags & UART_CLEAR_FIFO) {
		serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
		serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		serial_outp(up, UART_FCR, 0);
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_inp(up, UART_LSR);
	(void) serial_inp(up, UART_RX);
	(void) serial_inp(up, UART_IIR);
	(void) serial_inp(up, UART_MSR);

	/*
	 * At this point, there's no way the LSR could still be 0xff;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (!(up->port.flags & UPF_BUGGY_UART) &&
	    (serial_inp(up, UART_LSR) == 0xff)) {
		printk("ttyS%d: LSR safety check engaged!\n", up->port.line);
		return -ENODEV;
	}

	/*
	 * If the "interrupt" for this port doesn't correspond with any
	 * hardware interrupt, we use a timer-based system.  The original
	 * driver used to do this with IRQ0.
	 */
	if (!is_real_interrupt(up->port.irq)) {
		unsigned int timeout = up->port.timeout;

		timeout = timeout > 6 ? (timeout / 2 - 2) : 1;

		up->timer.data = (unsigned long)up;
		mod_timer(&up->timer, jiffies + timeout);
	} else {
		retval = serial_link_irq_chain(up);
		if (retval)
			return retval;
	}

	/*
	 * Now, initialize the UART
	 */
	serial_outp(up, UART_LCR, UART_LCR_WLEN8);

	spin_lock_irqsave(&up->port.lock, flags);
	if (up->port.flags & UPF_FOURPORT) {
		if (!is_real_interrupt(up->port.irq))
			up->port.mctrl |= TIOCM_OUT1;
	} else
		/*
		 * Most PC uarts need OUT2 raised to enable interrupts.
		 */
		if (is_real_interrupt(up->port.irq))
			up->port.mctrl |= TIOCM_OUT2;

	serial_au1x00_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via change_speed(), which will be occuring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI | UART_IER_RDI;
	serial_outp(up, UART_IER, up->ier);

	if (up->port.flags & UPF_FOURPORT) {
		unsigned int icp;
		/*
		 * Enable interrupts on the AST Fourport board
		 */
		icp = (up->port.iobase & 0xfe0) | 0x01f;
		outb_p(0x80, icp);
		(void) inb_p(icp);
	}

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) serial_inp(up, UART_LSR);
	(void) serial_inp(up, UART_RX);
	(void) serial_inp(up, UART_IIR);
	(void) serial_inp(up, UART_MSR);

	return 0;
}

static void serial_au1x00_shutdown(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned long flags;

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	serial_outp(up, UART_IER, 0);

	spin_lock_irqsave(&up->port.lock, flags);
	if (up->port.flags & UPF_FOURPORT) {
		/* reset interrupts on the AST Fourport board */
		inb((up->port.iobase & 0xfe0) | 0x1f);
		up->port.mctrl |= TIOCM_OUT1;
	} else
		up->port.mctrl &= ~TIOCM_OUT2;

	serial_au1x00_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(up, UART_LCR, serial_inp(up, UART_LCR) & ~UART_LCR_SBC);
	serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO |
				  UART_FCR_CLEAR_RCVR |
				  UART_FCR_CLEAR_XMIT);
	serial_outp(up, UART_FCR, 0);

	/*
	 * Read data port to reset things, and then unlink from
	 * the IRQ chain.
	 */
	(void) serial_in(up, UART_RX);

	if (!is_real_interrupt(up->port.irq))
		del_timer_sync(&up->timer);
	else
		serial_unlink_irq_chain(up);
}

static void
serial_au1x00_change_speed(struct uart_port *port, unsigned int cflag,
			unsigned int iflag, unsigned int quot)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	unsigned char cval;
	unsigned long flags;

	switch (cflag & CSIZE) {
	case CS5:
		cval = 0x00;
		break;
	case CS6:
		cval = 0x01;
		break;
	case CS7:
		cval = 0x02;
		break;
	default:
	case CS8:
		cval = 0x03;
		break;
	}

	if (cflag & CSTOPB)
		cval |= 0x04;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;

#if 0 /* fixme */
	if (uart_config[up->port.type].flags & UART_USE_FIFO) {
		if ((up->port.uartclk / quot) < (2400 * 16))
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
		else
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	}
#endif

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&up->port, cflag))
		up->ier |= UART_IER_MSI;

	serial_out(up, UART_IER, up->ier);

	quot = 0x35;
	serial_outp(up, 0x28, quot & 0xffff);

	up->lcr = cval;					/* Save LCR */
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void
serial_au1x00_pm(struct uart_port *port, unsigned int state,
	      unsigned int oldstate)
{
#if 0
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	if (state) {
		/* sleep */
		if (uart_config[up->port.type].flags & UART_STARTECH) {
			/* Arrange to enter sleep mode */
			serial_outp(up, UART_LCR, 0xBF);
			serial_outp(up, UART_EFR, UART_EFR_ECB);
			serial_outp(up, UART_LCR, 0);
			serial_outp(up, UART_IER, UART_IERX_SLEEP);
			serial_outp(up, UART_LCR, 0xBF);
			serial_outp(up, UART_EFR, 0);
			serial_outp(up, UART_LCR, 0);
		}
		if (up->port.type == PORT_16750) {
			/* Arrange to enter sleep mode */
			serial_outp(up, UART_IER, UART_IERX_SLEEP);
		}

		if (up->pm)
			up->pm(port, state, oldstate);
	} else {
		/* wake */
		if (uart_config[up->port.type].flags & UART_STARTECH) {
			/* Wake up UART */
			serial_outp(up, UART_LCR, 0xBF);
			serial_outp(up, UART_EFR, UART_EFR_ECB);
			/*
			 * Turn off LCR == 0xBF so we actually set the IER
			 * register on the XR16C850
			 */
			serial_outp(up, UART_LCR, 0);
			serial_outp(up, UART_IER, 0);
			/*
			 * Now reset LCR so we can turn off the ECB bit
			 */
			serial_outp(up, UART_LCR, 0xBF);
			serial_outp(up, UART_EFR, 0);
			/*
			 * For a XR16C850, we need to set the trigger levels
			 */
			if (up->port.type == PORT_16850) {
				unsigned char fctr;

				fctr = serial_inp(up, UART_FCTR) &
					 ~(UART_FCTR_RX | UART_FCTR_TX);
				serial_outp(up, UART_FCTR, fctr |
						UART_FCTR_TRGD |
						UART_FCTR_RX);
				serial_outp(up, UART_TRG, UART_TRG_96);
				serial_outp(up, UART_FCTR, fctr |
						UART_FCTR_TRGD |
						UART_FCTR_TX);
				serial_outp(up, UART_TRG, UART_TRG_96);
			}
			serial_outp(up, UART_LCR, 0);
		}

		if (up->port.type == PORT_16750) {
			/* Wake up UART */
			serial_outp(up, UART_IER, 0);
		}

		if (up->pm)
			up->pm(port, state, oldstate);
	}
#endif
}

/*
 * Resource handling.  This is complicated by the fact that resources
 * depend on the port type.  Maybe we should be claiming the standard
 * 8250 ports, and then trying to get other resources as necessary?
 */
static int
serial_au1x00_request_std_resource(struct uart_8250_port *up, struct resource **res)
{
	unsigned int size = 8 << up->port.regshift;
	int ret = 0;

	printk("request_std_resource\n");
#if 1
	switch (up->port.iotype) {
	case SERIAL_IO_MEM:
		if (up->port.mapbase) {
			*res = request_mem_region(up->port.mapbase, size, "serial");
			if (!*res)
				ret = -EBUSY;
		}
		break;

	case SERIAL_IO_HUB6:
	case SERIAL_IO_PORT:
		*res = request_region(up->port.iobase, size, "serial");
		if (!*res)
			ret = -EBUSY;
		break;
	}
#endif
	return ret;
}


static void serial_au1x00_release_port(struct uart_port *port)
{
	printk("release_port\n");
}

static int serial_au1x00_request_port(struct uart_port *port)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	struct resource *res = NULL, *res_rsa = NULL;
	int ret = 0;

	printk("request_port\n");
	if (up->port.flags & UPF_RESOURCES) {
		ret = serial_au1x00_request_std_resource(up, &res);
	}

	/*
	 * If we have a mapbase, then request that as well.
	 */
	if (ret == 0 && up->port.flags & UPF_IOREMAP) {
		int size = res->end - res->start + 1;

		up->port.membase = ioremap(up->port.mapbase, size);
		if (!up->port.membase)
			ret = -ENOMEM;
	}

	if (ret) {
		if (res_rsa)
			release_resource(res_rsa);
		if (res)
			release_resource(res);
	}
	return ret;
}

static void serial_au1x00_config_port(struct uart_port *port, int flags)
{
	struct uart_8250_port *up = (struct uart_8250_port *)port;
	struct resource *res_std = NULL, *res_rsa = NULL;
	int probeflags = PROBE_ANY;

	probeflags &= ~PROBE_RSA;

	if (flags & UART_CONFIG_TYPE)
		autoconfig(up, probeflags);

	/*
	 * If the port wasn't an RSA port, release the resource.
	 */
	if (up->port.type != PORT_RSA && res_rsa)
		release_resource(res_rsa);

	if (up->port.type == PORT_UNKNOWN && res_std)
		release_resource(res_std);
}

static int
serial_au1x00_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->irq >= NR_IRQS || ser->irq < 0 ||
	    ser->baud_base < 9600 || ser->type < PORT_UNKNOWN ||
	    ser->type > PORT_MAX_8250 || ser->type == PORT_CIRRUS ||
	    ser->type == PORT_STARTECH)
		return -EINVAL;
	return 0;
}

static const char *
serial_au1x00_type(struct uart_port *port)
{
	int type = port->type;

	if (type >= ARRAY_SIZE(uart_config))
		type = 0;
	return uart_config[type].name;
}

static struct uart_ops serial_au1x00_pops = {
	.tx_empty	= serial_au1x00_tx_empty,
	.set_mctrl	= serial_au1x00_set_mctrl,
	.get_mctrl	= serial_au1x00_get_mctrl,
	.stop_tx	= serial_au1x00_stop_tx,
	.start_tx	= serial_au1x00_start_tx,
	.stop_rx	= serial_au1x00_stop_rx,
	.enable_ms	= serial_au1x00_enable_ms,
	.break_ctl	= serial_au1x00_break_ctl,
	.startup	= serial_au1x00_startup,
	.shutdown	= serial_au1x00_shutdown,
	.change_speed	= serial_au1x00_change_speed,
	.pm		= serial_au1x00_pm,
	.type		= serial_au1x00_type,
	.release_port	= serial_au1x00_release_port,
	.request_port	= serial_au1x00_request_port,
	.config_port	= serial_au1x00_config_port,
	.verify_port	= serial_au1x00_verify_port,
};

static struct uart_8250_port serial_au1x00_ports[UART_NR];

static void __init serial_au1x00_isa_init_ports(void)
{
	struct uart_8250_port *up;
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0, up = serial_au1x00_ports; i < ARRAY_SIZE(old_serial_port);
	     i++, up++) {
		up->port.iobase   = old_serial_port[i].port;
		up->port.irq      = irq_cannonicalize(old_serial_port[i].irq);
		up->port.uartclk  = old_serial_port[i].baud_base * 16;
		up->port.flags    = old_serial_port[i].flags |
				    UPF_RESOURCES;
		up->port.hub6     = old_serial_port[i].hub6;
		up->port.membase  = old_serial_port[i].iomem_base;
		up->port.iotype   = old_serial_port[i].io_type;
		up->port.regshift = old_serial_port[i].iomem_reg_shift;
		up->port.ops      = &serial_au1x00_pops;
	}
}

static void __init serial_au1x00_register_ports(struct uart_driver *drv)
{
	int i;

	serial_au1x00_isa_init_ports();

	for (i = 0; i < UART_NR; i++) {
		struct uart_8250_port *up = &serial_au1x00_ports[i];

		up->port.line = i;
		up->port.ops = &serial_au1x00_pops;
		init_timer(&up->timer);
		up->timer.function = serial_au1x00_timeout;

		/*
		 * ALPHA_KLUDGE_MCR needs to be killed.
		 */
		up->mcr_mask = ~ALPHA_KLUDGE_MCR;
		up->mcr_force = ALPHA_KLUDGE_MCR;

		uart_add_one_port(drv, &up->port);
	}
}

#ifdef CONFIG_SERIAL_AU1X00_CONSOLE

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_8250_port *up)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
		udelay(1);
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       ((serial_in(up, UART_MSR) & UART_MSR_CTS) == 0))
			udelay(1);
	}
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial_au1x00_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_8250_port *up = &serial_au1x00_ports[co->index];
	unsigned int ier;
	int i;

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		wait_for_xmitr(up);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		serial_out(up, UART_TX, *s);
		if (*s == 10) {
			wait_for_xmitr(up);
			serial_out(up, UART_TX, 13);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	serial_out(up, UART_IER, ier);
}

static kdev_t serial_au1x00_console_device(struct console *co)
{
	return mk_kdev(TTY_MAJOR, 64 + co->index);
}

static int __init serial_au1x00_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	//if (co->index >= UART_NR)
	if (co->index >= 4)
		co->index = 0;
	port = &serial_au1x00_ports[co->index].port;

	/*
	 * Temporary fix.
	 */
	spin_lock_init(&port->lock);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console serial_au1x00_console = {
	.name		= "ttyS",
	.write		= serial_au1x00_console_write,
	.device		= serial_au1x00_console_device,
	.setup		= serial_au1x00_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

void __init serial_au1x00_console_init(void)
{
	serial_au1x00_isa_init_ports();
	register_console(&serial_au1x00_console);
}

#define SERIAL8250_CONSOLE	&serial_au1x00_console
#else
#define SERIAL8250_CONSOLE	NULL
#endif

static struct uart_driver serial_au1x00_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial",
#ifdef CONFIG_DEVFS_FS
	.dev_name		= "tts/%d",
#else
	.dev_name		= "ttyS%d",
#endif
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= UART_NR,
	.cons			= SERIAL8250_CONSOLE,
};

/*
 * register_serial and unregister_serial allows for 16x50 serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */

static int __register_serial(struct serial_struct *req, int line)
{
	struct uart_port port;

	port.iobase   = req->port;
	port.membase  = req->iomem_base;
	port.irq      = req->irq;
	port.uartclk  = req->baud_base * 16;
	port.fifosize = req->xmit_fifo_size;
	port.regshift = req->iomem_reg_shift;
	port.iotype   = req->io_type;
	port.flags    = req->flags | UPF_BOOT_AUTOCONF;
	port.line     = line;

	/*
	 * If a clock rate wasn't specified by the low level
	 * driver, then default to the standard clock rate.
	 */
	if (port.uartclk == 0)
		port.uartclk = get_au1x00_uart_baud_base();
		//port.uartclk = BASE_BAUD * 16;

	return uart_register_port(&serial_au1x00_reg, &port);
}

/**
 *	register_serial - configure a 16x50 serial port at runtime
 *	@req: request structure
 *
 *	Configure the serial port specified by the request. If the
 *	port exists and is in use an error is returned. If the port
 *	is not currently in the table it is added.
 *
 *	The port is then probed and if neccessary the IRQ is autodetected
 *	If this fails an error is returned.
 *
 *	On success the port is ready to use and the line number is returned.
 */
int register_serial(struct serial_struct *req)
{
	return __register_serial(req, -1);
}

int __init early_serial_setup(struct serial_struct *req)
{
	__register_serial(req, req->line);
	return 0;
}

/**
 *	unregister_serial - remove a 16x50 serial port at runtime
 *	@line: serial line number
 *
 *	Remove one serial port.  This may be called from interrupt
 *	context.
 */
void unregister_serial(int line)
{
	uart_unregister_port(&serial_au1x00_reg, line);
}

static int __init serial_au1x00_init(void)
{
	int ret, i;

	printk(KERN_INFO "Serial: 8250/16550 driver $Revision: 1.90 $ "
		"IRQ sharing %sabled\n", share_irqs ? "en" : "dis");

	for (i = 0; i < NR_IRQS; i++)
		spin_lock_init(&irq_lists[i].lock);

	ret = uart_register_driver(&serial_au1x00_reg);
	if (ret)
		return ret;

	serial_au1x00_register_ports(&serial_au1x00_reg);
	return 0;
}

static void __exit serial_au1x00_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++)
		uart_remove_one_port(&serial_au1x00_reg, &serial_au1x00_ports[i].port);

	uart_unregister_driver(&serial_au1x00_reg);
}

module_init(serial_au1x00_init);
module_exit(serial_au1x00_exit);

EXPORT_SYMBOL(register_serial);
EXPORT_SYMBOL(unregister_serial);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au1x00 serial driver $Revision: 1.00 $");

MODULE_PARM(share_irqs, "i");
