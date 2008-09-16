#ifndef	XPD_H
#define	XPD_H

/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2006, Xorcom
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

#include "xdefs.h"
#include "xproto.h"

#ifdef	__KERNEL__
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/version.h>
#include <asm/atomic.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#include <linux/moduleparam.h>
#ifdef	XPP_DEBUGFS
#ifndef	CONFIG_DEBUG_FS
#warning kernel does not include CONFIG_DEBUG_FS, canceling XPP_DEBUGFS support
#undef	XPP_DEBUGFS
#else
#include <linux/debugfs.h>
#endif
#endif
#endif	/* __KERNEL__ */

#include <dahdi/kernel.h>

#ifdef __KERNEL__
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
/* also added in RHEL kernels with the OpenInfiniband backport: */
#if LINUX_VERSION_CODE != KERNEL_VERSION(2,6,9) || !defined(DEFINE_SPINLOCK)
typedef	unsigned gfp_t;		/* Added in 2.6.14 */
#endif
#endif

/*
 * FIXME: Kludge for 2.6.19
 * bool is now defined as a proper boolean type (gcc _Bool)
 * but the command line parsing framework handles it as int.
 */
#define	DEF_PARM_BOOL(name,init,perm,desc)	\
	int name = init;	\
	module_param(name, bool, perm);		\
	MODULE_PARM_DESC(name, desc " [default " #init "]")

#define	DEF_PARM(type,name,init,perm,desc)	\
	type name = init;	\
	module_param(name, type, perm);		\
	MODULE_PARM_DESC(name, desc " [default " #init "]")

#if	LINUX_VERSION_CODE	< KERNEL_VERSION(2,6,10)
/*
 * Old 2.6 kernels had module_param_array() macro that receive the counter
 * by value.
 */
#define	DEF_ARRAY(type,name,count,init,desc)	\
	unsigned int name ## _num_values;	\
	type name[count] = { [0 ... ((count)-1)] = (init) };			\
	module_param_array(name, type, name ## _num_values, 0644);	\
	MODULE_PARM_DESC(name, desc " ( 1-" __MODULE_STRING(count) ")")
#else
#define	DEF_ARRAY(type,name,count,init,desc)	\
	unsigned int name ## _num_values;	\
	type name[count] = {[0 ... ((count)-1)] = init};			\
	module_param_array(name, type, &name ## _num_values, 0644);	\
	MODULE_PARM_DESC(name, desc " ( 1-" __MODULE_STRING(count) ")")
#endif
#endif	// __KERNEL__

#define	CARD_DESC_MAGIC	0xca9dde5c

struct	card_desc_struct {
	struct list_head	card_list;
	u32			magic;
	byte			type;		/* LSB: 1 - to_phone, 0 - to_line */
	byte			subtype;
	struct xpd_addr		xpd_addr;
	byte			numchips;
	byte			ports_per_chip;
	byte			ports;
	byte			port_dir;
	struct xpd_addr		ec_addr;	/* echo canceler address */
};

typedef enum xpd_direction {
	TO_PSTN = 0,
	TO_PHONE = 1,
} xpd_direction_t;

#ifdef	__KERNEL__

/*
 * XPD statistics counters
 */
enum {
	XPD_N_PCM_READ,
	XPD_N_PCM_WRITE,
	XPD_N_RECV_ERRORS,
};

#define	XPD_COUNTER(xpd, counter)	((xpd)->counters[XPD_N_ ## counter])

#define	C_(x)	[ XPD_N_ ## x ] = { #x }

/* yucky, make an instance so we can size it... */
static struct xpd_counters {
	char	*name;
} xpd_counters[] = {
	C_(PCM_READ),
	C_(PCM_WRITE),
	C_(RECV_ERRORS),
};

#undef C_

#define	XPD_COUNTER_MAX	(sizeof(xpd_counters)/sizeof(xpd_counters[0]))

/*
 * An XPD is a single Xorcom Protocol Device
 */
struct xpd {
	char xpdname[XPD_NAMELEN];
	struct dahdi_span	span;
	struct dahdi_chan	*chans[32];
	int		channels;
	xpd_type_t	type;
	const char	*type_name;
	byte		subtype;
	xpd_direction_t	direction;		/* TO_PHONE, TO_PSTN */
	int		subunits;		/* all siblings */
	xpp_line_t	no_pcm;			/* Temporary: disable PCM (for USB-1) */
	xpp_line_t	offhook;		/* Actual chip state: 0 - ONHOOK, 1 - OFHOOK */
	xpp_line_t	cid_on;
	xpp_line_t	msg_waiting;		/* Voice Mail Waiting Indication */
	xpp_line_t	digital_outputs;	/* 0 - no, 1 - yes */
	xpp_line_t	digital_inputs;		/* 0 - no, 1 - yes */
	xpp_line_t	digital_signalling;	/* BRI signalling channels */
	uint		timing_priority;	/* from 'span' directives in chan_dahdi.conf */

	/* maintained by card drivers */
	uint		pcm_len;		/* allocation length of PCM packet (dynamic) */
	xpp_line_t	wanted_pcm_mask;
	xpp_line_t	silence_pcm;		/* inject silence during next tick */
	xpp_line_t	mute_dtmf;

	bool		ringing[CHANNELS_PERXPD];

	xbus_t *xbus;			/* The XBUS we are connected to */

	spinlock_t	lock;
	atomic_t	dahdi_registered;	/* Am I fully registered with dahdi */
	atomic_t	open_counter;	/* Number of open channels */

	int		flags;
	unsigned long	blink_mode;	/* bitmask of blinking ports */
#define	DEFAULT_LED_PERIOD	(1000/8)	/* in tick */

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*proc_xpd_dir;
	struct proc_dir_entry	*proc_xpd_summary;
	struct proc_dir_entry	*proc_xpd_ztregister;
	struct proc_dir_entry	*proc_xpd_blink;
	struct proc_dir_entry	*proc_xpd_chipregs;
#endif

	int		counters[XPD_COUNTER_MAX];

	const xproto_table_t	*xproto;	/* Card level protocol table */
	const xops_t	*xops;			/* Card level operations */
	void		*priv;			/* Card level private data */
	bool		card_present;
	reg_cmd_t	requested_reply;
	reg_cmd_t	last_reply;

	unsigned long	last_response;	/* in jiffies */
	unsigned	xbus_idx;	/* index in xbus->xpds[] */
	struct xpd_addr	addr;
	struct list_head xpd_list;
	unsigned int	timer_count;
	/* Echo cancelation */
	u_char ec_chunk1[CHANNELS_PERXPD][DAHDI_CHUNKSIZE];
	u_char ec_chunk2[CHANNELS_PERXPD][DAHDI_CHUNKSIZE];
};

#define	for_each_line(xpd,i)	for((i) = 0; (i) < (xpd)->channels; (i)++)
#define	IS_BRI(xpd)		((xpd)->type == XPD_TYPE_BRI)
#define	TICK_TOLERANCE		500 /* usec */

#ifdef	DEBUG_SYNC_PARPORT
void xbus_flip_bit(xbus_t *xbus, unsigned int bitnum0, unsigned int bitnum1);
#else
#define	xbus_flip_bit(xbus, bitnum0, bitnum1)
#endif

static inline void *my_kzalloc(size_t size, gfp_t flags)
{
	void	*p;

	p = kmalloc(size, flags);
	if(p)
		memset(p, 0, size);
	return p;
}

#endif

#endif	/* XPD_H */
