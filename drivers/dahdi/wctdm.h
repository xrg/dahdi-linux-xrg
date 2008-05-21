/*
 * Wilcard S100P FXS Interface Driver for Zapata Telephony interface
 *
 * Written by Mark Spencer <markster@linux-support.net>
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
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

#ifndef _WCTDM_H
#define _WCTDM_H

#include <linux/ioctl.h>

#define NUM_REGS	  109
#define NUM_INDIRECT_REGS 105

struct wctdm_stats {
	int tipvolt;	/* TIP voltage (mV) */
	int ringvolt;	/* RING voltage (mV) */
	int batvolt;	/* VBAT voltage (mV) */
};

struct wctdm_regs {
	unsigned char direct[NUM_REGS];
	unsigned short indirect[NUM_INDIRECT_REGS];
};

struct wctdm_regop {
	int indirect;
	unsigned char reg;
	unsigned short val;
};

struct wctdm_echo_coefs {
	unsigned char acim;
	unsigned char coef1;
	unsigned char coef2;
	unsigned char coef3;
	unsigned char coef4;
	unsigned char coef5;
	unsigned char coef6;
	unsigned char coef7;
	unsigned char coef8;
};

#define WCTDM_GET_STATS	_IOR (ZT_CODE, 60, struct wctdm_stats)
#define WCTDM_GET_REGS	_IOR (ZT_CODE, 61, struct wctdm_regs)
#define WCTDM_SET_REG	_IOW (ZT_CODE, 62, struct wctdm_regop)
#define WCTDM_SET_ECHOTUNE _IOW (ZT_CODE, 63, struct wctdm_echo_coefs)


#endif /* _WCTDM_H */
