/*****************************************************************************/

/*
 *      gentbl.c  --  Linux soundcard HF FSK driver,
 *                    Table generator.
 *
 *      Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *        Swiss Federal Institute of Technology (ETH), Electronics Lab
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*****************************************************************************/
      
#include <linux/hfmodem.h>
#include <math.h>
#include <stdio.h>

/* --------------------------------------------------------------------- */

#define SINTABBITS  9
#define SINTABSIZE  (1<<SINTABBITS)

/* --------------------------------------------------------------------- */

static void gensintbl(void)
{
	int i;

	printf("#define SINTABBITS %d\n#define SINTABSIZE  (1<<SINTABBITS)\n"
	       "\nstatic short isintab[SINTABSIZE+SINTABSIZE/4] = {\n\t", SINTABBITS);
	for (i = 0; i < (SINTABSIZE+SINTABSIZE/4); i++) {
		printf("%6d", (int)(32767.0 * sin(2.0 * M_PI / SINTABSIZE * i)));
		if (i < (SINTABSIZE+SINTABSIZE/4)-1) {
			if ((i & 7) == 7)
				printf(",\n\t");
			else
				printf(",");
		}
	}
	printf("\n};\n\n");
}

/* --------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	printf("/*\n * This file is automatically generated by %s, DO NOT EDIT!\n*/\n\n",
	       argv[0]);
	gensintbl();
	exit(0);
}

/* --------------------------------------------------------------------- */

