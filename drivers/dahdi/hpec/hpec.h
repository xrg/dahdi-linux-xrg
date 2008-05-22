/*
 * DAHDI Telephony Interface to Digium High-Performance Echo Canceller
 *
 * Copyright (C) 2006 Digium, Inc.
 *
 * All rights reserved.
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
 */

#if !defined(_HPEC_H)
#define _HPEC_H

struct echo_can_state;

void __attribute__((regparm(0))) hpec_init(int __attribute__((regparm(0))) __attribute__((format (printf, 1, 2))) (*logger)(const char *format, ...),
					   unsigned int debug,
					   unsigned int chunk_size,
					   void * (*memalloc)(size_t len),
					   void (*memfree)(void *ptr));

void __attribute__((regparm(0))) hpec_shutdown(void);

int __attribute__((regparm(0))) hpec_license_challenge(struct hpec_challenge *challenge);

int __attribute__((regparm(0))) hpec_license_check(struct hpec_license *license);

struct echo_can_state __attribute__((regparm(0))) *hpec_channel_alloc(unsigned int len);

void __attribute__((regparm(0))) hpec_channel_free(struct echo_can_state *channel);

void __attribute__((regparm(0))) hpec_channel_update(struct echo_can_state *channel, short *iref, short *isig);

#endif /* !defined(_HPEC_H) */

