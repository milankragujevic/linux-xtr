#ifndef __SOUND_PCM_H
#define __SOUND_PCM_H

/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/asound.h>
#include <linux/poll.h>

typedef sndrv_pcm_uframes_t snd_pcm_uframes_t;
typedef sndrv_pcm_sframes_t snd_pcm_sframes_t;
typedef enum sndrv_pcm_class snd_pcm_class_t;
typedef enum sndrv_pcm_subclass snd_pcm_subclass_t;
typedef enum sndrv_pcm_stream snd_pcm_stream_t;
typedef enum sndrv_pcm_access snd_pcm_access_t;
typedef enum sndrv_pcm_format snd_pcm_format_t;
typedef enum sndrv_pcm_subformat snd_pcm_subformat_t;
typedef enum sndrv_pcm_state snd_pcm_state_t;
typedef union sndrv_pcm_sync_id snd_pcm_sync_id_t;
typedef struct sndrv_pcm_info snd_pcm_info_t;
typedef enum sndrv_pcm_hw_param snd_pcm_hw_param_t;
typedef struct sndrv_pcm_hw_params snd_pcm_hw_params_t;
typedef enum sndrv_pcm_start snd_pcm_start_t;
typedef enum sndrv_pcm_xrun snd_pcm_xrun_t;
typedef enum sndrv_pcm_tstamp snd_pcm_tstamp_t;
typedef struct sndrv_pcm_sw_params snd_pcm_sw_params_t;
typedef struct sndrv_pcm_channel_info snd_pcm_channel_info_t;
typedef struct sndrv_pcm_status snd_pcm_status_t;
typedef struct sndrv_pcm_mmap_status snd_pcm_mmap_status_t;
typedef struct sndrv_pcm_mmap_control snd_pcm_mmap_control_t;

#define _snd_pcm_substream_chip(substream) ((substream)->pcm->private_data)
#define snd_pcm_substream_chip(substream) snd_magic_cast1(chip_t, _snd_pcm_substream_chip(substream), return -ENXIO)
#define _snd_pcm_chip(pcm) ((pcm)->private_data)
#define snd_pcm_chip(pcm) snd_magic_cast1(chip_t, _snd_pcm_chip(pcm), return -ENXIO)

typedef struct _snd_pcm_file snd_pcm_file_t;
typedef struct _snd_pcm_runtime snd_pcm_runtime_t;

#ifdef CONFIG_SND_OSSEMUL
#include "pcm_oss.h"
#endif

/*
 *  Hardware (lowlevel) section
 */

typedef struct _snd_pcm_hardware {
	unsigned int info;		/* SNDRV_PCM_INFO_* */
	unsigned int formats;		/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int rate_min;		/* min rate */
	unsigned int rate_max;		/* max rate */
	unsigned int channels_min;	/* min channels */
	unsigned int channels_max;	/* max channels */
	size_t buffer_bytes_max;	/* max buffer size */
	size_t period_bytes_min;	/* min period size */
	size_t period_bytes_max;	/* max period size */
	unsigned int periods_min;	/* min # of periods */
	unsigned int periods_max;	/* max # of periods */
	size_t fifo_size;		/* fifo size in bytes */
} snd_pcm_hardware_t;

typedef struct _snd_pcm_ops {
	int (*open)(snd_pcm_substream_t *substream);
	int (*close)(snd_pcm_substream_t *substream);
	int (*ioctl)(snd_pcm_substream_t * substream,
		     unsigned int cmd, void *arg);
	int (*hw_params)(snd_pcm_substream_t * substream, snd_pcm_hw_params_t * params);
	int (*hw_free)(snd_pcm_substream_t *substream);
	int (*prepare)(snd_pcm_substream_t * substream);
	int (*trigger)(snd_pcm_substream_t * substream, int cmd);
	snd_pcm_uframes_t (*pointer)(snd_pcm_substream_t * substream);
	int (*copy)(snd_pcm_substream_t *substream, int channel, snd_pcm_uframes_t pos,
		    void *buf, snd_pcm_uframes_t count);
	int (*silence)(snd_pcm_substream_t *substream, int channel, 
		       snd_pcm_uframes_t pos, snd_pcm_uframes_t count);
} snd_pcm_ops_t;

/*
 *
 */

#define SNDRV_PCM_DEVICES		8

#define SNDRV_PCM_IOCTL1_FALSE		((void *)0)
#define SNDRV_PCM_IOCTL1_TRUE		((void *)1)

#define SNDRV_PCM_IOCTL1_RESET		0
#define SNDRV_PCM_IOCTL1_INFO		1
#define SNDRV_PCM_IOCTL1_CHANNEL_INFO	2
#define SNDRV_PCM_IOCTL1_GSTATE		3

#define SNDRV_PCM_TRIGGER_STOP		0
#define SNDRV_PCM_TRIGGER_START		1
#define SNDRV_PCM_TRIGGER_PAUSE_PUSH	3
#define SNDRV_PCM_TRIGGER_PAUSE_RELEASE	4
#define SNDRV_PCM_TRIGGER_SUSPEND	5
#define SNDRV_PCM_TRIGGER_RESUME	6

