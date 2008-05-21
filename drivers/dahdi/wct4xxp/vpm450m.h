/*
 * Copyright (C) 2005-2006 Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * All Rights Reserved
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

#ifndef _VPM450M_H
#define _VPM450M_H

#include <linux/firmware.h>

struct vpm450m;

/* From driver */
unsigned int oct_get_reg(void *data, unsigned int reg);
void oct_set_reg(void *data, unsigned int reg, unsigned int val);

/* From vpm450m */
struct vpm450m *init_vpm450m(void *wc, int *isalaw, int numspans, const struct firmware *firmware);
unsigned int get_vpm450m_capacity(void *wc);
void vpm450m_setec(struct vpm450m *instance, int channel, int eclen);
void vpm450m_setdtmf(struct vpm450m *instance, int channel, int dtmfdetect, int dtmfmute);
int vpm450m_checkirq(struct vpm450m *vpm450m);
int vpm450m_getdtmf(struct vpm450m *vpm450m, int *channel, int *tone, int *start);
void release_vpm450m(struct vpm450m *instance);

#endif
