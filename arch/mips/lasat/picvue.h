/* 
 * Picvue PVC160206 display driver
 *
 * Brian Murphy <brian.murphy@eicon.com> 
 *
 */
#include <asm/semaphore.h>

#define PVC_NLINES		2
#define PVC_DISPMEM		80
#define PVC_LINELEN		PVC_DISPMEM / PVC_NLINES
#define PVC_VISIBLE_CHARS	16

void pvc_write_string(const unsigned char *str, u8 addr, int line);

#define BM_SIZE			8
#define MAX_PROGRAMMABLE_CHARS	8
int pvc_program_cg(int charnum, u8 bitmap[BM_SIZE]);

void pvc_dispcnt(u8 cmd);
#define  DISP_OFF	0
#define  DISP_ON	(1 << 2)
#define  CUR_ON		(1 << 1)
#define  CUR_BLINK	(1 << 0)

void pvc_move(u8 cmd);
#define  DISPLAY	(1 << 3)
#define  CURSOR		0
#define  RIGHT		(1 << 2)
#define  LEFT		0

void pvc_clear(void);
void pvc_home(void);

extern struct semaphore pvc_sem;