/*
 * Zapata Telephony
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#ifndef _DIGITS_H
#define _DIGITS_H

#define DEFAULT_DTMF_LENGTH	100 * ZT_CHUNKSIZE
#define DEFAULT_MFR1_LENGTH	68 * ZT_CHUNKSIZE
#define DEFAULT_MFR2_LENGTH	100 * ZT_CHUNKSIZE
#define	PAUSE_LENGTH		500 * ZT_CHUNKSIZE

/* At the end of silence, the tone stops */
static struct zt_tone dtmf_silence = {
	.tonesamples = DEFAULT_DTMF_LENGTH,
};

/* At the end of silence, the tone stops */
static struct zt_tone mfr1_silence = {
	.tonesamples = DEFAULT_MFR1_LENGTH,
};

/* At the end of silence, the tone stops */
static struct zt_tone mfr2_silence = {
	.tonesamples = DEFAULT_MFR2_LENGTH,
};

/* A pause in the dialing */
static struct zt_tone tone_pause = {
	.tonesamples = PAUSE_LENGTH,
};

#endif 
