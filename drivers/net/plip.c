/* $Id: plip.c,v 1.3.6.2 1997/04/16 15:07:56 phil Exp $ */
/* PLIP: A parallel port "network" driver for Linux. */
/* This driver is for parallel port with 5-bit cable (LapLink (R) cable). */
/*
 * Authors:	Donald Becker,  <becker@super.org>
 *		Tommy Thorn, <thorn@daimi.aau.dk>
 *		Tanabe Hiroyasu, <hiro@sanpo.t.u-tokyo.ac.jp>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Peter Bauer, <100136.3530@compuserve.com>
 *		Niibe Yutaka, <gniibe@mri.co.jp>
 *
 *		Modularization and ifreq/ifmap support by Alan Cox.
 *		Rewritten by Niibe Yutaka.
 *              parport-sharing awareness code by Philip Blundell.
 *
 * Fixes:
 *		Niibe Yutaka
 *		  - Module initialization.  You can specify I/O addr and IRQ:
 *			# insmod plip.o io=0x3bc irq=7
 *		  - MTU fix.
 *		  - Make sure other end is OK, before sending a packet.
 *		  - Fix immediate timer problem.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/*
 * Original version and the name 'PLIP' from Donald Becker <becker@super.org>
 * inspired by Russ Nelson's parallel port packet driver.
 *
 * NOTE:
 *     Tanabe Hiroyasu had changed the protocol, and it was in Linux v1.0.
 *     Because of the necessity to communicate to DOS machines with the
 *     Crynwr packet driver, Peter Bauer changed the protocol again
 *     back to original protocol.
 *
 *     This version follows original PLIP protocol.
 *     So, this PLIP can't communicate the PLIP of Linux v1.0.
 */

/*
 *     To use with DOS box, please do (Turn on ARP switch):
 *	# ifconfig plip[0-2] arp
 */
static const char *version = "NET3 PLIP version 2.2-parport gniibe@mri.co.jp\n";

/*
  Sources:
	Ideas and protocols came from Russ Nelson's <nelson@crynwr.com>
	"parallel.asm" parallel port packet driver.

  The "Crynwr" parallel port standard specifies the following protocol:
    Trigger by sending '0x08' (this cause interrupt on other end)
    count-low octet
    count-high octet
    ... data octets
    checksum octet
  Each octet is sent as <wait for rx. '0x1?'> <send 0x10+(octet&0x0F)>
			<wait for rx. '0x0?'> <send 0x00+((octet>>4)&0x0F)>

  The packet is encapsulated as if it were ethernet.

  The cable used is a de facto standard parallel null cable -- sold as
  a "LapLink" cable by various places.  You'll need a 12-conductor cable to
  make one yourself.  The wiring is:
    SLCTIN	17 - 17
    GROUND	25 - 25
    D0->ERROR	2 - 15		15 - 2
    D1->SLCT	3 - 13		13 - 3
    D2->PAPOUT	4 - 12		12 - 4
    D3->ACK	5 - 10		10 - 5
    D4->BUSY	6 - 11		11 - 6
  Do not connect the other pins.  They are
    D5,D6,D7 are 7,8,9
    STROBE is 1, FEED is 14, INIT is 16
    extra grounds are 18,19,20,21,22,23,24
*/

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/if_ether.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/lp.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_plip.h>

#include <linux/tqueue.h>
#include <linux/ioport.h>
#include <asm/bitops.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include <linux/parport.h>

/* Maximum number of devices to support. */
#define PLIP_MAX  8

/* Use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
static unsigned int net_debug = NET_DEBUG;

/* In micro second */
#define PLIP_DELAY_UNIT		   1

/* Connection time out = PLIP_TRIGGER_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_TRIGGER_WAIT	 500

/* Nibble time out = PLIP_NIBBLE_WAIT * PLIP_DELAY_UNIT usec */
#define PLIP_NIBBLE_WAIT        3000

#define PAR_INTR_ON		(LP_PINITP|LP_PSELECP|LP_PINTEN)
#define PAR_INTR_OFF		(LP_PINITP|LP_PSELECP)
#define PAR_DATA(dev)		((dev)->base_addr+0)
#define PAR_STATUS(dev)		((dev)->base_addr+1)
#define PAR_CONTROL(dev)	((dev)->base_addr+2)

/* Bottom halfs */
static void plip_kick_bh(struct device *dev);
static void plip_bh(struct device *dev);

/* Interrupt handler */
static void plip_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/* Functions for DEV methods */
static int plip_rebuild_header(struct sk_buff *skb);
static int plip_tx_packet(struct sk_buff *skb, struct device *dev);
static int plip_open(struct device *dev);
static int plip_close(struct device *dev);
static struct net_device_stats *plip_get_stats(struct device *dev);
static int plip_config(struct device *dev, struct ifmap *map);
static int plip_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static int plip_preempt(void *handle);
static void plip_wakeup(void *handle);

enum plip_connection_state {
	PLIP_CN_NONE=0,
	PLIP_CN_RECEIVE,
	PLIP_CN_SEND,
	PLIP_CN_CLOSING,
	PLIP_CN_ERROR
};

enum plip_packet_state {
	PLIP_PK_DONE=0,
	PLIP_PK_TRIGGER,
	PLIP_PK_LENGTH_LSB,
	PLIP_PK_LENGTH_MSB,
	PLIP_PK_DATA,
	PLIP_PK_CHECKSUM
};

enum plip_nibble_state {
	PLIP_NB_BEGIN,
	PLIP_NB_1,
	PLIP_NB_2,
};

struct plip_local {
	enum plip_packet_state state;
	enum plip_nibble_state nibble;
	union {
		struct {
#if defined(__LITTLE_ENDIAN)
			unsigned char lsb;
			unsigned char msb;
#elif defined(__BIG_ENDIAN)
			unsigned char msb;
			unsigned char lsb;
#else
#error	"Please fix the endianness defines in <asm/byteorder.h>"
#endif
		} b;
		unsigned short h;
	} length;
	unsigned short byte;
	unsigned char  checksum;
	unsigned char  data;
	struct sk_buff *skb;
};

struct net_local {
	struct net_device_stats enet_stats;
	struct tq_struct immediate;
	struct tq_struct deferred;
	struct plip_local snd_data;
	struct plip_local rcv_data;
	struct pardevice *pardev;
	unsigned long  trigger;
	unsigned long  nibble;
	enum plip_connection_state connection;
	unsigned short timeout_count;
	int is_deferred;
	int port_owner;
	int should_relinquish;
	int (*orig_rebuild_header)(struct sk_buff *skb);
};

/* Entry point of PLIP driver.
   Probe the hardware, and register/initialize the driver.

   PLIP is rather weird, because of the way it interacts with the parport
   system.  It is _not_ initialised from Space.c.  Instead, plip_init()
   is called, and that function makes up a "struct device" for each port, and
   then calls us here.

   */
__initfunc(int
plip_init_dev(struct device *dev, struct parport *pb))
{
	struct net_local *nl;
	struct pardevice *pardev;

	dev->irq = pb->irq;
	dev->base_addr = pb->base;

	if (pb->irq == -1) {
		printk(KERN_INFO "plip: %s has no IRQ.\n", pb->name);
		return -ENODEV;
	}
	
	pardev = parport_register_device(pb, dev->name, plip_preempt,
					 plip_wakeup, plip_interrupt, 
					 PARPORT_DEV_LURK, dev);

	printk(version);
	printk("%s: Parallel port at %#3lx, using IRQ %d\n", dev->name,
	       dev->base_addr, dev->irq);

	/* Fill in the generic fields of the device structure. */
	ether_setup(dev);

	/* Then, override parts of it */
	dev->hard_start_xmit	= plip_tx_packet;
	dev->open		= plip_open;
	dev->stop		= plip_close;
	dev->get_stats 		= plip_get_stats;
	dev->set_config		= plip_config;
	dev->do_ioctl		= plip_ioctl;
	dev->tx_queue_len	= 10;
	dev->flags	        = IFF_POINTOPOINT|IFF_NOARP;

	/* Set the private structure */
	dev->priv = kmalloc(sizeof (struct net_local), GFP_KERNEL);
	if (dev->priv == NULL) {
		printk(KERN_ERR "%s: out of memory\n", dev->name);
		parport_unregister_device(pardev);
		return -ENOMEM;
	}
	memset(dev->priv, 0, sizeof(struct net_local));
	nl = (struct net_local *) dev->priv;

	nl->orig_rebuild_header = dev->rebuild_header;
	dev->rebuild_header 	= plip_rebuild_header;
	nl->pardev = pardev; 

	nl->port_owner = 0;

	/* Initialize constants */
	nl->trigger	= PLIP_TRIGGER_WAIT;
	nl->nibble	= PLIP_NIBBLE_WAIT;

	/* Initialize task queue structures */
	nl->immediate.next = NULL;
	nl->immediate.sync = 0;
	nl->immediate.routine = (void *)(void *)plip_bh;
	nl->immediate.data = dev;

	nl->deferred.next = NULL;
	nl->deferred.sync = 0;
	nl->deferred.routine = (void *)(void *)plip_kick_bh;
	nl->deferred.data = dev;

	return 0;
}

/* Bottom half handler for the delayed request.
   This routine is kicked by do_timer().
   Request `plip_bh' to be invoked. */