#define SNDRV_PCM_DMA_TYPE_CONTINUOUS	0	/* continuous no-DMA memory */
#define SNDRV_PCM_DMA_TYPE_ISA		1	/* ISA continuous */
#define SNDRV_PCM_DMA_TYPE_PCI		2	/* PCI continuous */

/* If you change this don't forget to changed snd_pcm_rates table in pcm_lib.c */
#define SNDRV_PCM_RATE_5512		(1<<0)		/* 5512Hz */
#define SNDRV_PCM_RATE_8000		(1<<1)		/* 8000Hz */
#define SNDRV_PCM_RATE_11025		(1<<2)		/* 11025Hz */
#define SNDRV_PCM_RATE_16000		(1<<3)		/* 16000Hz */
#define SNDRV_PCM_RATE_22050		(1<<4)		/* 22050Hz */
#define SNDRV_PCM_RATE_32000		(1<<5)		/* 32000Hz */
#define SNDRV_PCM_RATE_44100		(1<<6)		/* 44100Hz */
#define SNDRV_PCM_RATE_48000		(1<<7)		/* 48000Hz */
#define SNDRV_PCM_RATE_64000		(1<<8)		/* 64000Hz */
#define SNDRV_PCM_RATE_88200		(1<<9)		/* 88200Hz */
#define SNDRV_PCM_RATE_96000		(1<<10)		/* 96000Hz */
#define SNDRV_PCM_RATE_176400		(1<<11)		/* 176400Hz */
#define SNDRV_PCM_RATE_192000		(1<<12)		/* 192000Hz */

#define SNDRV_PCM_RATE_CONTINUOUS	(1<<30)		/* continuous range */
#define SNDRV_PCM_RATE_KNOT		(1<<31)		/* supports more non-continuos rates */

#define SNDRV_PCM_RATE_8000_44100	(SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_11025|\
					 SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_22050|\
					 SNDRV_PCM_RATE_32000|SNDRV_PCM_RATE_44100)
#define SNDRV_PCM_RATE_8000_48000	(SNDRV_PCM_RATE_8000_44100|SNDRV_PCM_RATE_48000)
#define SNDRV_PCM_RATE_8000_96000	(SNDRV_PCM_RATE_8000_48000|SNDRV_PCM_RATE_64000|\
					 SNDRV_PCM_RATE_88200|SNDRV_PCM_RATE_96000)
#define SNDRV_PCM_RATE_8000_192000	(SNDRV_PCM_RATE_8000_96000|SNDRV_PCM_RATE_176400|\
					 SNDRV_PCM_RATE_192000)
#define SNDRV_PCM_FMTBIT_S8		(1 << SNDRV_PCM_FORMAT_S8)
#define SNDRV_PCM_FMTBIT_U8		(1 << SNDRV_PCM_FORMAT_U8)
#define SNDRV_PCM_FMTBIT_S16_LE		(1 << SNDRV_PCM_FORMAT_S16_LE)
#define SNDRV_PCM_FMTBIT_S16_BE		(1 << SNDRV_PCM_FORMAT_S16_BE)
#define SNDRV_PCM_FMTBIT_U16_LE		(1 << SNDRV_PCM_FORMAT_U16_LE)
#define SNDRV_PCM_FMTBIT_U16_BE		(1 << SNDRV_PCM_FORMAT_U16_BE)
#define SNDRV_PCM_FMTBIT_S24_LE		(1 << SNDRV_PCM_FORMAT_S24_LE)
#define SNDRV_PCM_FMTBIT_S24_BE		(1 << SNDRV_PCM_FORMAT_S24_BE)
#define SNDRV_PCM_FMTBIT_U24_LE		(1 << SNDRV_PCM_FORMAT_U24_LE)
#define SNDRV_PCM_FMTBIT_U24_BE		(1 << SNDRV_PCM_FORMAT_U24_BE)
#define SNDRV_PCM_FMTBIT_S32_LE		(1 << SNDRV_PCM_FORMAT_S32_LE)
#define SNDRV_PCM_FMTBIT_S32_BE		(1 << SNDRV_PCM_FORMAT_S32_BE)
#define SNDRV_PCM_FMTBIT_U32_LE		(1 << SNDRV_PCM_FORMAT_U32_LE)
#define SNDRV_PCM_FMTBIT_U32_BE		(1 << SNDRV_PCM_FORMAT_U32_BE)
#define SNDRV_PCM_FMTBIT_FLOAT_LE	(1 << SNDRV_PCM_FORMAT_FLOAT_LE)
#define SNDRV_PCM_FMTBIT_FLOAT_BE	(1 << SNDRV_PCM_FORMAT_FLOAT_BE)
#define SNDRV_PCM_FMTBIT_FLOAT64_LE	(1 << SNDRV_PCM_FORMAT_FLOAT64_LE)
#define SNDRV_PCM_FMTBIT_FLOAT64_BE	(1 << SNDRV_PCM_FORMAT_FLOAT64_BE)
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE (1 << SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE)
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE (1 << SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE)
#define SNDRV_PCM_FMTBIT_MU_LAW		(1 << SNDRV_PCM_FORMAT_MU_LAW)
#define SNDRV_PCM_FMTBIT_A_LAW		(1 << SNDRV_PCM_FORMAT_A_LAW)
#define SNDRV_PCM_FMTBIT_IMA_ADPCM	(1 << SNDRV_PCM_FORMAT_IMA_ADPCM)
#define SNDRV_PCM_FMTBIT_MPEG		(1 << SNDRV_PCM_FORMAT_MPEG)
#define SNDRV_PCM_FMTBIT_GSM		(1 << SNDRV_PCM_FORMAT_GSM)
#define SNDRV_PCM_FMTBIT_SPECIAL	(1 << SNDRV_PCM_FORMAT_SPECIAL)

