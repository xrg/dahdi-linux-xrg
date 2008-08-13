/*
 * ECHO_CAN_JPAH
 *
 * by Jason Parker
 *
 * Based upon mg2ec.h - sort of.
 * This "echo can" will completely hose your audio.
 * Don't use it unless you're absolutely sure you know what you're doing.
 *
 * Copyright (C) 2007-2008, Digium, Inc.
 *
 * All rights reserved.
 *
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
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

struct echo_can_state {
	int blah;
};

static int echo_can_create(struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p,
			   struct echo_can_state **ec)
{
	unsigned int x;
	char *c;

	if ((*ec = kmalloc(sizeof(**ec), GFP_KERNEL))) {
		memset(ec, 0, sizeof(**ec));
	}

	for (x = 0; x < ecp->param_count; x++) {
		for (c = p[x].name; *c; c++)
			*c = tolower(*c);
		printk(KERN_WARNING "Unknown parameter supplied to JPAH echo canceler: '%s'\n", p[x].name);
		kfree(*ec);

		return -EINVAL;
	}

	return 0;
}

static void echo_can_free(struct echo_can_state *ec)
{
	kfree(ec);
}

static void echo_can_update(struct echo_can_state *ec, short *iref, short *isig)
{
	unsigned int x;

	for (x = 0; x < DAHDI_CHUNKSIZE; x++) {
		if (ec->blah < 2) {
			ec->blah++;

			*isig++ = 0;
		} else {
			ec->blah = 0;
			
			isig++;
		}
	}
}

static int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	return 0;
}

static const struct dahdi_echocan me = {
	.name = "JPAH",
	.owner = THIS_MODULE,
	.echo_can_create = echo_can_create,
	.echo_can_free = echo_can_free,
	.echo_can_array_update = echo_can_update,
	.echo_can_traintap = echo_can_traintap,
};

static int __init mod_init(void)
{
	if (dahdi_register_echocan(&me)) {
		module_printk(KERN_ERR, "could not register with DAHDI core\n");

		return -EPERM;
	}

	module_printk(KERN_NOTICE, "Registered echo canceler '%s'\n", me.name);

	return 0;
}

static void __exit mod_exit(void)
{
	dahdi_unregister_echocan(&me);
}

module_param(debug, int, S_IRUGO | S_IWUSR);

MODULE_DESCRIPTION("DAHDI Jason Parker Audio Hoser");
MODULE_AUTHOR("Jason Parker <jparker@digium.com>");
MODULE_LICENSE("GPL v2");

module_init(mod_init);
module_exit(mod_exit);
