/*
 * VoiceBus(tm) Interface Library.
 *
 * Written by Shaun Ruffell <sruffell@digium.com>
 * and based on previous work by Mark Spencer <markster@digium.com>, 
 * Matthew Fredrickson <creslin@digium.com>, and
 * Michael Spiceland <mspiceland@digium.com>
 * 
 * Copyright (C) 2007-2008 Digium, Inc.
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
 *
 */
#ifndef __VOICEBUS_H__
#define __VOICEBUS_H__

struct voicebus;

#define VOICEBUS_DEFAULT_LATENCY 3

void voicebus_setdebuglevel(struct voicebus *vb, u32 level);
int voicebus_getdebuglevel(struct voicebus *vb);
struct pci_dev * voicebus_get_pci_dev(struct voicebus *vb);
int voicebus_init(struct pci_dev* pdev, u32 framesize, 
                  const char *board_name,
		  void (*handle_receive)(void *buffer, void *context),
		  void (*handle_transmit)(void *buffer, void *context),
		  void *context, 
		  struct voicebus **vb_p);
void voicebus_release(struct voicebus *vb);
int voicebus_start(struct voicebus *vb);
int voicebus_stop(struct voicebus *vb);
void * voicebus_alloc(struct voicebus* vb);
void voicebus_free(struct voicebus *vb, void *vbb);
int voicebus_transmit(struct voicebus *vb, void *vbb);
int voicebus_set_minlatency(struct voicebus *vb, unsigned int milliseconds);
int voicebus_current_latency(struct voicebus *vb) ;
 
#endif /* __VOICEBUS_H__ */