#ifdef SNDRV_LITTLE_ENDIAN
#define SNDRV_PCM_FMTBIT_S16		SNDRV_PCM_FMTBIT_S16_LE
#define SNDRV_PCM_FMTBIT_U16		SNDRV_PCM_FMTBIT_U16_LE
#define SNDRV_PCM_FMTBIT_S24		SNDRV_PCM_FMTBIT_S24_LE
#define SNDRV_PCM_FMTBIT_U24		SNDRV_PCM_FMTBIT_U24_LE
#define SNDRV_PCM_FMTBIT_S32		SNDRV_PCM_FMTBIT_S32_LE
#define SNDRV_PCM_FMTBIT_U32		SNDRV_PCM_FMTBIT_U32_LE
#define SNDRV_PCM_FMTBIT_FLOAT		SNDRV_PCM_FMTBIT_FLOAT_LE
#define SNDRV_PCM_FMTBIT_FLOAT64	SNDRV_PCM_FMTBIT_FLOAT64_LE
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE
#endif
#ifdef SNDRV_BIG_ENDIAN
#define SNDRV_PCM_FMTBIT_S16		SNDRV_PCM_FMTBIT_S16_BE
#define SNDRV_PCM_FMTBIT_U16		SNDRV_PCM_FMTBIT_U16_BE
#define SNDRV_PCM_FMTBIT_S24		SNDRV_PCM_FMTBIT_S24_BE
#define SNDRV_PCM_FMTBIT_U24		SNDRV_PCM_FMTBIT_U24_BE
#define SNDRV_PCM_FMTBIT_S32		SNDRV_PCM_FMTBIT_S32_BE
#define SNDRV_PCM_FMTBIT_U32		SNDRV_PCM_FMTBIT_U32_BE
#define SNDRV_PCM_FMTBIT_FLOAT		SNDRV_PCM_FMTBIT_FLOAT_BE
#define SNDRV_PCM_FMTBIT_FLOAT64	SNDRV_PCM_FMTBIT_FLOAT64_BE
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE
#endif

struct _snd_pcm_file {
	snd_pcm_substream_t * substream;
	struct _snd_pcm_file * next;
};

typedef struct _snd_pcm_hw_rule snd_pcm_hw_rule_t;

typedef int (*snd_pcm_hw_rule_func_t)(snd_pcm_hw_params_t *params,
				      snd_pcm_hw_rule_t *rule);

struct _snd_pcm_hw_rule {
	unsigned int cond;
	snd_pcm_hw_rule_func_t func;
	int var;
	int deps[4];
	void *private;
};

typedef struct _snd_pcm_hw_constraints {
	unsigned int masks[SNDRV_PCM_HW_PARAM_LAST_MASK - 
			   SNDRV_PCM_HW_PARAM_FIRST_MASK + 1];
	snd_interval_t intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
			     SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	unsigned int rules_num;
	unsigned int rules_all;
	snd_pcm_hw_rule_t *rules;
} snd_pcm_hw_constraints_t;

