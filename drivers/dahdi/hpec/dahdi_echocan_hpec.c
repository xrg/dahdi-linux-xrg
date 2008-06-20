/*
 * DAHDI Telephony Interface to Digium High-Performance Echo Canceller
 *
 * Copyright (C) 2006-2008 Digium, Inc.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>

#include <dahdi/kernel.h>

static int debug;

#define module_printk(level, fmt, args...) printk(level "%s: " fmt, THIS_MODULE->name, ## args)
#define debug_printk(level, fmt, args...) if (debug >= level) printk("%s (%s): " fmt, THIS_MODULE->name, __FUNCTION__, ## args)

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

static void echo_can_free(struct echo_can_state *ec)
{
	hpec_channel_free(ec);
}

static void echo_can_array_update(struct echo_can_state *ec, short *iref, short *isig)
{
	hpec_channel_update(ec, iref, isig);
}

DECLARE_MUTEX(alloc_lock);

static int echo_can_create(struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p,
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
	case DAHDI_EC_LICENSE_CHALLENGE:
		if (down_interruptible(&license_lock))
			return -EINTR;
		memset(&challenge, 0, sizeof(challenge));
		if (hpec_license_challenge(&challenge))
			result = -ENODEV;
		if (!result && copy_to_user((unsigned char *) data, &challenge, sizeof(challenge)))
			result = -EFAULT;
		up(&license_lock);
		return result;
	case DAHDI_EC_LICENSE_RESPONSE:
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

static const struct dahdi_echocan me = {
	.name = "HPEC",
	.owner = THIS_MODULE,
	.echo_can_create = echo_can_create,
	.echo_can_free = echo_can_free,
	.echo_can_array_update = echo_can_array_update,
	.echo_can_traintap = echo_can_traintap,
};

static int __init mod_init(void)
{
	if (dahdi_register_echocan(&me)) {
		module_printk(KERN_ERR, "could not register with DAHDI core\n");

		return -EPERM;
	}

	module_printk(KERN_NOTICE, "Registered echo canceler '%s'\n", me.name);

	hpec_init(logger, debug, DAHDI_CHUNKSIZE, memalloc, memfree);

	dahdi_set_hpec_ioctl(hpec_license_ioctl);

	return 0;
}

static void __exit mod_exit(void)
{
	dahdi_unregister_echocan(&me);

	dahdi_set_hpec_ioctl(NULL);

	hpec_shutdown();
}

module_param(debug, int, S_IRUGO | S_IWUSR);

MODULE_DESCRIPTION("DAHDI High Performance Echo Canceller");
MODULE_AUTHOR("Kevin P. Fleming <kpfleming@digium.com>");
MODULE_LICENSE("Digium Commercial");

module_init(mod_init);
module_exit(mod_exit);
