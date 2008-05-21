/*
 * Dynamic Span Interface for DAHDI (Ethernet Interface)
 *
 * Written by Mark Spencer <markster@digium.com>
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>

#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

#include <dahdi/kernel.h>
#include <dahdi/user.h>

#define ETH_P_ZTDETH	0xd00d

struct ztdeth_header {
	unsigned short subaddr;
};

/* We take the raw message, put it in an ethernet frame, and add a
   two byte addressing header at the top for future use */
#ifdef DEFINE_SPINLOCK
static DEFINE_SPINLOCK(zlock);
#else
static spinlock_t zlock = SPIN_LOCK_UNLOCKED;
#endif

static struct sk_buff_head skbs;

static struct ztdeth {
	unsigned char addr[ETH_ALEN];
	unsigned short subaddr; /* Network byte order */
	struct dahdi_span *span;
	char ethdev[IFNAMSIZ];
	struct net_device *dev;
	struct ztdeth *next;
} *zdevs = NULL;

struct dahdi_span *ztdeth_getspan(unsigned char *addr, unsigned short subaddr)
{
	unsigned long flags;
	struct ztdeth *z;
	struct dahdi_span *span = NULL;
	spin_lock_irqsave(&zlock, flags);
	z = zdevs;
	while(z) {
		if (!memcmp(addr, z->addr, ETH_ALEN) &&
			z->subaddr == subaddr)
			break;
		z = z->next;
	}
	if (z)
		span = z->span;
	spin_unlock_irqrestore(&zlock, flags);
	return span;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
static int ztdeth_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
#else
static int ztdeth_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
#endif
{
	struct dahdi_span *span;
	struct ztdeth_header *zh;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
	zh = (struct ztdeth_header *)skb_network_header(skb);
#else
	zh = (struct ztdeth_header *)skb->nh.raw;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	span = ztdeth_getspan(eth_hdr(skb)->h_source, zh->subaddr);
#else
	span = ztdeth_getspan(skb->mac.ethernet->h_source, zh->subaddr);
#endif	
	if (span) {
		skb_pull(skb, sizeof(struct ztdeth_header));
		dahdi_dynamic_receive(span, (unsigned char *)skb->data, skb->len);
	}
	kfree_skb(skb);
	return 0;
}

static int ztdeth_notifier(struct notifier_block *block, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct ztdeth *z;
	unsigned long flags;
	switch(event) {
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
		spin_lock_irqsave(&zlock, flags);
		z = zdevs;
		while(z) {
			/* Note that the device no longer exists */
			if (z->dev == dev)
				z->dev = NULL;
			z = z->next;
		}
		spin_unlock_irqrestore(&zlock, flags);
		break;
	case NETDEV_UP:
		spin_lock_irqsave(&zlock, flags);
		z = zdevs;
		while(z) {
			/* Now that the device exists again, use it */
			if (!strcmp(z->ethdev, dev->name))
				z->dev = dev;
			z = z->next;
		}
		spin_unlock_irqrestore(&zlock, flags);
		break;
	}
	return 0;
}

static int ztdeth_transmit(void *pvt, unsigned char *msg, int msglen)
{
	struct ztdeth *z;
	struct sk_buff *skb;
	struct ztdeth_header *zh;
	unsigned long flags;
	struct net_device *dev;
	unsigned char addr[ETH_ALEN];
	unsigned short subaddr; /* Network byte order */

	spin_lock_irqsave(&zlock, flags);
	z = pvt;
	if (z->dev) {
		/* Copy fields to local variables to remove spinlock ASAP */
		dev = z->dev;
		memcpy(addr, z->addr, sizeof(z->addr));
		subaddr = z->subaddr;
		spin_unlock_irqrestore(&zlock, flags);
		skb = dev_alloc_skb(msglen + dev->hard_header_len + sizeof(struct ztdeth_header) + 32);
		if (skb) {
			/* Reserve header space */
			skb_reserve(skb, dev->hard_header_len + sizeof(struct ztdeth_header));

			/* Copy message body */
			memcpy(skb_put(skb, msglen), msg, msglen);

			/* Throw on header */
			zh = (struct ztdeth_header *)skb_push(skb, sizeof(struct ztdeth_header));
			zh->subaddr = subaddr;

			/* Setup protocol and such */
			skb->protocol = __constant_htons(ETH_P_ZTDETH);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
			skb_set_network_header(skb, 0);
#else
			skb->nh.raw = skb->data;
#endif
			skb->dev = dev;
#if  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
			dev_hard_header(skb, dev, ETH_P_ZTDETH, addr, dev->dev_addr, skb->len);
#else
			if (dev->hard_header)
				dev->hard_header(skb, dev, ETH_P_ZTDETH, addr, dev->dev_addr, skb->len);
#endif
			skb_queue_tail(&skbs, skb);
		}
	}
	else
		spin_unlock_irqrestore(&zlock, flags);
	return 0;
}


static int ztdeth_flush(void)
{
	struct sk_buff *skb;

	/* Handle all transmissions now */
	while ((skb = skb_dequeue(&skbs))) {
		dev_queue_xmit(skb);
	}
	return 0;
}

static struct packet_type ztdeth_ptype = {
	type: __constant_htons(ETH_P_ZTDETH),		/* Protocol */
	dev: NULL,					/* Device (NULL = wildcard) */
	func: ztdeth_rcv,				/* Receiver */
};

static int digit2int(char d)
{
	switch(d) {
	case 'F':
	case 'E':
	case 'D':
	case 'C':
	case 'B':
	case 'A':
		return d - 'A' + 10;
	case 'f':
	case 'e':
	case 'd':
	case 'c':
	case 'b':
	case 'a':
		return d - 'a' + 10;
	case '9':
	case '8':
	case '7':
	case '6':
	case '5':
	case '4':
	case '3':
	case '2':
	case '1':
	case '0':
		return d - '0';
	}
	return -1;
}

static int hex2int(char *s)
{
	int res;
	int tmp;
	/* Gotta be at least one digit */
	if (strlen(s) < 1)
		return -1;
	/* Can't be more than two */
	if (strlen(s) > 2)
		return -1;
	/* Grab the first digit */
	res = digit2int(s[0]);
	if (res < 0)
		return -1;
	tmp = res;
	/* Grab the next */
	if (strlen(s) > 1) {
		res = digit2int(s[1]);
		if (res < 0)
			return -1;
		tmp = tmp * 16 + res;
	}
	return tmp;
}

static void ztdeth_destroy(void *pvt)
{
	struct ztdeth *z = pvt;
	unsigned long flags;
	struct ztdeth *prev=NULL, *cur;
	spin_lock_irqsave(&zlock, flags);
	cur = zdevs;
	while(cur) {
		if (cur == z) {
			if (prev)
				prev->next = cur->next;
			else
				zdevs = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	spin_unlock_irqrestore(&zlock, flags);
	if (cur == z) {	/* Successfully removed */
		printk("TDMoE: Removed interface for %s\n", z->span->name);
		kfree(z);
#ifndef LINUX26
		MOD_DEC_USE_COUNT;
#else
		module_put(THIS_MODULE);
#endif
	}
}

static void *ztdeth_create(struct dahdi_span *span, char *addr)
{
	struct ztdeth *z;
	char src[256];
	char tmp[256], *tmp2, *tmp3, *tmp4 = NULL;
	int res,x;
	unsigned long flags;

	z = kmalloc(sizeof(struct ztdeth), GFP_KERNEL);
	if (z) {
		/* Zero it out */
		memset(z, 0, sizeof(struct ztdeth));

		/* Address should be <dev>/<macaddr>[/subaddr] */
		dahdi_copy_string(tmp, addr, sizeof(tmp));
		tmp2 = strchr(tmp, '/');
		if (tmp2) {
			*tmp2 = '\0';
			tmp2++;
			dahdi_copy_string(z->ethdev, tmp, sizeof(z->ethdev));
		} else {
			printk("Invalid TDMoE address (no device) '%s'\n", addr);
			kfree(z);
			return NULL;
		}
		if (tmp2) {
			tmp4 = strchr(tmp2+1, '/');
			if (tmp4) {
				*tmp4 = '\0';
				tmp4++;
			}
			/* We don't have SSCANF :(  Gotta do this the hard way */
			tmp3 = strchr(tmp2, ':');
			for (x=0;x<6;x++) {
				if (tmp2) {
					if (tmp3) {
						*tmp3 = '\0';
						tmp3++;
					}
					res = hex2int(tmp2);
					if (res < 0)
						break;
					z->addr[x] = res & 0xff;
				} else
					break;
				if ((tmp2 = tmp3))
					tmp3 = strchr(tmp2, ':');
			}
			if (x != 6) {
				printk("TDMoE: Invalid MAC address in: %s\n", addr);
				kfree(z);
				return NULL;
			}
		} else {
			printk("TDMoE: Missing MAC address\n");
			kfree(z);
			return NULL;
		}
		if (tmp4) {
			int sub = 0;
			int mul = 1;

			/* We have a subaddr */
			tmp3 = tmp4 + strlen (tmp4) - 1;
			while (tmp3 >= tmp4) {
				if (*tmp3 >= '0' && *tmp3 <= '9') {
					sub += (*tmp3 - '0') * mul;
				} else {
					printk("TDMoE: Invalid subaddress\n");
					kfree(z);
					return NULL;
				}
				mul *= 10;
				tmp3--;
			}
			z->subaddr = htons(sub);
		}
		z->dev = dev_get_by_name(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
				&init_net,
#endif
				z->ethdev);
		if (!z->dev) {
			printk("TDMoE: Invalid device '%s'\n", z->ethdev);
			kfree(z);
			return NULL;
		}
		z->span = span;
		src[0] ='\0';
		for (x=0;x<5;x++)
			sprintf(src + strlen(src), "%02x:", z->dev->dev_addr[x]);
		sprintf(src + strlen(src), "%02x", z->dev->dev_addr[5]);
		printk("TDMoE: Added new interface for %s at %s (addr=%s, src=%s, subaddr=%d)\n", span->name, z->dev->name, addr, src, ntohs(z->subaddr));

		spin_lock_irqsave(&zlock, flags);
		z->next = zdevs;
		zdevs = z;
		spin_unlock_irqrestore(&zlock, flags);
#ifndef LINUX26
		MOD_INC_USE_COUNT;
#else
		if(!try_module_get(THIS_MODULE))
			printk("TDMoE: Unable to increment module use count\n");
#endif
	}
	return z;
}

static struct dahdi_dynamic_driver ztd_eth = {
	"eth",
	"Ethernet",
	ztdeth_create,
	ztdeth_destroy,
	ztdeth_transmit,
	ztdeth_flush
};

static struct notifier_block ztdeth_nblock = {
	notifier_call: ztdeth_notifier,
};

static int __init ztdeth_init(void)
{
	dev_add_pack(&ztdeth_ptype);
	register_netdevice_notifier(&ztdeth_nblock);
	dahdi_dynamic_register(&ztd_eth);

	skb_queue_head_init(&skbs);

	return 0;
}

static void __exit ztdeth_exit(void)
{
	dev_remove_pack(&ztdeth_ptype);
	unregister_netdevice_notifier(&ztdeth_nblock);
	dahdi_dynamic_unregister(&ztd_eth);
}

MODULE_DESCRIPTION("DAHDI Dynamic TDMoE Support");
MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(ztdeth_init);
module_exit(ztdeth_exit);