static inline unsigned int *constrs_mask(snd_pcm_hw_constraints_t *constrs,
					snd_pcm_hw_param_t var)
{
	return &constrs->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static inline snd_interval_t *constrs_interval(snd_pcm_hw_constraints_t *constrs,
					  snd_pcm_hw_param_t var)
{
	return &constrs->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

typedef struct {
	unsigned int num;
	unsigned int den_min, den_max, den_step;
} ratnum_t;

typedef struct {
	unsigned int num_min, num_max, num_step;
	unsigned int den;
} ratden_t;

typedef struct {
	int nrats;
	ratnum_t *rats;
} snd_pcm_hw_constraint_ratnums_t;

typedef struct {
	int nrats;
	ratden_t *rats;
} snd_pcm_hw_constraint_ratdens_t;

typedef struct {
	unsigned int count;
	unsigned int *list;
	unsigned int mask;
} snd_pcm_hw_constraint_list_t;

struct _snd_pcm_runtime {
	/* -- Status -- */
	snd_pcm_substream_t *trigger_master;
	snd_timestamp_t trigger_tstamp;	/* trigger timestamp */
	int overrange;
	snd_pcm_uframes_t avail_max;
	snd_pcm_uframes_t hw_ptr_base;	/* Position at buffer restart */
	snd_pcm_uframes_t hw_ptr_interrupt; /* Position at interrupt time*/

	/* -- HW params -- */
	snd_pcm_access_t access;	/* access mode */
	snd_pcm_format_t format;	/* SNDRV_PCM_FORMAT_* */
	snd_pcm_subformat_t subformat;	/* subformat */
	unsigned int rate;		/* rate in Hz */
	unsigned int channels;		/* channels */
	snd_pcm_uframes_t period_size;	/* period size */
	unsigned int periods;		/* periods */
	snd_pcm_uframes_t buffer_size;	/* buffer size */
	unsigned int tick_time;		/* tick time */
	snd_pcm_uframes_t min_align;	/* Min alignment for the format */
	size_t byte_align;
	unsigned int frame_bits;
	unsigned int sample_bits;
	unsigned int info;
	unsigned int rate_num;
	unsigned int rate_den;

	/* -- SW params -- */
	snd_pcm_tstamp_t tstamp_mode;	/* mmap timestamp is updated */
  	unsigned int period_step;
	unsigned int sleep_min;		/* min ticks to sleep */
	snd_pcm_uframes_t xfer_align;	/* xfer size need to be a multiple */
	snd_pcm_uframes_t start_threshold;
	snd_pcm_uframes_t stop_threshold;
	snd_pcm_uframes_t silence_threshold; /* Silence filling happens when
						noise is nearest than this */
	snd_pcm_uframes_t silence_size;	/* Silence filling size */
	snd_pcm_uframes_t boundary;	/* pointers wrap point */

	snd_pcm_uframes_t silenced_start;
	snd_pcm_uframes_t silenced_size;

	snd_pcm_sync_id_t sync;		/* hardware synchronization ID */

	/* -- mmap -- */
	volatile snd_pcm_mmap_status_t *status;
	volatile snd_pcm_mmap_control_t *control;
	atomic_t mmap_count;

	/* -- locking / scheduling -- */
	spinlock_t lock;
	wait_queue_head_t sleep;
	struct timer_list tick_timer;
	struct fasync_struct *fasync;

	/* -- private section -- */
	void *private_data;
	void (*private_free)(snd_pcm_runtime_t *runtime);

	/* -- hardware description -- */
	snd_pcm_hardware_t hw;
	snd_pcm_hw_constraints_t hw_constraints;

	/* -- interrupt callbacks -- */
	void (*transfer_ack_begin)(snd_pcm_substream_t *substream);
	void (*transfer_ack_end)(snd_pcm_substream_t *substream);

	/* -- timer -- */
	unsigned int timer_resolution;	/* timer resolution */

	/* -- DMA -- */           
	unsigned char *dma_area;	/* DMA area */
	dma_addr_t dma_addr;		/* physical bus address (not accessible from main CPU) */
	unsigned long dma_bytes;	/* size of DMA area */

#ifdef CONFIG_SND_OSSEMUL
	/* -- OSS things -- */
	snd_pcm_oss_runtime_t oss;
#endif
};

struct _snd_pcm_substream {
	snd_pcm_t *pcm;
	snd_pcm_str_t *pstr;
	int number;
	char name[32];			/* substream name */
	int stream;			/* stream (direction) */
	size_t buffer_bytes_max;	/* limit ring buffer size */
	int dma_type;			
	void *dma_area;
	dma_addr_t dma_addr;
	size_t dma_bytes;
	size_t dma_max;
	void *dma_private;
	/* -- hardware operations -- */
	snd_pcm_ops_t *ops;
	/* -- runtime information -- */
	snd_pcm_runtime_t *runtime;
        /* -- timer section -- */
	snd_timer_t *timer;		/* timer */
	int timer_running;		/* time is running */
	spinlock_t timer_lock;
	/* -- next substream -- */
	snd_pcm_substream_t *next;
	/* -- linked substreams -- */
	snd_pcm_substream_t *link_next;
	snd_pcm_substream_t *link_prev;
	snd_pcm_file_t *file;
	struct file *ffile;
#ifdef CONFIG_SND_OSSEMUL
	/* -- OSS things -- */
	snd_pcm_oss_substream_t oss;
#endif
	snd_info_entry_t *proc_root;
	snd_info_entry_t *proc_info_entry;
	snd_info_entry_t *proc_hw_params_entry;
	snd_info_entry_t *proc_sw_params_entry;
	snd_info_entry_t *proc_status_entry;
	snd_info_entry_t *proc_prealloc_entry;
};

#ifdef CONFIG_SND_OSSEMUL
#define SUBSTREAM_BUSY(substream) ((substream)->file != NULL || ((substream)->oss.file != NULL))
#else
#define SUBSTREAM_BUSY(substream) ((substream)->file != NULL)
#endif


struct _snd_pcm_str {
	int stream;				/* stream (direction) */
	snd_pcm_t *pcm;
	/* -- substreams -- */
	unsigned int substream_count;
	unsigned int substream_opened;
	snd_pcm_substream_t *substream;
#ifdef CONFIG_SND_OSSEMUL
	/* -- OSS things -- */
	snd_pcm_oss_stream_t oss;
#endif
	snd_pcm_file_t *files;
	snd_minor_t *reg;
	snd_info_entry_t *proc_root;
	snd_info_entry_t *proc_info_entry;
};

struct _snd_pcm {
	snd_card_t *card;
	unsigned int device;	/* device number */
	unsigned int info_flags;
	unsigned short dev_class;
	unsigned short dev_subclass;
	char id[64];
	char name[80];
	snd_pcm_str_t streams[2];
	struct semaphore open_mutex;
	wait_queue_head_t open_wait;
	void *private_data;
	void (*private_free) (snd_pcm_t *pcm);
#ifdef CONFIG_SND_OSSEMUL
	snd_pcm_oss_t oss;
#endif
};

typedef struct _snd_pcm_notify {
	int (*n_register) (unsigned short minor, snd_pcm_t * pcm);
	int (*n_unregister) (unsigned short minor, snd_pcm_t * pcm);
	struct list_head list;
} snd_pcm_notify_t;

/*
 *  Registering
 */

extern snd_pcm_t *snd_pcm_devices[];
extern snd_minor_t snd_pcm_reg[2];

void snd_pcm_lock(int unlock);

int snd_pcm_new(snd_card_t * card, char *id, int device,
		int playback_count, int capture_count,
		snd_pcm_t **rpcm);

int snd_pcm_notify(snd_pcm_notify_t *notify, int nfree);

/*
 *  Native I/O
 */

int snd_pcm_info(snd_pcm_substream_t * substream, snd_pcm_info_t *info);
int snd_pcm_info_user(snd_pcm_substream_t * substream, snd_pcm_info_t *info);
int snd_pcm_status(snd_pcm_substream_t * substream, snd_pcm_status_t *status);
int snd_pcm_prepare(snd_pcm_substream_t *substream);
int snd_pcm_start(snd_pcm_substream_t *substream);
int snd_pcm_stop(snd_pcm_substream_t *substream, int status);
#ifdef CONFIG_PM
int snd_pcm_suspend(snd_pcm_substream_t *substream);
int snd_pcm_suspend_all(snd_pcm_t *pcm);
#endif
int snd_pcm_kernel_playback_ioctl(snd_pcm_substream_t *substream, unsigned int cmd, void *arg);
int snd_pcm_kernel_capture_ioctl(snd_pcm_substream_t *substream, unsigned int cmd, void *arg);
int snd_pcm_kernel_ioctl(snd_pcm_substream_t *substream, unsigned int cmd, void *arg);
int snd_pcm_open(struct inode *inode, struct file *file);
int snd_pcm_release(struct inode *inode, struct file *file);
unsigned int snd_pcm_playback_poll(struct file *file, poll_table * wait);
unsigned int snd_pcm_capture_poll(struct file *file, poll_table * wait);
int snd_pcm_open_substream(snd_pcm_t *pcm, int stream, snd_pcm_substream_t **rsubstream);
void snd_pcm_release_substream(snd_pcm_substream_t *substream);
void snd_pcm_vma_notify_data(void *client, void *data);
int snd_pcm_mmap_data(snd_pcm_substream_t *substream, struct file *file, struct vm_area_struct *area);

#if BITS_PER_LONG >= 64

static inline void div64_32(u_int64_t *n, u_int32_t div, u_int32_t *rem)
{
	*rem = *n % div;
	*n /= div;
}

#elif defined(i386)

static inline void div64_32(u_int64_t *n, u_int32_t div, u_int32_t *rem)
{
	u_int32_t low, high;
	low = *n & 0xffffffff;
	high = *n >> 32;
	if (high) {
		u_int32_t high1 = high % div;
		high /= div;
		asm("divl %2":"=a" (low), "=d" (*rem):"rm" (div), "a" (low), "d" (high1));
		*n = (u_int64_t)high << 32 | low;
	} else {
		*n = low / div;
		*rem = low % div;
	}
}
#else

static inline void divl(u_int32_t high, u_int32_t low,
			u_int32_t div,
			u_int32_t *q, u_int32_t *r)
{
	u_int64_t n = (u_int64_t)high << 32 | low;
	u_int64_t d = (u_int64_t)div << 31;
	u_int32_t q1 = 0;
	int c = 32;
	while (n > 0xffffffffU) {
		q1 <<= 1;
		if (n > d) {
			n -= d;
			q1 |= 1;
		}
		d >>= 1;
		c--;
	}
	q1 <<= c;
	if (n) {
		low = n;
		*q = q1 | (low / div);
		*r = low % div;
	} else {
		*r = 0;
		*q = q1;
	}
	return;
}

static inline void div64_32(u_int64_t *n, u_int32_t div, u_int32_t *rem)
{
	u_int32_t low, high;
	low = *n & 0xffffffff;
	high = *n >> 32;
	if (high) {
		u_int32_t high1 = high % div;
		u_int32_t low1 = low;
		high /= div;
		divl(high1, low1, div, &low, rem);
		*n = (u_int64_t)high << 32 | low;
	} else {
		*n = low / div;
		*rem = low % div;
	}
}
#endif

/*
 *  PCM library
 */

static inline int snd_pcm_running(snd_pcm_substream_t *substream)
{
	return (substream->runtime->status->state == SNDRV_PCM_STATE_RUNNING ||
		(substream->runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
		 substream->stream == SNDRV_PCM_STREAM_PLAYBACK));
}

static inline ssize_t bytes_to_samples(snd_pcm_runtime_t *runtime, ssize_t size)
{
	return size * 8 / runtime->sample_bits;
}

static inline snd_pcm_sframes_t bytes_to_frames(snd_pcm_runtime_t *runtime, ssize_t size)
{
	return size * 8 / runtime->frame_bits;
}

static inline ssize_t samples_to_bytes(snd_pcm_runtime_t *runtime, ssize_t size)
{
	return size * runtime->sample_bits / 8;
}

static inline ssize_t frames_to_bytes(snd_pcm_runtime_t *runtime, snd_pcm_sframes_t size)
{
	return size * runtime->frame_bits / 8;
}

static inline int frame_aligned(snd_pcm_runtime_t *runtime, ssize_t bytes)
{
	return bytes % runtime->byte_align == 0;
}

static inline size_t snd_pcm_lib_buffer_bytes(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return frames_to_bytes(runtime, runtime->buffer_size);
}

static inline size_t snd_pcm_lib_period_bytes(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return frames_to_bytes(runtime, runtime->period_size);
}

/*
 *  result is: 0 ... (boundary - 1)
 */
static inline snd_pcm_uframes_t snd_pcm_playback_avail(snd_pcm_runtime_t *runtime)
{
	snd_pcm_sframes_t avail = runtime->status->hw_ptr + runtime->buffer_size - runtime->control->appl_ptr;
	if (avail < 0)
		avail += runtime->boundary;
	else if ((snd_pcm_uframes_t) avail >= runtime->boundary)
		avail -= runtime->boundary;
	return avail;
}

/*
 *  result is: 0 ... (boundary - 1)
 */
static inline snd_pcm_uframes_t snd_pcm_capture_avail(snd_pcm_runtime_t *runtime)
{
	snd_pcm_sframes_t avail = runtime->status->hw_ptr - runtime->control->appl_ptr;
	if (avail < 0)
		avail += runtime->boundary;
	return avail;
}

static inline snd_pcm_sframes_t snd_pcm_playback_hw_avail(snd_pcm_runtime_t *runtime)
{
	return runtime->buffer_size - snd_pcm_playback_avail(runtime);
}

static inline snd_pcm_sframes_t snd_pcm_capture_hw_avail(snd_pcm_runtime_t *runtime)
{
	return runtime->buffer_size - snd_pcm_capture_avail(runtime);
}

static inline void snd_pcm_trigger_done(snd_pcm_substream_t *substream, 
					snd_pcm_substream_t *master)
{
	substream->runtime->trigger_master = master;
}

static inline int hw_is_mask(int var)
{
	return var >= SNDRV_PCM_HW_PARAM_FIRST_MASK &&
		var <= SNDRV_PCM_HW_PARAM_LAST_MASK;
}

static inline int hw_is_interval(int var)
{
	return var >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL &&
		var <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL;
}

typedef unsigned int snd_mask_t;
#define SND_MASK_MAX 32

static inline snd_mask_t *hw_param_mask(snd_pcm_hw_params_t *params,
				     snd_pcm_hw_param_t var)
{
	return (snd_mask_t*)&params->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static inline snd_interval_t *hw_param_interval(snd_pcm_hw_params_t *params,
					     snd_pcm_hw_param_t var)
{
	return &params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

static inline const snd_mask_t *hw_param_mask_c(const snd_pcm_hw_params_t *params,
					     snd_pcm_hw_param_t var)
{
	return (const snd_mask_t *)hw_param_mask((snd_pcm_hw_params_t*) params, var);
}

static inline const snd_interval_t *hw_param_interval_c(const snd_pcm_hw_params_t *params,
						     snd_pcm_hw_param_t var)
{
	return (const snd_interval_t *)hw_param_interval((snd_pcm_hw_params_t*) params, var);
}

#define params_access(p) (ffs(*hw_param_mask((p), SNDRV_PCM_HW_PARAM_ACCESS)) - 1)
#define params_format(p) (ffs(*hw_param_mask((p), SNDRV_PCM_HW_PARAM_FORMAT)) - 1)
#define params_subformat(p) (ffs(*hw_param_mask((p), SNDRV_PCM_HW_PARAM_SUBFORMAT)) - 1)
#define params_channels(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_CHANNELS)->min
#define params_rate(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_RATE)->min
#define params_period_size(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min
#define params_period_bytes(p) ((params_period_size(p)*snd_pcm_format_physical_width(params_format(p))*params_channels(p))/8)
#define params_periods(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_PERIODS)->min
#define params_buffer_size(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min
#define params_buffer_bytes(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min
#define params_tick_time(p) hw_param_interval((p), SNDRV_PCM_HW_PARAM_TICK_TIME)->min


int snd_interval_refine(snd_interval_t *i, const snd_interval_t *v);
void snd_interval_mul(const snd_interval_t *a, const snd_interval_t *b, snd_interval_t *c);
void snd_interval_div(const snd_interval_t *a, const snd_interval_t *b, snd_interval_t *c);
void snd_interval_muldivk(const snd_interval_t *a, const snd_interval_t *b, 
			  unsigned int k, snd_interval_t *c);
void snd_interval_mulkdiv(const snd_interval_t *a, unsigned int k,
			  const snd_interval_t *b, snd_interval_t *c);
int snd_interval_list(snd_interval_t *i, unsigned int count, unsigned int *list, unsigned int mask);
int snd_interval_step(snd_interval_t *i, unsigned int min, unsigned int step);
int snd_interval_ratnum(snd_interval_t *i,
			unsigned int rats_count, ratnum_t *rats,
			unsigned int *nump, unsigned int *denp);
int snd_interval_ratden(snd_interval_t *i,
			unsigned int rats_count, ratden_t *rats,
			unsigned int *nump, unsigned int *denp);

void _snd_pcm_hw_params_any(snd_pcm_hw_params_t *params);
void _snd_pcm_hw_param_setempty(snd_pcm_hw_params_t *params, snd_pcm_hw_param_t var);
int snd_pcm_hw_param_min(snd_pcm_substream_t *substream, 
			 snd_pcm_hw_params_t *params,
			 snd_pcm_hw_param_t var,
			 unsigned int val, int *dir);
int snd_pcm_hw_param_max(snd_pcm_substream_t *substream, 
			 snd_pcm_hw_params_t *params,
			 snd_pcm_hw_param_t var,
			 unsigned int val, int *dir);
int snd_pcm_hw_param_setinteger(snd_pcm_substream_t *substream, 
				snd_pcm_hw_params_t *params,
				snd_pcm_hw_param_t var);
int snd_pcm_hw_param_first(snd_pcm_substream_t *substream, 
			   snd_pcm_hw_params_t *params,
			   snd_pcm_hw_param_t var, int *dir);
int snd_pcm_hw_param_last(snd_pcm_substream_t *substream, 
			  snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, int *dir);
int snd_pcm_hw_param_near(snd_pcm_substream_t *substream, 
			  snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, 
			  unsigned int val, int *dir);
int snd_pcm_hw_params_choose(snd_pcm_substream_t *substream, snd_pcm_hw_params_t *params);

int snd_pcm_hw_refine(snd_pcm_substream_t *substream, snd_pcm_hw_params_t *params);

int snd_pcm_hw_constraints_init(snd_pcm_substream_t *substream);
int snd_pcm_hw_constraints_complete(snd_pcm_substream_t *substream);

int snd_pcm_hw_constraint_mask(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var,
			       unsigned int mask);
int snd_pcm_hw_constraint_minmax(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var,
				 unsigned int min, unsigned int max);
int snd_pcm_hw_constraint_integer(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var);
int snd_pcm_hw_constraint_list(snd_pcm_runtime_t *runtime, 
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       snd_pcm_hw_constraint_list_t *l);
int snd_pcm_hw_constraint_ratnums(snd_pcm_runtime_t *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  snd_pcm_hw_constraint_ratnums_t *r);
int snd_pcm_hw_constraint_ratdens(snd_pcm_runtime_t *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  snd_pcm_hw_constraint_ratdens_t *r);
int snd_pcm_hw_constraint_msbits(snd_pcm_runtime_t *runtime, 
				 unsigned int cond,
				 unsigned int width,
				 unsigned int msbits);
int snd_pcm_hw_constraint_step(snd_pcm_runtime_t *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       unsigned long step);
int snd_pcm_hw_rule_add(snd_pcm_runtime_t *runtime,
			unsigned int cond,
			int var,
			snd_pcm_hw_rule_func_t func, void *private,
			int dep, ...);

int snd_pcm_format_signed(snd_pcm_format_t format);
int snd_pcm_format_unsigned(snd_pcm_format_t format);
int snd_pcm_format_linear(snd_pcm_format_t format);
int snd_pcm_format_little_endian(snd_pcm_format_t format);
int snd_pcm_format_big_endian(snd_pcm_format_t format);
int snd_pcm_format_width(snd_pcm_format_t format);			/* in bits */
int snd_pcm_format_physical_width(snd_pcm_format_t format);		/* in bits */
u_int64_t snd_pcm_format_silence_64(snd_pcm_format_t format);
int snd_pcm_format_set_silence(snd_pcm_format_t format, void *buf, unsigned int frames);
snd_pcm_format_t snd_pcm_build_linear_format(int width, int unsignd, int big_endian);
ssize_t snd_pcm_format_size(snd_pcm_format_t format, size_t samples);
 
void snd_pcm_set_ops(snd_pcm_t * pcm, int direction, snd_pcm_ops_t *ops);
void snd_pcm_set_sync(snd_pcm_substream_t * substream);
int snd_pcm_lib_interleave_len(snd_pcm_substream_t *substream);
int snd_pcm_lib_ioctl(snd_pcm_substream_t *substream,
		      unsigned int cmd, void *arg);                      
int snd_pcm_update_hw_ptr(snd_pcm_substream_t *substream);
int snd_pcm_playback_xrun_check(snd_pcm_substream_t *substream);
int snd_pcm_capture_xrun_check(snd_pcm_substream_t *substream);
int snd_pcm_playback_xrun_asap(snd_pcm_substream_t *substream);
int snd_pcm_capture_xrun_asap(snd_pcm_substream_t *substream);
void snd_pcm_playback_silence(snd_pcm_substream_t *substream);
int snd_pcm_playback_ready(snd_pcm_substream_t *substream);
int snd_pcm_capture_ready(snd_pcm_substream_t *substream);
long snd_pcm_playback_ready_jiffies(snd_pcm_substream_t *substream);
long snd_pcm_capture_ready_jiffies(snd_pcm_substream_t *substream);
int snd_pcm_playback_data(snd_pcm_substream_t *substream);
int snd_pcm_playback_empty(snd_pcm_substream_t *substream);
int snd_pcm_capture_empty(snd_pcm_substream_t *substream);
void snd_pcm_tick_prepare(snd_pcm_substream_t *substream);
void snd_pcm_tick_set(snd_pcm_substream_t *substream, unsigned long ticks);
void snd_pcm_tick_elapsed(snd_pcm_substream_t *substream);
void snd_pcm_period_elapsed(snd_pcm_substream_t *substream);
snd_pcm_sframes_t snd_pcm_lib_write(snd_pcm_substream_t *substream,
				    const void *buf, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_read(snd_pcm_substream_t *substream,
				   void *buf, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_writev(snd_pcm_substream_t *substream,
				     void **bufs, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_readv(snd_pcm_substream_t *substream,
				    void **bufs, snd_pcm_uframes_t frames);

/*
 *  Timer interface
 */

void snd_pcm_timer_resolution_change(snd_pcm_substream_t *substream);
void snd_pcm_timer_init(snd_pcm_substream_t * substream);
void snd_pcm_timer_done(snd_pcm_substream_t * substream);

/*
 *  Memory
 */

int snd_pcm_lib_preallocate_free(snd_pcm_substream_t *substream);
int snd_pcm_lib_preallocate_free_for_all(snd_pcm_t *pcm);
int snd_pcm_lib_preallocate_pages(snd_pcm_substream_t *substream,
				  size_t size, size_t max,
				  unsigned int flags);
int snd_pcm_lib_preallocate_pages_for_all(snd_pcm_t *pcm,
					  size_t size, size_t max,
					  unsigned int flags);
int snd_pcm_lib_malloc_pages(snd_pcm_substream_t *substream, size_t size);
int snd_pcm_lib_free_pages(snd_pcm_substream_t *substream);

#ifdef CONFIG_ISA
int snd_pcm_lib_preallocate_isa_pages(snd_pcm_substream_t *substream,
				      size_t size, size_t max);
int snd_pcm_lib_preallocate_isa_pages_for_all(snd_pcm_t *pcm,
					      size_t size, size_t max);
#endif
#ifdef CONFIG_PCI
int snd_pcm_lib_preallocate_pci_pages(struct pci_dev *pci,
				      snd_pcm_substream_t *substream,
				      size_t size, size_t max);
int snd_pcm_lib_preallocate_pci_pages_for_all(struct pci_dev *pci,
					      snd_pcm_t *pcm,
					      size_t size,
					      size_t max);
#endif

static inline void snd_pcm_limit_isa_dma_size(int dma, size_t *max)
{
	*max = dma < 4 ? 64 * 1024 : 128 * 1024;
}

/*
 *  Misc
 */

#define SNDRV_PCM_DEFAULT_CON_SPDIF	(IEC958_AES0_CON_EMPHASIS_NONE|\
					 (IEC958_AES1_CON_ORIGINAL<<8)|\
					 (IEC958_AES1_CON_PCM_CODER<<8)|\
					 (IEC958_AES3_CON_FS_48000<<24))

#endif /* __SOUND_PCM_H */