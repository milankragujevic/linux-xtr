/*
 * control.c: Console support for PowerMac "control" display adaptor.
 *
 * Copyright (C) 1996 Paul Mackerras.
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/vc_ioctl.h>
#include <linux/nvram.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <linux/selection.h>
#include "pmac-cons.h"
#include "control.h"

/*
 * Structure of the registers for the RADACAL colormap device.
 */
struct cmap_regs {
	unsigned char addr;
	char pad1[15];
	unsigned char d1;
	char pad2[15];
	unsigned char d2;
	char pad3[15];
	unsigned char lut;
	char pad4[15];
};

/*
 * Structure of the registers for the "control" display adaptor".
 */
#define PAD(x)	char x[12]

struct preg {			/* padded register */
	unsigned r;
	char pad[12];
};

struct control_regs {
	struct preg vcount;	/* vertical counter */
	/* Vertical parameters are in units of 1/2 scan line */
	struct preg vswin;	/* between vsblank and vssync */
	struct preg vsblank;	/* vert start blank */
	struct preg veblank;	/* vert end blank (display start) */
	struct preg vewin;	/* between vesync and veblank */
	struct preg vesync;	/* vert end sync */
	struct preg vssync;	/* vert start sync */
	struct preg vperiod;	/* vert period */
	struct preg reg8;
	/* Horizontal params are in units of 2 pixels */
	struct preg hperiod;	/* horiz period - 2 */
	struct preg hsblank;	/* horiz start blank */
	struct preg heblank;	/* horiz end blank */
	struct preg hesync;	/* horiz end sync */
	struct preg hssync;	/* horiz start sync */
	struct preg rege;
	struct preg regf;
	struct preg reg10;
	struct preg reg11;
	struct preg ctrl;	/* display control */
	struct preg start_addr;	/* start address: 5 lsbs zero */
	struct preg pitch;	/* addrs diff between scan lines */
	struct preg mon_sense;	/* monitor sense bits */
	struct preg flags;
	struct preg mode;
	struct preg reg18;
	struct preg reg19;
	struct preg res[6];
};

static void set_control_clock(unsigned char *params);
static int read_control_sense(void);
static int control_vram_reqd(int vmode, int cmode);

static int total_vram;		/* total amount of video memory, bytes */
static unsigned char *frame_buffer;
static struct cmap_regs *cmap_regs;
static struct control_regs *disp_regs;
static int control_use_bank2;

/*
 * Register initialization tables for the control display.
 *
 * Dot clock rate is
 * 3.9064MHz * 2**clock_params[2] * clock_params[1] / clock_params[0].
 *
 * The values for vertical frequency (V) in the comments below
 * are the values measured using the modes under MacOS.
 */
struct control_regvals {
	int	pitch[3];		/* bytes/line, indexed by color_mode */
	int	offset[3];		/* first pixel address */
	unsigned regs[16];		/* for vswin .. reg10 */
	unsigned char mode[3];		/* indexed by color_mode */
	unsigned char radacal_ctrl[3];
	unsigned char clock_params[3];
};

/* Register values for 1280x1024, 75Hz mode (20) */
static struct control_regvals control_reg_init_20 = {
	{ 1280, 2560, 0 },
	{ 0x10, 0x20, 0 },
	{ 2129, 2128, 80, 42, 4, 2130, 2132, 88,
	  420, 411, 91, 35, 421, 18, 211, 386, },
	{ 1, 1, 1},
	{ 0x50, 0x64, 0x64 },
	{ 13, 56, 3 }	/* pixel clock = 134.61MHz for V=74.81Hz */
};

/* Register values for 1280x960, 75Hz mode (19) */
static struct control_regvals control_reg_init_19 = {
	{ 1280, 2560, 0 },
	{ 0x10, 0x20, 0 },
	{ 1997, 1996, 76, 40, 4, 1998, 2000, 86,
	  418, 409, 89, 35, 419, 18, 210, 384, },
	{ 1, 1, 1 },
	{ 0x50, 0x64, 0x64 },
	{ 31, 125, 3 }	/* pixel clock = 126.01MHz for V=75.01 Hz */
};

/* Register values for 1152x870, 75Hz mode (18) */
static struct control_regvals control_reg_init_18 = {
	{ 1152, 2304, 4608 },
	{ 0x10, 0x28, 0x50 },
	{ 1825, 1822, 82, 43, 4, 1828, 1830, 120,
	  726, 705, 129, 63, 727, 32, 364, 664 },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 19, 61, 3 }	/* pixel clock = 100.33MHz for V=75.31Hz */
};

