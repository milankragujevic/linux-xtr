/*
 * device driver for philips saa7134 based TV cards
 * card-specific stuff.
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>

#include "saa7134-reg.h"
#include "saa7134.h"
#include "tuner.h"

/* commly used strings */
static char name_mute[]    = "mute";
static char name_radio[]   = "Radio";
static char name_tv[]      = "Television";
static char name_comp1[]   = "Composite1";
static char name_comp2[]   = "Composite2";
static char name_svideo[]  = "S-Video";

/* ------------------------------------------------------------------ */
/* board config info                                                  */

struct saa7134_board saa7134_boards[] = {
	[SAA7134_BOARD_UNKNOWN] = {
		name:		"UNKNOWN/GENERIC",
		audio_clock:	0x00187de7,
		tuner_type:	TUNER_ABSENT,
		inputs: {{
			name: "default",
			vmux: 0,
			amux: LINE1,
		}},
	},
	[SAA7134_BOARD_PROTEUS_PRO] = {
		/* /me */
		name:		"Proteus Pro [philips reference design]",
		audio_clock:	0x00187de7,
		tuner_type:	TUNER_PHILIPS_PAL,
		inputs: {{
			name: name_comp1,
			vmux: 0,
			amux: LINE1,
		},{
			name: name_tv,
			vmux: 1,
			amux: TV,
			tv:   1,
		}},
	},
	[SAA7134_BOARD_FLYVIDEO3000] = {
		/* "Marco d'Itri" <md@Linux.IT> */
		name:		"LifeView FlyVIDEO3000",
		audio_clock:	0x00200000,
		tuner_type:	TUNER_PHILIPS_PAL,
		inputs: {{
			name: name_tv,
			vmux: 1,
			amux: TV,
			tv:   1,
		},{
			name: name_comp1,
			vmux: 0,
			amux: LINE1,
		},{
			name: name_comp2,
			vmux: 3,
			amux: LINE1,
		},{
			name: name_svideo,
			vmux: 8,
			amux: LINE1,
		}},
		radio: {
			name: name_radio,
			amux: LINE2,
		},
	},
	[SAA7134_BOARD_FLYVIDEO2000] = {
		/* "TC Wan" <tcwan@cs.usm.my> */
		name:           "LifeView FlyVIDEO2000",
		audio_clock:    0x00200000,
		tuner_type:     TUNER_LG_PAL_NEW_TAPC,
		gpiomask:       0x6000,
		inputs: {{
			name: name_tv,
			vmux: 1,
			amux: LINE2,
			gpio: 0x0000,
			tv:   1,
		},{
			name: name_comp1,
			vmux: 0,
			amux: LINE2,
			gpio: 0x4000,
		},{
			name: name_comp2,
			vmux: 3,
			amux: LINE2,
			gpio: 0x4000,
		},{
			name: name_svideo,
			vmux: 8,
			amux: LINE2,
			gpio: 0x4000,
		}},
                radio: {
                        name: name_radio,
                        amux: LINE2,
                },
		mute: {
			name: name_mute,
			amux: LINE1,
		},
	},
	[SAA7134_BOARD_EMPRESS] = {
		/* "Gert Vervoort" <gert.vervoort@philips.com> */
		name:		"EMPRESS",
		audio_clock:	0x00187de7,
		tuner_type:	TUNER_PHILIPS_PAL,
		inputs: {{
			name: name_comp1,
			vmux: 0,
			amux: LINE1,
		},{
			name: name_svideo,
			vmux: 8,
			amux: LINE1,
		},{
			name: name_tv,
			vmux: 1,
			amux: LINE2,
			tv:   1,
		}},
		radio: {
			name: name_radio,
			amux: LINE2,
		},
		i2s_rate:  48000,
		has_ts:    1,
		video_out: CCIR656,
	},
	[SAA7134_BOARD_MONSTERTV] = {
               /* "K.Ohta" <alpha292@bremen.or.jp> */
               name:           "SKNet Monster TV",
               audio_clock:    0x00187de7,
               tuner_type:     TUNER_PHILIPS_NTSC_M,
               inputs: {{
                       name: name_tv,
                       vmux: 1,
                       amux: TV,
                       tv:   1,
               },{
                       name: name_comp1,
                       vmux: 0,
                       amux: LINE1,
               },{
                       name: name_svideo,
                       vmux: 8,
                       amux: LINE1,
               }},
               radio: {
                       name: name_radio,
                       amux: LINE2,
               },
	},
	[SAA7134_BOARD_MD9717] = {
		name:		"Tevion MD 9717",
		audio_clock:	0x00200000,
		tuner_type:	TUNER_PHILIPS_PAL,
		inputs: {{
			name: name_tv,
			vmux: 1,
			amux: TV,
			tv:   1,
		},{
			/* workaround for problems with normal TV sound */
			name: "TV (mono only)",
			vmux: 1,
			amux: LINE2,
			tv:   1,
		},{
			name: name_comp1,
			vmux: 2,
			amux: LINE1,
		},{
			name: name_comp2,
			vmux: 3,
			amux: LINE1,
		},{
			name: name_svideo,
			vmux: 8,
			amux: LINE1,
		}},
		radio: {
			name: name_radio,
			amux: LINE2,
		},
	},
	[SAA7134_BOARD_TVSTATION_RDS] = {
		name:		"KNC One TV-Station RDS",
		audio_clock:	0x00200000,
		tuner_type:	TUNER_PHILIPS_FM1216ME_MK3,
		need_tda9887:   1,
		inputs: {{
			name: name_tv,
			vmux: 1,
			amux: TV,
			tv:   1,
		},{
			name: name_comp1,
			vmux: 2,
			amux: LINE1,
		},{
			name: name_comp2,
			vmux: 3,
			amux: LINE1,
		}},
		radio: {
			name: name_radio,
			amux: LINE2,
		},
	},
	[SAA7134_BOARD_CINERGY400] = {
                name:           "Terratec Cinergy 400 TV",
                audio_clock:    0x00200000,
                tuner_type:     TUNER_PHILIPS_PAL,
                inputs: {{
                        name: name_tv,
                        vmux: 1,
                        amux: TV,
                        tv:   1,
                },{
                        name: name_comp1,
                        vmux: 4,
                        amux: LINE1,
                },{
                        name: name_svideo,
                        vmux: 8,
                        amux: LINE1,
                },{
                        name: name_comp2, // CVideo over SVideo Connector
                        vmux: 0,
                        amux: LINE1,
                }}
        },
	[SAA7134_BOARD_MD5044] = {
		name:           "Medion 5044",
		audio_clock:    0x00200000,
		tuner_type:     TUNER_PHILIPS_FM1216ME_MK3,
		need_tda9887:   1,
		inputs: {{
			name: name_tv,
			vmux: 1,
			amux: TV,
			tv:   1,
		},{
			name: name_comp1,
			vmux: 0,
			amux: LINE2,
		},{
			name: name_comp2,
			vmux: 3,
			amux: LINE2,
		},{
			name: name_svideo,
			vmux: 8,
			amux: LINE2,
		}},
		radio: {
			name: name_radio,
			amux: LINE2,
		},
	},
};
const int saa7134_bcount = (sizeof(saa7134_boards)/sizeof(struct saa7134_board));

