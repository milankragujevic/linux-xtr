#ifndef _LINUX_MISCDEVICE_H
#define _LINUX_MISCDEVICE_H

#define BUSMOUSE_MINOR 0
#define PSMOUSE_MINOR  1
#define MS_BUSMOUSE_MINOR 2
#define ATIXL_BUSMOUSE_MINOR 3
#define AMIGAMOUSE_MINOR 4
#define ATARIMOUSE_MINOR 5
#define SUN_MOUSE_MINOR 6
#define PC110PAD_MINOR 9
#define WATCHDOG_MINOR		130	/* Watchdog timer     */
#define TEMP_MINOR		131	/* Temperature Sensor */
#define RTC_MINOR 135
#define SUN_OPENPROM_MINOR 139
#define NVRAM_MINOR 144
#define RADIO_MINOR 152
#define MISC_DYNAMIC_MINOR 255

#define SGI_GRAPHICS_MINOR   146
#define SGI_OPENGL_MINOR     147
#define SGI_GFX_MINOR        148
#define SGI_STREAMS_MOUSE    149
#define SGI_STREAMS_KEYBOARD 150
/* drivers/sgi/char/usema.c */
#define SGI_USEMACLONE	     151

extern int misc_init(void);

struct miscdevice 
{
	int minor;
	const char *name;
	struct file_operations *fops;
	struct miscdevice * next, * prev;
};

extern int misc_register(struct miscdevice * misc);
extern int misc_deregister(struct miscdevice * misc);

#endif