/* Register values for 1024x768, 75Hz mode (17) */
static struct control_regvals control_reg_init_17 = {
	{ 1024, 2048, 4096 },
	{ 0x10, 0x28, 0x50 },
	{ 1603, 1600, 64, 34, 4, 1606, 1608, 120,
	  662, 641, 129, 47, 663, 24, 332, 616 },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 11, 28, 3 }	/* pixel clock = 79.55MHz for V=74.50Hz */
};

/* Register values for 1024x768, 72Hz mode (15) */
static struct control_regvals control_reg_init_15 = {
	{ 1024, 2048, 4096 },
	{ 0x10, 0x28, 0x50 },
	{ 1607, 1604, 68, 39, 10, 1610, 1612, 132,
	  670, 653, 141, 67, 671, 34, 336, 604, },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 12, 30, 3 }	/* pixel clock = 78.12MHz for V=72.12Hz */
};

/* Register values for 1024x768, 60Hz mode (14) */
static struct control_regvals control_reg_init_14 = {
	{ 1024, 2048, 4096 },
	{ 0x10, 0x28, 0x50 },
	{ 1607, 1604, 68, 39, 10, 1610, 1612, 132,
	  670, 653, 141, 67, 671, 34, 336, 604, },
	{ 2, 1, 1 },
	{ 0x10, 0x14, 0x28 },
	{ 15, 31, 3 }	/* pixel clock = 64.58MHz for V=59.62Hz */
};

/* Register values for 832x624, 75Hz mode (13) */
static struct control_regvals control_reg_init_13 = {
	{ 832, 1664, 3328 },
	{ 0x10, 0x28, 0x50 },
	{ 1331, 1330, 82, 43, 4, 1332, 1334, 128,
	  574, 553, 137, 31, 575, 16, 288, 544 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 23, 42, 3 }	/* pixel clock = 57.07MHz for V=74.27Hz */
};

/* Register values for 800x600, 75Hz mode (12) */
static struct control_regvals control_reg_init_12 = {
	{ 800, 1600, 3200 },
	{ 0x10, 0x28, 0x50 },
	{ 1247, 1246, 46, 25, 4, 1248, 1250, 104,
	  526, 513, 113, 39, 527, 20, 264, 488, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 7, 11, 3 }	/* pixel clock = 49.11MHz for V=74.40Hz */
};

/* Register values for 800x600, 72Hz mode (11) */
static struct control_regvals control_reg_init_11 = {
	{ 800, 1600, 3200 },
	{ 0x10, 0x28, 0x50 },
	{ 1293, 1256, 56, 33, 10, 1330, 1332, 76,
	  518, 485, 85, 59, 519, 30, 260, 460, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 17, 27, 3 }	/* pixel clock = 49.63MHz for V=71.66Hz */
};

/* Register values for 800x600, 60Hz mode (10) */
static struct control_regvals control_reg_init_10 = {
	{ 800, 1600, 3200 },
	{ 0x10, 0x28, 0x50 },
	{ 1293, 1256, 56, 33, 10, 1330, 1332, 76,
	  518, 485, 85, 59, 519, 30, 260, 460, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 20, 53, 2 }	/* pixel clock = 41.41MHz for V=59.78Hz */
};

/* Register values for 640x870, 75Hz Full Page Display (7) */
static struct control_regvals control_reg_init_7 = {
        { 640, 1280, 2560 },
	{ 0x10, 0x30, 0x68 },
	{ 0x727, 0x724, 0x58, 0x2e, 0x4, 0x72a, 0x72c, 0x40,
	  0x19e, 0x18c, 0x4c, 0x27, 0x19f, 0x14, 0xd0, 0x178 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 9, 33, 2 }	/* pixel clock = 57.29MHz for V=75.01Hz */
};

/* Register values for 640x480, 67Hz mode (6) */
static struct control_regvals control_reg_init_6 = {
	{ 640, 1280, 2560 },
	{ 0, 8, 0x10 },
	{ 1045, 1042, 82, 43, 4, 1048, 1050, 72,
	  430, 393, 73, 31, 431, 16, 216, 400 },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 14, 27, 2 }	/* pixel clock = 30.13MHz for V=66.43Hz */
};