/* ------------------------------------------------------------------ */
/* PCI ids + subsystem IDs                                            */

struct pci_device_id __devinitdata saa7134_pci_tbl[] = {
	{
		vendor:       PCI_VENDOR_ID_PHILIPS,
		device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
		subvendor:    PCI_VENDOR_ID_PHILIPS,
		subdevice:    0x2001,
		driver_data:  SAA7134_BOARD_PROTEUS_PRO,
        },{
		vendor:       PCI_VENDOR_ID_PHILIPS,
		device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
		subvendor:    PCI_VENDOR_ID_PHILIPS,
		subdevice:    0x6752,
		driver_data:  SAA7134_BOARD_EMPRESS,
	},{
		vendor:       PCI_VENDOR_ID_PHILIPS,
		device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
                subvendor:    0x1131,
                subdevice:    0x4e85,
		driver_data:  SAA7134_BOARD_MONSTERTV,
        },{
                vendor:       PCI_VENDOR_ID_PHILIPS,
                device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
                subvendor:    0x153B,
                subdevice:    0x1142,
                driver_data:  SAA7134_BOARD_CINERGY400,
        },{

		/* --- boards without eeprom + subsystem ID --- */
                vendor:       PCI_VENDOR_ID_PHILIPS,
                device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
                subvendor:    PCI_VENDOR_ID_PHILIPS,
		subdevice:    0,
		driver_data:  SAA7134_BOARD_NOAUTO,
        },{
                vendor:       PCI_VENDOR_ID_PHILIPS,
                device:       PCI_DEVICE_ID_PHILIPS_SAA7130,
                subvendor:    PCI_VENDOR_ID_PHILIPS,
		subdevice:    0,
		driver_data:  SAA7134_BOARD_NOAUTO,
	},{
		
		/* --- default catch --- */
		vendor:       PCI_VENDOR_ID_PHILIPS,
		device:       PCI_DEVICE_ID_PHILIPS_SAA7130,
                subvendor:    PCI_ANY_ID,
                subdevice:    PCI_ANY_ID,
		driver_data:  SAA7134_BOARD_UNKNOWN,
        },{
		vendor:       PCI_VENDOR_ID_PHILIPS,
		device:       PCI_DEVICE_ID_PHILIPS_SAA7134,
                subvendor:    PCI_ANY_ID,
                subdevice:    PCI_ANY_ID,
		driver_data:  SAA7134_BOARD_UNKNOWN,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, saa7134_pci_tbl);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */