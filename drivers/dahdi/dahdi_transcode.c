/*
 * Transcoder Interface for DAHDI
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2006-2007, Digium, Inc.
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
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/moduleparam.h>
#include <asm/io.h>

#include <dahdi/kernel.h>
#include <dahdi/user.h>

static int debug = 0;
static struct dahdi_transcoder *trans;
static spinlock_t translock = SPIN_LOCK_UNLOCKED;

EXPORT_SYMBOL(dahdi_transcoder_register);
EXPORT_SYMBOL(dahdi_transcoder_unregister);
EXPORT_SYMBOL(dahdi_transcoder_alert);
EXPORT_SYMBOL(dahdi_transcoder_alloc);
EXPORT_SYMBOL(dahdi_transcoder_free);

struct dahdi_transcoder *dahdi_transcoder_alloc(int numchans)
{
	struct dahdi_transcoder *ztc;
	unsigned int x;
	size_t size = sizeof(*ztc) + (sizeof(ztc->channels[0]) * numchans);

	if (!(ztc = kmalloc(size, GFP_KERNEL)))
		return NULL;

	memset(ztc, 0, size);
	strcpy(ztc->name, "<unspecified>");
	ztc->numchannels = numchans;
	for (x=0;x<ztc->numchannels;x++) {
		init_waitqueue_head(&ztc->channels[x].ready);
		ztc->channels[x].parent = ztc;
		ztc->channels[x].offset = x;
		ztc->channels[x].chan_built = 0;
		ztc->channels[x].built_fmts = 0;
	}

	return ztc;
}

static int schluffen(wait_queue_head_t *q)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(q, &wait);
	current->state = TASK_INTERRUPTIBLE;

	if (!signal_pending(current))
		schedule();

	current->state = TASK_RUNNING;
	remove_wait_queue(q, &wait);

	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

void dahdi_transcoder_free(struct dahdi_transcoder *ztc)
{
	kfree(ztc);
}

/* Register a transcoder */
int dahdi_transcoder_register(struct dahdi_transcoder *tc)
{
	struct dahdi_transcoder *cur;
	int res = -EBUSY;

	spin_lock(&translock);
	for (cur = trans; cur; cur = cur->next) {
		if (cur == tc) {
			spin_unlock(&translock);
			return res;
		}
	}

	tc->next = trans;
	trans = tc;
	printk("Registered codec translator '%s' with %d transcoders (srcs=%08x, dsts=%08x)\n", 
	       tc->name, tc->numchannels, tc->srcfmts, tc->dstfmts);
	res = 0;
	spin_unlock(&translock);

	return res;
}

/* Unregister a transcoder */
int dahdi_transcoder_unregister(struct dahdi_transcoder *tc) 
{
	struct dahdi_transcoder *cur, *prev;
	int res = -EINVAL;

	spin_lock(&translock);
	for (cur = trans, prev = NULL; cur; prev = cur, cur = cur->next) {
		if (cur == tc)
			break;
	}

	if (!cur) {
		spin_unlock(&translock);
		return res;
	}

	if (prev)
		prev->next = tc->next;
	else
		trans = tc->next;
	tc->next = NULL;
	printk("Unregistered codec translator '%s' with %d transcoders (srcs=%08x, dsts=%08x)\n", 
	       tc->name, tc->numchannels, tc->srcfmts, tc->dstfmts);
	res = 0;
	spin_unlock(&translock);

	return res;
}

/* Alert a transcoder */
int dahdi_transcoder_alert(struct dahdi_transcoder_channel *ztc)
{
	if (debug)
		printk("DAHDI Transcoder Alert!\n");
	if (ztc->tch)
		ztc->tch->status &= ~DAHDI_TC_FLAG_BUSY;
	wake_up_interruptible(&ztc->ready);

	return 0;
}

static int dahdi_tc_open(struct inode *inode, struct file *file)
{
	struct dahdi_transcoder_channel *ztc;
	struct dahdi_transcode_header *zth;
	struct page *page;

	if (!(ztc = kmalloc(sizeof(*ztc), GFP_KERNEL)))
		return -ENOMEM;

	if (!(zth = kmalloc(sizeof(*zth), GFP_KERNEL | GFP_DMA))) {
		kfree(ztc);
		return -ENOMEM;
	}
	
	memset(ztc, 0, sizeof(*ztc));
	memset(zth, 0, sizeof(*zth));
	ztc->flags = DAHDI_TC_FLAG_TRANSIENT | DAHDI_TC_FLAG_BUSY;
	ztc->tch = zth;
	if (debug)
		printk("Allocated Transcoder Channel, header is at %p!\n", zth);
	zth->magic = DAHDI_TRANSCODE_MAGIC;
	file->private_data = ztc;
	for (page = virt_to_page(zth);
	     page < virt_to_page((unsigned long) zth + sizeof(*zth));
	     page++)
		SetPageReserved(page);

	return 0;
}

static void ztc_release(struct dahdi_transcoder_channel *ztc)
{
	struct dahdi_transcode_header *zth = ztc->tch;
	struct page *page;

	if (!ztc)
		return;

	ztc->flags &= ~(DAHDI_TC_FLAG_BUSY);

	if(ztc->tch) {
		for (page = virt_to_page(zth);
		     page < virt_to_page((unsigned long) zth + sizeof(*zth));
		     page++)
			ClearPageReserved(page);
		kfree(ztc->tch);
	}

	ztc->tch = NULL;
	/* Actually reset the transcoder channel */
	if (ztc->flags & DAHDI_TC_FLAG_TRANSIENT)
		kfree(ztc);
	if (debug)
		printk("Released Transcoder!\n");
}

static int dahdi_tc_release(struct inode *inode, struct file *file)
{
	ztc_release(file->private_data);

	return 0;
}

static int do_reset(struct dahdi_transcoder_channel **ztc)
{
	struct dahdi_transcoder_channel *newztc = NULL, *origztc = NULL;
	struct dahdi_transcode_header *zth = (*ztc)->tch;
	struct dahdi_transcoder *tc;
	unsigned int x;
	unsigned int match = 0;

	if (((*ztc)->srcfmt != zth->srcfmt) ||
	    ((*ztc)->dstfmt != zth->dstfmt)) {
		/* Find new transcoder */
		spin_lock(&translock);
		for (tc = trans; tc && !newztc; tc = tc->next) {
			if (!(tc->srcfmts & zth->srcfmt))
				continue;

			if (!(tc->dstfmts & zth->dstfmt))
				continue;

			match = 1;
			for (x = 0; x < tc->numchannels; x++) {
				if (tc->channels[x].flags & DAHDI_TC_FLAG_BUSY)
					continue;
				if ((tc->channels[x].chan_built) && ((zth->srcfmt | zth->dstfmt) != tc->channels[x].built_fmts))
					continue;

				newztc = &tc->channels[x];
				newztc->flags = DAHDI_TC_FLAG_BUSY;
				break;
			}
		}
		spin_unlock(&translock);

		if (!newztc)
			return match ? -EBUSY : -ENOSYS;

		/* Move transcoder header over */
		origztc = (*ztc);
		(*ztc) = newztc;
		(*ztc)->tch = origztc->tch;
		origztc->tch = NULL;
		(*ztc)->flags |= (origztc->flags & ~(DAHDI_TC_FLAG_TRANSIENT));
		ztc_release(origztc);
	}

	/* Actually reset the transcoder channel */
	if ((*ztc)->parent && ((*ztc)->parent->operation))
		return (*ztc)->parent->operation((*ztc), DAHDI_TCOP_ALLOCATE);

	return -EINVAL;
}

static int wait_busy(struct dahdi_transcoder_channel *ztc)
{
	int ret;

	for (;;) {
		if (!(ztc->tch->status & DAHDI_TC_FLAG_BUSY))
			return 0;
		if ((ret = schluffen(&ztc->ready)))
			return ret;
	}
}

static int dahdi_tc_getinfo(unsigned long data)
{
	struct dahdi_transcode_info info;
	unsigned int x;
	struct dahdi_transcoder *tc;
	
	if (copy_from_user(&info, (struct dahdi_transcode_info *) data, sizeof(info)))
		return -EFAULT;

	spin_lock(&translock);
	for (tc = trans, x = info.tcnum; tc && x; tc = tc->next, x--);
	spin_unlock(&translock);

	if (!tc)
		return -ENOSYS;

	dahdi_copy_string(info.name, tc->name, sizeof(info.name));
	info.numchannels = tc->numchannels;
	info.srcfmts = tc->srcfmts;
	info.dstfmts = tc->dstfmts;

	return copy_to_user((struct dahdi_transcode_info *) data, &info, sizeof(info)) ? -EFAULT : 0;
}

static int dahdi_tc_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data)
{
	int op;
	int ret;
	struct dahdi_transcoder_channel *ztc = file->private_data;

	if (cmd != DAHDI_TRANSCODE_OP)
		return -ENOSYS;

	if (get_user(op, (int *) data))
		return -EFAULT;

	if (debug)
		printk("DAHDI Transcode ioctl op = %d!\n", op);

	switch(op) {
	case DAHDI_TCOP_GETINFO:
		ret = dahdi_tc_getinfo(data);
		break;
	case DAHDI_TCOP_ALLOCATE:
		/* Reset transcoder, possibly changing who we point to */
		ret = do_reset(&ztc);
		file->private_data = ztc;
		break;
	case DAHDI_TCOP_RELEASE:
		ret = ztc->parent->operation(ztc, DAHDI_TCOP_RELEASE);
		break;
	case DAHDI_TCOP_TEST:
		ret = ztc->parent->operation(ztc, DAHDI_TCOP_TEST);
		break;
	case DAHDI_TCOP_TRANSCODE:
		if (!ztc->parent->operation)
			return -EINVAL;

		ztc->tch->status |= DAHDI_TC_FLAG_BUSY;
		if (!(ret = ztc->parent->operation(ztc, DAHDI_TCOP_TRANSCODE))) {
			/* Wait for busy to go away if we're not non-blocking */
			if (!(file->f_flags & O_NONBLOCK)) {
				if (!(ret = wait_busy(ztc)))
					ret = ztc->errorstatus;
			}
		} else
			ztc->tch->status &= ~DAHDI_TC_FLAG_BUSY;
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static int dahdi_tc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dahdi_transcoder_channel *ztc = file->private_data;
	unsigned long physical;
	int res;

	if (!ztc)
		return -EINVAL;

	/* Do not allow an offset */
	if (vma->vm_pgoff) {
		if (debug)
			printk("zttranscode: Attempted to mmap with offset!\n");
		return -EINVAL;
	}

	if ((vma->vm_end - vma->vm_start) != sizeof(struct dahdi_transcode_header)) {
		if (debug)
			printk("zttranscode: Attempted to mmap with size %d != %zd!\n", (int) (vma->vm_end - vma->vm_start), sizeof(struct dahdi_transcode_header));
		return -EINVAL;
	}

	physical = (unsigned long) virt_to_phys(ztc->tch);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	res = remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, sizeof(struct dahdi_transcode_header), PAGE_SHARED);
#else
  #if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	res = remap_page_range(vma->vm_start, physical, sizeof(struct dahdi_transcode_header), PAGE_SHARED);
  #else
	res = remap_page_range(vma, vma->vm_start, physical, sizeof(struct dahdi_transcode_header), PAGE_SHARED);
  #endif
#endif
	if (res) {
		if (debug)
			printk("zttranscode: remap failed!\n");
		return -EAGAIN;
	}

	if (debug)
		printk("zttranscode: successfully mapped transcoder!\n");

	return 0;
}

static unsigned int dahdi_tc_poll(struct file *file, struct poll_table_struct *wait_table)
{
	struct dahdi_transcoder_channel *ztc = file->private_data;

	if (!ztc)
		return -EINVAL;

	poll_wait(file, &ztc->ready, wait_table);
	return ztc->tch->status & DAHDI_TC_FLAG_BUSY ? 0 : POLLPRI;
}

static struct file_operations __dahdi_transcode_fops = {
	owner: THIS_MODULE,
	llseek: NULL,
	open: dahdi_tc_open,
	release: dahdi_tc_release,
	ioctl: dahdi_tc_ioctl,
	read: NULL,
	write: NULL,
	poll: dahdi_tc_poll,
	mmap: dahdi_tc_mmap,
	flush: NULL,
	fsync: NULL,
	fasync: NULL,
};

static struct dahdi_chardev transcode_chardev = {
	.name = "transcode",
	.minor = 250,
};

int zttranscode_init(void)
{
	int res;

	if (dahdi_transcode_fops) {
		printk("Whoa, dahdi_transcode_fops already set?!\n");
		return -EBUSY;
	}

	dahdi_transcode_fops = &__dahdi_transcode_fops;

	if ((res = dahdi_register_chardev(&transcode_chardev)))
		return res;

	printk("DAHDI Transcoder support loaded\n");

	return 0;
}

void zttranscode_cleanup(void)
{
	dahdi_unregister_chardev(&transcode_chardev);

	dahdi_transcode_fops = NULL;

	printk("DAHDI Transcoder support unloaded\n");
}

module_param(debug, int, S_IRUGO | S_IWUSR);

MODULE_DESCRIPTION("DAHDI Transcoder Support");
MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(zttranscode_init);
module_exit(zttranscode_cleanup);