static void
plip_kick_bh(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;

	if (nl->is_deferred) {
		queue_task(&nl->immediate, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

/* Forward declarations of internal routines */
static int plip_none(struct device *, struct net_local *,
		     struct plip_local *, struct plip_local *);
static int plip_receive_packet(struct device *, struct net_local *,
			       struct plip_local *, struct plip_local *);
static int plip_send_packet(struct device *, struct net_local *,
			    struct plip_local *, struct plip_local *);
static int plip_connection_close(struct device *, struct net_local *,
				 struct plip_local *, struct plip_local *);
static int plip_error(struct device *, struct net_local *,
		      struct plip_local *, struct plip_local *);
static int plip_bh_timeout_error(struct device *dev, struct net_local *nl,
				 struct plip_local *snd,
				 struct plip_local *rcv,
				 int error);

#define OK        0
#define TIMEOUT   1
#define ERROR     2

typedef int (*plip_func)(struct device *dev, struct net_local *nl,
			 struct plip_local *snd, struct plip_local *rcv);

static plip_func connection_state_table[] =
{
	plip_none,
	plip_receive_packet,
	plip_send_packet,
	plip_connection_close,
	plip_error
};

/* Bottom half handler of PLIP. */
static void
plip_bh(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct plip_local *snd = &nl->snd_data;
	struct plip_local *rcv = &nl->rcv_data;
	plip_func f;
	int r;

	nl->is_deferred = 0;
	f = connection_state_table[nl->connection];
	if ((r = (*f)(dev, nl, snd, rcv)) != OK
	    && (r = plip_bh_timeout_error(dev, nl, snd, rcv, r)) != OK) {
		nl->is_deferred = 1;
		queue_task(&nl->deferred, &tq_timer);
	}
}

static int
plip_bh_timeout_error(struct device *dev, struct net_local *nl,
		      struct plip_local *snd, struct plip_local *rcv,
		      int error)
{
	unsigned char c0;

	cli();
	if (nl->connection == PLIP_CN_SEND) {

		if (error != ERROR) { /* Timeout */
			nl->timeout_count++;
			if ((snd->state == PLIP_PK_TRIGGER
			     && nl->timeout_count <= 10)
			    || nl->timeout_count <= 3) {
				sti();
				/* Try again later */
				return TIMEOUT;
			}
			c0 = inb(PAR_STATUS(dev));
			printk("%s: transmit timeout(%d,%02x)\n",
			       dev->name, snd->state, c0);
		}
		nl->enet_stats.tx_errors++;
		nl->enet_stats.tx_aborted_errors++;
	} else if (nl->connection == PLIP_CN_RECEIVE) {
		if (rcv->state == PLIP_PK_TRIGGER) {
			/* Transmission was interrupted. */
			sti();
			return OK;
		}
		if (error != ERROR) { /* Timeout */
			if (++nl->timeout_count <= 3) {
				sti();
				/* Try again later */
				return TIMEOUT;
			}
			c0 = inb(PAR_STATUS(dev));
			printk("%s: receive timeout(%d,%02x)\n",
			       dev->name, rcv->state, c0);
		}
		nl->enet_stats.rx_dropped++;
	}
	rcv->state = PLIP_PK_DONE;
	if (rcv->skb) {
		kfree_skb(rcv->skb, FREE_READ);
		rcv->skb = NULL;
	}
	snd->state = PLIP_PK_DONE;
	if (snd->skb) {
		dev_kfree_skb(snd->skb, FREE_WRITE);
		snd->skb = NULL;
	}
	disable_irq(dev->irq);
	outb(PAR_INTR_OFF, PAR_CONTROL(dev));
	dev->tbusy = 1;
	nl->connection = PLIP_CN_ERROR;
	outb(0x00, PAR_DATA(dev));
	sti();

	return TIMEOUT;
}

static int
plip_none(struct device *dev, struct net_local *nl,
	  struct plip_local *snd, struct plip_local *rcv)
{
	return OK;
}

/* PLIP_RECEIVE --- receive a byte(two nibbles)
   Returns OK on success, TIMEOUT on timeout */
inline static int
plip_receive(unsigned short nibble_timeout, unsigned short status_addr,
	     enum plip_nibble_state *ns_p, unsigned char *data_p)
{
	unsigned char c0, c1;
	unsigned int cx;

	switch (*ns_p) {
	case PLIP_NB_BEGIN:
		cx = nibble_timeout;
		while (1) {
			c0 = inb(status_addr);
			udelay(PLIP_DELAY_UNIT);
			if ((c0 & 0x80) == 0) {
				c1 = inb(status_addr);
				if (c0 == c1)
					break;
			}
			if (--cx == 0)
				return TIMEOUT;
		}
		*data_p = (c0 >> 3) & 0x0f;
		outb(0x10, --status_addr); /* send ACK */
		status_addr++;
		*ns_p = PLIP_NB_1;

	case PLIP_NB_1:
		cx = nibble_timeout;
		while (1) {
			c0 = inb(status_addr);
			udelay(PLIP_DELAY_UNIT);
			if (c0 & 0x80) {
				c1 = inb(status_addr);
				if (c0 == c1)
					break;
			}
			if (--cx == 0)
				return TIMEOUT;
		}
		*data_p |= (c0 << 1) & 0xf0;
		outb(0x00, --status_addr); /* send ACK */
		status_addr++;
		*ns_p = PLIP_NB_BEGIN;
	case PLIP_NB_2:
		break;
	}
	return OK;
}

/* PLIP_RECEIVE_PACKET --- receive a packet */
static int
plip_receive_packet(struct device *dev, struct net_local *nl,
		    struct plip_local *snd, struct plip_local *rcv)
{
	unsigned short status_addr = PAR_STATUS(dev);
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;

	switch (rcv->state) {
	case PLIP_PK_TRIGGER:
		disable_irq(dev->irq);
		outb(PAR_INTR_OFF, PAR_CONTROL(dev));
		dev->interrupt = 0;
		outb(0x01, PAR_DATA(dev)); /* send ACK */
		if (net_debug > 2)
			printk("%s: receive start\n", dev->name);
		rcv->state = PLIP_PK_LENGTH_LSB;
		rcv->nibble = PLIP_NB_BEGIN;

	case PLIP_PK_LENGTH_LSB:
		if (snd->state != PLIP_PK_DONE) {
			if (plip_receive(nl->trigger, status_addr,
					 &rcv->nibble, &rcv->length.b.lsb)) {
				/* collision, here dev->tbusy == 1 */
				rcv->state = PLIP_PK_DONE;
				nl->is_deferred = 1;
				nl->connection = PLIP_CN_SEND;
				queue_task(&nl->deferred, &tq_timer);
				outb(PAR_INTR_ON, PAR_CONTROL(dev));
				enable_irq(dev->irq);
				return OK;
			}
		} else {
			if (plip_receive(nibble_timeout, status_addr,
					 &rcv->nibble, &rcv->length.b.lsb))
				return TIMEOUT;
		}
		rcv->state = PLIP_PK_LENGTH_MSB;

	case PLIP_PK_LENGTH_MSB:
		if (plip_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &rcv->length.b.msb))
			return TIMEOUT;
		if (rcv->length.h > dev->mtu + dev->hard_header_len
		    || rcv->length.h < 8) {
			printk("%s: bogus packet size %d.\n", dev->name, rcv->length.h);
			return ERROR;
		}
		/* Malloc up new buffer. */
		rcv->skb = dev_alloc_skb(rcv->length.h);
		if (rcv->skb == NULL) {
			printk("%s: Memory squeeze.\n", dev->name);
			return ERROR;
		}
		skb_put(rcv->skb,rcv->length.h);
		rcv->skb->dev = dev;
		rcv->state = PLIP_PK_DATA;
		rcv->byte = 0;
		rcv->checksum = 0;

	case PLIP_PK_DATA:
		lbuf = rcv->skb->data;
		do
			if (plip_receive(nibble_timeout, status_addr,
					 &rcv->nibble, &lbuf[rcv->byte]))
				return TIMEOUT;
		while (++rcv->byte < rcv->length.h);
		do
			rcv->checksum += lbuf[--rcv->byte];
		while (rcv->byte);
		rcv->state = PLIP_PK_CHECKSUM;

	case PLIP_PK_CHECKSUM:
		if (plip_receive(nibble_timeout, status_addr,
				 &rcv->nibble, &rcv->data))
			return TIMEOUT;
		if (rcv->data != rcv->checksum) {
			nl->enet_stats.rx_crc_errors++;
			if (net_debug)
				printk("%s: checksum error\n", dev->name);
			return ERROR;
		}
		rcv->state = PLIP_PK_DONE;

	case PLIP_PK_DONE:
		/* Inform the upper layer for the arrival of a packet. */
		rcv->skb->protocol=eth_type_trans(rcv->skb, dev);
		netif_rx(rcv->skb);
		nl->enet_stats.rx_packets++;
		rcv->skb = NULL;
		if (net_debug > 2)
			printk("%s: receive end\n", dev->name);

		/* Close the connection. */
		outb (0x00, PAR_DATA(dev));
		cli();
		if (snd->state != PLIP_PK_DONE) {
			nl->connection = PLIP_CN_SEND;
			sti();
			queue_task(&nl->immediate, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
			outb(PAR_INTR_ON, PAR_CONTROL(dev));
			enable_irq(dev->irq);
			return OK;
		} else {
			nl->connection = PLIP_CN_NONE;
			sti();
			outb(PAR_INTR_ON, PAR_CONTROL(dev));
			enable_irq(dev->irq);
			return OK;
		}
	}
	return OK;
}

/* PLIP_SEND --- send a byte (two nibbles)
   Returns OK on success, TIMEOUT when timeout    */
inline static int
plip_send(unsigned short nibble_timeout, unsigned short data_addr,
	  enum plip_nibble_state *ns_p, unsigned char data)
{
	unsigned char c0;
	unsigned int cx;

	switch (*ns_p) {
	case PLIP_NB_BEGIN:
		outb((data & 0x0f), data_addr);
		*ns_p = PLIP_NB_1;

	case PLIP_NB_1:
		outb(0x10 | (data & 0x0f), data_addr);
		cx = nibble_timeout;
		data_addr++;
		while (1) {
			c0 = inb(data_addr);
			if ((c0 & 0x80) == 0)
				break;
			if (--cx == 0)
				return TIMEOUT;
			udelay(PLIP_DELAY_UNIT);
		}
		outb(0x10 | (data >> 4), --data_addr);
		*ns_p = PLIP_NB_2;

	case PLIP_NB_2:
		outb((data >> 4), data_addr);
		data_addr++;
		cx = nibble_timeout;
		while (1) {
			c0 = inb(data_addr);
			if (c0 & 0x80)
				break;
			if (--cx == 0)
				return TIMEOUT;
			udelay(PLIP_DELAY_UNIT);
		}
		data_addr--;
		*ns_p = PLIP_NB_BEGIN;
		return OK;
	}
	return OK;
}

/* PLIP_SEND_PACKET --- send a packet */
static int
plip_send_packet(struct device *dev, struct net_local *nl,
		 struct plip_local *snd, struct plip_local *rcv)
{
	unsigned short data_addr = PAR_DATA(dev);
	unsigned short nibble_timeout = nl->nibble;
	unsigned char *lbuf;
	unsigned char c0;
	unsigned int cx;

	if (snd->skb == NULL || (lbuf = snd->skb->data) == NULL) {
		printk("%s: send skb lost\n", dev->name);
		snd->state = PLIP_PK_DONE;
		snd->skb = NULL;
		return ERROR;
	}

	switch (snd->state) {
	case PLIP_PK_TRIGGER:
		if ((inb(PAR_STATUS(dev)) & 0xf8) != 0x80)
			return TIMEOUT;

		/* Trigger remote rx interrupt. */
		outb(0x08, data_addr);
		cx = nl->trigger;
		while (1) {
			udelay(PLIP_DELAY_UNIT);
			cli();
			if (nl->connection == PLIP_CN_RECEIVE) {
				sti();
				/* interrupted */
				nl->enet_stats.collisions++;
				if (net_debug > 1)
					printk("%s: collision.\n", dev->name);
				return OK;
			}
			c0 = inb(PAR_STATUS(dev));
			if (c0 & 0x08) {
				disable_irq(dev->irq);
				outb(PAR_INTR_OFF, PAR_CONTROL(dev));
				if (net_debug > 2)
					printk("%s: send start\n", dev->name);
				snd->state = PLIP_PK_LENGTH_LSB;
				snd->nibble = PLIP_NB_BEGIN;
				nl->timeout_count = 0;
				sti();
				break;
			}
			sti();
			if (--cx == 0) {
				outb(0x00, data_addr);
				return TIMEOUT;
			}
		}

	case PLIP_PK_LENGTH_LSB:
		if (plip_send(nibble_timeout, data_addr,
			      &snd->nibble, snd->length.b.lsb))
			return TIMEOUT;
		snd->state = PLIP_PK_LENGTH_MSB;

	case PLIP_PK_LENGTH_MSB:
		if (plip_send(nibble_timeout, data_addr,
			      &snd->nibble, snd->length.b.msb))
			return TIMEOUT;
		snd->state = PLIP_PK_DATA;
		snd->byte = 0;
		snd->checksum = 0;

	case PLIP_PK_DATA:
		do
			if (plip_send(nibble_timeout, data_addr,
				      &snd->nibble, lbuf[snd->byte]))
				return TIMEOUT;
		while (++snd->byte < snd->length.h);
		do
			snd->checksum += lbuf[--snd->byte];
		while (snd->byte);
		snd->state = PLIP_PK_CHECKSUM;

	case PLIP_PK_CHECKSUM:
		if (plip_send(nibble_timeout, data_addr,
			      &snd->nibble, snd->checksum))
			return TIMEOUT;

		dev_kfree_skb(snd->skb, FREE_WRITE);
		nl->enet_stats.tx_packets++;
		snd->state = PLIP_PK_DONE;

	case PLIP_PK_DONE:
		/* Close the connection */
		outb (0x00, data_addr);
		snd->skb = NULL;
		if (net_debug > 2)
			printk("%s: send end\n", dev->name);
		nl->connection = PLIP_CN_CLOSING;
		nl->is_deferred = 1;
		queue_task(&nl->deferred, &tq_timer);
		outb(PAR_INTR_ON, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		return OK;
	}
	return OK;
}

static int
plip_connection_close(struct device *dev, struct net_local *nl,
		      struct plip_local *snd, struct plip_local *rcv)
{
	cli();
	if (nl->connection == PLIP_CN_CLOSING) {
		nl->connection = PLIP_CN_NONE;
		dev->tbusy = 0;
		mark_bh(NET_BH);
	}
	sti();
	if (nl->should_relinquish) {
		nl->should_relinquish = nl->port_owner = 0;
		parport_release(nl->pardev);
	}
	return OK;
}

/* PLIP_ERROR --- wait till other end settled */
static int
plip_error(struct device *dev, struct net_local *nl,
	   struct plip_local *snd, struct plip_local *rcv)
{
	unsigned char status;

	status = inb(PAR_STATUS(dev));
	if ((status & 0xf8) == 0x80) {
		if (net_debug > 2)
			printk("%s: reset interface.\n", dev->name);
		nl->connection = PLIP_CN_NONE;
		nl->should_relinquish = 0;
		dev->tbusy = 0;
		dev->interrupt = 0;
		outb(PAR_INTR_ON, PAR_CONTROL(dev));
		enable_irq(dev->irq);
		mark_bh(NET_BH);
	} else {
		nl->is_deferred = 1;
		queue_task(&nl->deferred, &tq_timer);
	}

	return OK;
}

/* Handle the parallel port interrupts. */
static void
plip_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct device *dev = (struct device *) irq2dev_map[irq];
	struct net_local *nl;
	struct plip_local *rcv;
	unsigned char c0;

	if (dev == NULL) {
		printk ("plip_interrupt: irq %d for unknown device.\n", irq);
		return;
	}

	nl = (struct net_local *)dev->priv;
	rcv = &nl->rcv_data;

	if (dev->interrupt)
		return;

	c0 = inb(PAR_STATUS(dev));
	if ((c0 & 0xf8) != 0xc0) {
		if (net_debug > 1)
			printk("%s: spurious interrupt\n", dev->name);
		return;
	}
	dev->interrupt = 1;
	if (net_debug > 3)
		printk("%s: interrupt.\n", dev->name);

	cli();
	switch (nl->connection) {
	case PLIP_CN_CLOSING:
		dev->tbusy = 0;
	case PLIP_CN_NONE:
	case PLIP_CN_SEND:
		dev->last_rx = jiffies;
		rcv->state = PLIP_PK_TRIGGER;
		nl->connection = PLIP_CN_RECEIVE;
		nl->timeout_count = 0;
		queue_task(&nl->immediate, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
		sti();
		break;

	case PLIP_CN_RECEIVE:
		sti();
		printk("%s: receive interrupt when receiving packet\n", dev->name);
		break;

	case PLIP_CN_ERROR:
		sti();
		printk("%s: receive interrupt in error state\n", dev->name);
		break;
	}
}

/* We don't need to send arp, for plip is point-to-point. */
static int
plip_rebuild_header(struct sk_buff *skb)
{
	struct device *dev = skb->dev;
	struct net_local *nl = (struct net_local *)dev->priv;
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	int i;

	if ((dev->flags & IFF_NOARP)==0)
		return nl->orig_rebuild_header(skb);

	if (eth->h_proto != __constant_htons(ETH_P_IP)
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		&& eth->h_proto != __constant_htons(ETH_P_IPV6)
#endif
	) {
		printk("plip_rebuild_header: Don't know how to resolve type %d addresses?\n", (int)eth->h_proto);
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
	}

	for (i=0; i < ETH_ALEN - sizeof(u32); i++)
		eth->h_dest[i] = 0xfc;
#if 0
	*(u32 *)(eth->h_dest+i) = dst;
#else
	/* Do not want to include net/route.h here.
	 * In any case, it is TOP of silliness to emulate
	 * hardware addresses on PtP link. --ANK
	 */
	*(u32 *)(eth->h_dest+i) = 0;
#endif
	return 0;
}

static int
plip_tx_packet(struct sk_buff *skb, struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct plip_local *snd = &nl->snd_data;

	if (dev->tbusy)
		return 1;

	/* We may need to grab the bus */
	if (!nl->port_owner) {
		if (parport_claim(nl->pardev))
			return 1;
		nl->port_owner = 1;
	}

	/* If some higher layer thinks we've missed an tx-done interrupt
	   we are passed NULL. Caution: dev_tint() handles the cli()/sti()
	   itself. */
	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	if (skb->len > dev->mtu + dev->hard_header_len) {
		printk("%s: packet too big, %d.\n", dev->name, (int)skb->len);
		dev->tbusy = 0;
		return 0;
	}

	if (net_debug > 2)
		printk("%s: send request\n", dev->name);

	cli();
	dev->trans_start = jiffies;
	snd->skb = skb;
	snd->length.h = skb->len;
	snd->state = PLIP_PK_TRIGGER;
	if (nl->connection == PLIP_CN_NONE) {
		nl->connection = PLIP_CN_SEND;
		nl->timeout_count = 0;
	}
	queue_task(&nl->immediate, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	sti();

	return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
 */
static int
plip_open(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	int i;

	/* Grab the port */
	if (!nl->port_owner) {
		if (parport_claim(nl->pardev)) return -EAGAIN;
		nl->port_owner = 1;
	}

	nl->should_relinquish = 0;

	/* Clear the data port. */
	outb (0x00, PAR_DATA(dev));

	/* Enable rx interrupt. */
	outb(PAR_INTR_ON, PAR_CONTROL(dev));

	/* Initialize the state machine. */
	nl->rcv_data.state = nl->snd_data.state = PLIP_PK_DONE;
	nl->rcv_data.skb = nl->snd_data.skb = NULL;
	nl->connection = PLIP_CN_NONE;
	nl->is_deferred = 0;

	/* Fill in the MAC-level header. */
	for (i=0; i < ETH_ALEN - sizeof(u32); i++)
		dev->dev_addr[i] = 0xfc;
	/* Ugh, this is like death. */
	*(u32 *)(dev->dev_addr+i) = dev->pa_addr;

	dev->interrupt = 0;
	dev->start = 1;
	dev->tbusy = 0;
	irq2dev_map[dev->irq] = dev;

	MOD_INC_USE_COUNT;
	return 0;
}

/* The inverse routine to plip_open (). */
static int
plip_close(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct plip_local *snd = &nl->snd_data;
	struct plip_local *rcv = &nl->rcv_data;

	dev->tbusy = 1;
	dev->start = 0;
	cli();
	irq2dev_map[dev->irq] = NULL;
	sti();
#ifdef NOTDEF
	outb(0x00, PAR_DATA(dev));
#endif
	nl->is_deferred = 0;
	nl->connection = PLIP_CN_NONE;
	if (nl->port_owner) {
		parport_release(nl->pardev);
		nl->port_owner = 0;
	}

	snd->state = PLIP_PK_DONE;
	if (snd->skb) {
		dev_kfree_skb(snd->skb, FREE_WRITE);
		snd->skb = NULL;
	}
	rcv->state = PLIP_PK_DONE;
	if (rcv->skb) {
		kfree_skb(rcv->skb, FREE_READ);
		rcv->skb = NULL;
	}

#ifdef NOTDEF
	/* Reset. */
	outb(0x00, PAR_CONTROL(dev));
#endif
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
plip_preempt(void *handle)
{
	struct device *dev = (struct device *)handle;
	struct net_local *nl = (struct net_local *)dev->priv;

	/* Stand our ground if a datagram is on the wire */
	if (nl->connection != PLIP_CN_NONE) {
		nl->should_relinquish = 1;
		return 1;
	}

	nl->port_owner = 0;	/* Remember that we released the bus */
	return 0;
}

static void
plip_wakeup(void *handle)
{
	struct device *dev = (struct device *)handle;
	struct net_local *nl = (struct net_local *)dev->priv;

	if (nl->port_owner) {
		/* Why are we being woken up? */
		printk(KERN_DEBUG "%s: why am I being woken up?\n", dev->name);
		if (!parport_claim(nl->pardev))
			/* bus_owner is already set (but why?) */
			printk(KERN_DEBUG "%s: I'm broken.\n", dev->name);
		else
			return;
	}
	
	if (!(dev->flags & IFF_UP))
		/* Don't need the port when the interface is down */
		return;

	if (!parport_claim(nl->pardev)) {
		nl->port_owner = 1;
		irq2dev_map[dev->irq] = dev;
		/* Clear the data port. */
		outb (0x00, PAR_DATA(dev));
	}

	return;
}

static struct net_device_stats *
plip_get_stats(struct device *dev)
{
	struct net_local *nl = (struct net_local *)dev->priv;
	struct net_device_stats *r = &nl->enet_stats;

	return r;
}

static int
plip_config(struct device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (map->base_addr != (unsigned long)-1
	    && map->base_addr != dev->base_addr)
		printk("%s: You cannot change base_addr of this interface (ignored).\n", dev->name);

	if (map->irq != (unsigned char)-1)
		dev->irq = map->irq;
	return 0;
}

static int
plip_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *nl = (struct net_local *) dev->priv;
	struct plipconf *pc = (struct plipconf *) &rq->ifr_data;

	switch(pc->pcmd) {
	case PLIP_GET_TIMEOUT:
		pc->trigger = nl->trigger;
		pc->nibble  = nl->nibble;
		break;
	case PLIP_SET_TIMEOUT:
		nl->trigger = pc->trigger;
		nl->nibble  = pc->nibble;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int parport[PLIP_MAX] = { -1, };
static int timid = 0;

MODULE_PARM(parport, "1-" __MODULE_STRING(PLIP_MAX) "i");
MODULE_PARM(timid, "1i");

static struct device *dev_plip[PLIP_MAX] = { NULL, };

#ifdef MODULE
void
cleanup_module(void)
{
	int i;

	for (i=0; i < PLIP_MAX; i++) {
		if (dev_plip[i]) {
			struct net_local *nl =
				(struct net_local *)dev_plip[i]->priv;
			unregister_netdev(dev_plip[i]);
			if (nl->port_owner)
				parport_release(nl->pardev);
			parport_unregister_device(nl->pardev);
			kfree(dev_plip[i]->priv);
			kfree(dev_plip[i]->name);
			kfree(dev_plip[i]);
			dev_plip[i] = NULL;
		}
	}
}

#define plip_init  init_module

#else /* !MODULE */

static int parport_ptr = 0;

void plip_setup(char *str, int *ints)
{
	/* Ugh. */
	if (!strncmp(str, "parport", 7)) {
		int n = simple_strtoul(str+7, NULL, 10);
		if (parport_ptr < PLIP_MAX)
			parport[parport_ptr++] = n;
		else
			printk(KERN_INFO "plip: too many ports, %s ignored.\n",
			       str);
	} else if (!strcmp(str, "timid")) {
		timid = 1;
	} else {
		if (ints[0] == 0 || ints[1] == 0) {
			/* disable driver on "parport=" or "parport=0" */
			parport[0] = -2;
		} else {
			printk(KERN_WARNING "warning: 'plip=0x%x' ignored\n", 
			       ints[1]);
		}
	}
}

#endif /* MODULE */

static int inline 
plip_searchfor(int list[], int a)
{
	int i;
	for (i = 0; i < 3 && list[i] != -1; i++) {
		if (list[i] == a) return 1;
	}
	return 0;
}

__initfunc(int
plip_init(void))
{
	struct parport *pb = parport_enumerate();
	int devices=0;
	int i=0;

	if (parport[0] == -2)
		return 0;

	if (parport[0] != -1 && timid) {
		printk(KERN_WARNING "plip: warning, ignoring `timid' since specific ports given.\n");
		timid = 0;
	}

	/* When user feeds parameters, use them */
	while (pb) {
		if ((parport[0] == -1 && (!timid || !pb->devices)) || 
		    plip_searchfor(parport, i)) {
			if (i == PLIP_MAX) {
				printk(KERN_ERR "plip: too many devices\n");
				break;
			}
			dev_plip[i] = kmalloc(sizeof(struct device),
					      GFP_KERNEL);
			if (!dev_plip[i]) {
				printk(KERN_ERR "plip: memory squeeze\n");
				break;
			}
			memset(dev_plip[i], 0, sizeof(struct device));
			dev_plip[i]->name = 
				kmalloc(strlen("plipXXX"), GFP_KERNEL);
			if (!dev_plip[i]->name) {
				printk(KERN_ERR "plip: memory squeeze.\n");
				kfree(dev_plip[i]);
				break;
			}
			sprintf(dev_plip[i]->name, "plip%d", i);
			dev_plip[i]->priv = pb;
			if (plip_init_dev(dev_plip[i],pb) || register_netdev(dev_plip[i])) {
				kfree(dev_plip[i]->name);
				kfree(dev_plip[i]);
			} else {
				devices++;
			}
		}
		i++;
		pb = pb->next;
  	}

	if (devices == 0) {
		printk(KERN_INFO "plip: no devices registered\n");
		return -EIO;
	}
	return 0;
}

/*
 * Local variables:
 * compile-command: "gcc -DMODULE -DMODVERSIONS -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -g -fomit-frame-pointer -pipe -m486 -c plip.c"
 * End:
 */