/* Register values for 640x480, 60Hz mode (5) */
static struct control_regvals control_reg_init_5 = {
	{ 640, 1280, 2560 },
	{ 0x10, 0x28, 0x50 },
	{ 1037, 1026, 66, 34, 2, 1048, 1050, 56,
	  398, 385, 65, 47, 399, 24, 200, 352, },
	{ 2, 1, 0 }, { 0x10, 0x14, 0x18 },
	{ 23, 37, 2 }	/* pixel clock = 25.14MHz for V=59.85Hz */
};

static struct control_regvals *control_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&control_reg_init_5,
	&control_reg_init_6,
	&control_reg_init_7,
	NULL, NULL,
	&control_reg_init_10,
	&control_reg_init_11,
	&control_reg_init_12,
	&control_reg_init_13,
	&control_reg_init_14,
	&control_reg_init_15,
	NULL,
	&control_reg_init_17,
	&control_reg_init_18,
	&control_reg_init_19,
	&control_reg_init_20
};

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int
read_control_sense()
{
	int sense;

	out_le32(&disp_regs->mon_sense.r, 7);	/* drive all lines high */
	__delay(200);
	out_le32(&disp_regs->mon_sense.r, 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(&disp_regs->mon_sense.r) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(&disp_regs->mon_sense.r, 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(&disp_regs->mon_sense.r) & 0xc0) >> 2;
	out_le32(&disp_regs->mon_sense.r, 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(&disp_regs->mon_sense.r) & 0x100) >> 5)
		| ((in_le32(&disp_regs->mon_sense.r) & 0x40) >> 4);
	out_le32(&disp_regs->mon_sense.r, 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(&disp_regs->mon_sense.r) & 0x180) >> 7;

	out_le32(&disp_regs->mon_sense.r, 077);	/* turn off drivers */
	return sense;
}

static inline int control_vram_reqd(int vmode, int cmode)
{
	return vmode_attrs[vmode-1].vres
		* control_reg_init[vmode-1]->pitch[cmode];
}

void
map_control_display(struct device_node *dp)
{
	int i, sense;
	unsigned long addr, size;
	int bank1, bank2;

	if (dp->next != 0)
		printk("Warning: only using first control display device\n");
	if (dp->n_addrs != 2)
		panic("expecting 2 addresses for control (got %d)", dp->n_addrs);

#if 0
	printk("pmac_display_init: node = %p, addrs =", dp->node);
	for (i = 0; i < dp->n_addrs; ++i)
		printk(" %x(%x)", dp->addrs[i].address, dp->addrs[i].size);
	printk(", intrs =");
	for (i = 0; i < dp->n_intrs; ++i)
		printk(" %x", dp->intrs[i]);
	printk("\n");
#endif

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x800000) {
			/* use the big-endian aperture (??) */
			addr += 0x800000;
			/* map at most 8MB for the frame buffer */
			frame_buffer = ioremap(addr, 0x800000);
		} else {
			disp_regs = ioremap(addr, size);
		}
	}
	cmap_regs = ioremap(0xf301b000, 0x1000);	/* XXX not in prom? */

	/* Work out which banks of VRAM we have installed. */
	frame_buffer[0] = 0x5a;
	frame_buffer[1] = 0xc7;
	bank1 = frame_buffer[0] == 0x5a && frame_buffer[1] == 0xc7;
	frame_buffer[0x600000] = 0xa5;
	frame_buffer[0x600001] = 0x38;
	bank2 = frame_buffer[0x600000] == 0xa5 && frame_buffer[0x600001] == 0x38;
	total_vram = (bank1 + bank2) * 0x200000;
	/* If we don't have bank 1 installed, we hope we have bank 2 :-) */
	control_use_bank2 = !bank1;
	if (control_use_bank2)
		frame_buffer += 0x600000;

	sense = read_control_sense();
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		if (video_mode <= 0 || video_mode > VMODE_MAX
		    || control_reg_init[video_mode-1] == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);
	if (control_reg_init[video_mode-1] == 0)
		video_mode = VMODE_640_480_60;

	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	if (color_mode == CMODE_NVRAM)
		color_mode = nvram_read_byte(NV_CMODE);
	if (color_mode < CMODE_8 || color_mode > CMODE_32)
		color_mode = CMODE_8;
	while (color_mode > CMODE_8
	       && control_vram_reqd(video_mode, color_mode) > total_vram)
		--color_mode;

	printk("Monitor sense value = 0x%x, ", sense);
}

