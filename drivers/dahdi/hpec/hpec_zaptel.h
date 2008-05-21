/*
 * Zapata Telephony Interface to Digium High-Performance Echo Canceller
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

#if !defined(_HPEC_ZAPTEL_H)
#define _HPEC_ZAPTEL_H

#define ZT_EC_ARRAY_UPDATE

#include "hpec_user.h"
#include "hpec.h"

static int __attribute__((regparm(0))) __attribute__((format (printf, 1, 2))) logger(const char *format, ...)
{
	int res;
	va_list args;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	va_start(args, format);
	res = vprintk(format, args);
	va_end(args);
#else
	char buf[256];

	va_start(args, format);
	res = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	printk(buf);
#endif

	return res;
}

static void *memalloc(size_t len)
{
	return kmalloc(len, GFP_KERNEL);
}

static void memfree(void *ptr)
{
	kfree(ptr);
}

static void echo_can_init(void)
{
	printk("Zaptel Echo Canceller: Digium High-Performance Echo Canceller\n");
	hpec_init(logger, debug, ZT_CHUNKSIZE, memalloc, memfree);
}

static void echo_can_identify(char *buf, size_t len)
{
	zap_copy_string(buf, "HPEC", len);
}

static void echo_can_shutdown(void)
{
	hpec_shutdown();
}

static inline void echo_can_free(struct echo_can_state *ec)
{
	hpec_channel_free(ec);
}

static inline void echo_can_array_update(struct echo_can_state *ec, short *iref, short *isig)
{
	hpec_channel_update(ec, iref, isig);
}

DECLARE_MUTEX(alloc_lock);

static int echo_can_create(struct zt_echocanparams *ecp, struct zt_echocanparam *p,
			   struct echo_can_state **ec)
{
	if (ecp->param_count > 0) {
		printk(KERN_WARNING "HPEC does not support parameters; failing request\n");
		return -EINVAL;
	}

	if (down_interruptible(&alloc_lock))
		return -ENOTTY;

	*ec = hpec_channel_alloc(ecp->tap_length);

	up(&alloc_lock);

	return *ec ? 0 : -ENOTTY;
}

static inline int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	return 1;
}

DECLARE_MUTEX(license_lock);

static int hpec_license_ioctl(unsigned int cmd, unsigned long data)
{
	struct hpec_challenge challenge;
	struct hpec_license license;
	int result = 0;

	switch (cmd) {
	case ZT_EC_LICENSE_CHALLENGE:
		if (down_interruptible(&license_lock))
			return -EINTR;
		memset(&challenge, 0, sizeof(challenge));
		if (hpec_license_challenge(&challenge))
			result = -ENODEV;
		if (!result && copy_to_user((unsigned char *) data, &challenge, sizeof(challenge)))
			result = -EFAULT;
		up(&license_lock);
		return result;
	case ZT_EC_LICENSE_RESPONSE:
		if (down_interruptible(&license_lock))
			return -EINTR;
		if (copy_from_user(&license, (unsigned char *) data, sizeof(license)))
			result = -EFAULT;
		if (!result && hpec_license_check(&license))
			result = -EACCES;
		up(&license_lock);
		return result;
	default:
		return -ENOSYS;
	}
}

#endif /* !defined(_HPEC_ZAPTEL_H) */