static void
set_control_clock(unsigned char *params)
{
	struct adb_request req;
	int i;

	for (i = 0; i < 3; ++i) {
		cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
			     0x50, i + 1, params[i]);
		while (!req.complete)
			cuda_poll();
	}
}

void
control_init()
{
	struct preg *rp;
	int i, yoff, hres;
	int ctrl, flags;
	unsigned *p;
	struct control_regvals *init;

	if (video_mode <= 0 || video_mode > VMODE_MAX
	    || (init = control_reg_init[video_mode-1]) == 0)
		panic("control: display mode %d not supported", video_mode);
	n_scanlines = vmode_attrs[video_mode-1].vres;
	hres = vmode_attrs[video_mode-1].hres;
	pixel_size = 1 << color_mode;
	line_pitch = init->pitch[color_mode];
	row_pitch = line_pitch * 16;

	if (control_vram_reqd(video_mode, color_mode) > 0x200000)
		flags = 0x51;
	else if (control_use_bank2)
		flags = 0x39;
	else
		flags = 0x31;
	if (video_mode >= VMODE_1280_960_75 && color_mode >= CMODE_16)
		ctrl = 0x7f;
	else
		ctrl = 0x3b;

	/* Initialize display timing registers */
	out_le32(&disp_regs->ctrl.r, 0x43b);
	set_control_clock(init->clock_params);
	cmap_regs->addr = 0x20; cmap_regs->d2 = init->radacal_ctrl[color_mode];
	cmap_regs->addr = 0x21; cmap_regs->d2 = control_use_bank2? 0: 1;
	cmap_regs->addr = 0x10; cmap_regs->d2 = 0;
	cmap_regs->addr = 0x11; cmap_regs->d2 = 0;
	rp = &disp_regs->vswin;
	for (i = 0; i < 16; ++i, ++rp)
		out_le32(&rp->r, init->regs[i]);
	out_le32(&disp_regs->pitch.r, line_pitch);
	out_le32(&disp_regs->mode.r, init->mode[color_mode]);
	out_le32(&disp_regs->flags.r, flags);
	out_le32(&disp_regs->start_addr.r, 0);
	out_le32(&disp_regs->reg18.r, 0x1e5);
	out_le32(&disp_regs->reg19.r, 0);

	pmac_init_palette();	/* Initialize colormap */

	/* Turn on display */
	out_le32(&disp_regs->ctrl.r, ctrl);

	yoff = (n_scanlines % 16) / 2;
	fb_start = frame_buffer + yoff * line_pitch + init->offset[color_mode];

	/* Clear screen */
	p = (unsigned *) (frame_buffer + init->offset[color_mode]);
	for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
		*p++ = 0;

	display_info.height = n_scanlines;
	display_info.width = hres;
	display_info.depth = pixel_size * 8;
	display_info.pitch = line_pitch;
	display_info.mode = video_mode;
	strncpy(display_info.name, "control", sizeof(display_info.name));
	display_info.fb_address = (unsigned long) frame_buffer + init->offset[color_mode];
	display_info.cmap_adr_address = (unsigned long) &cmap_regs->addr;
	display_info.cmap_data_address = (unsigned long) &cmap_regs->lut;
	display_info.disp_reg_address = (unsigned long) &disp_regs;
}

int
control_setmode(struct vc_mode *mode, int doit)
{
	int cmode;

	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || control_reg_init[mode->mode-1] == 0)
		return -EINVAL;
	switch (mode->depth) {
	case 24:
	case 32:
		cmode = CMODE_32;
		break;
	case 16:
		cmode = CMODE_16;
		break;
	case 8:
	case 0:		/* (default) */
		cmode = CMODE_8;
		break;
	default:
		return -EINVAL;
	}
	if (control_vram_reqd(mode->mode, cmode) > total_vram)
		return -EINVAL;
	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		control_init();
	}
	return 0;
}

void
control_set_palette(unsigned char red[], unsigned char green[],
		    unsigned char blue[], int index, int ncolors)
{
	int i;

	for (i = 0; i < ncolors; ++i) {
		cmap_regs->addr = index + i;	eieio();
		cmap_regs->lut = red[i];	eieio();
		cmap_regs->lut = green[i];	eieio();
		cmap_regs->lut = blue[i];	eieio();
	}
}

void
control_set_blanking(int blank_mode)
{
	int ctrl;

	ctrl = ld_le32(&disp_regs->ctrl.r) | 0x33;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&disp_regs->ctrl.r, ctrl);
}
