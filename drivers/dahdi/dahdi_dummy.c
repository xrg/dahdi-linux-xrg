/*
 * Dummy Zaptel Driver for Zapata Telephony interface
 *
 * Required: usb-uhci module and kernel > 2.4.4 OR kernel > 2.6.0
 *
 * Written by Robert Pleh <robert.pleh@hermes.si>
 * 2.6 version by Tony Hoyle
 * Unified by Mark Spencer <markster@digium.com>
 * Converted to use RTC on i386 by Tony Mountifield <tony@softins.co.uk>
 *
 * Converted to use HighResTimers on i386 by Jeffery Palmer <jeff@triggerinc.com>
 *
 * Copyright (C) 2002, Hermes Softlab
 * Copyright (C) 2004, Digium, Inc.
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

/*
 * To use the high resolution timers, in your kernel CONFIG_HIGH_RES_TIMERS 
 * needs to be enabled (Processor type and features -> High Resolution 
 * Timer Support), and optionally HPET (Processor type and features -> 
 * HPET Timer Support) provides a better clock source.
 */

#include <linux/version.h>

#ifndef VERSION_CODE
#  define VERSION_CODE(vers,rel,seq) ( ((vers)<<16) | ((rel)<<8) | (seq) )
#endif


#if LINUX_VERSION_CODE < VERSION_CODE(2,4,5)
#  error "This kernel is too old: not supported by this file"
#endif

/*
 * NOTE: (only applies to kernel 2.6)
 * If using an i386 architecture without a PC real-time clock,
 * the #define USE_RTC should be commented out.
 */
#if defined(__i386__) || defined(__x86_64__)
#if LINUX_VERSION_CODE >= VERSION_CODE(2,6,13)
/* The symbol hrtimer_forward is only exported as of 2.6.22: */
#if defined(CONFIG_HIGH_RES_TIMERS) && LINUX_VERSION_CODE >= VERSION_CODE(2,6,22)
#define USE_HIGHRESTIMER
#else
#define USE_RTC
#endif
#else
#if 0
#define USE_RTC
#endif
#endif
#endif

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <dahdi/kernel.h>
#include <dahdi/user.h>

#ifndef LINUX26
#include <linux/usb.h>
#include <linux/pci.h>
#include <asm/io.h>
#endif
#ifdef LINUX26
#ifdef USE_HIGHRESTIMER
#include <linux/hrtimer.h>
#endif
#ifdef USE_RTC
#include <linux/rtc.h>
#endif
#include <linux/moduleparam.h>
#endif

#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,19)
#define USB2420
#endif

struct ztdummy {
	struct dahdi_span span;
	struct dahdi_chan chan;
#ifdef LINUX26
	unsigned int counter;
#ifdef USE_RTC
	spinlock_t rtclock;
	rtc_task_t rtc_task;
#endif
#endif
};


#ifndef LINUX26
/* Uhci definitions and structures - from file usb-uhci.h */
#define TD_CTRL_IOC		(1 << 24)	/* Interrupt on Complete */
#define USBSTS 2

typedef enum {
	TD_TYPE, QH_TYPE
} uhci_desc_type_t;

typedef struct {
	__u32 link;
	__u32 status;
	__u32 info;
	__u32 buffer;
} uhci_td_t, *puhci_td_t;


typedef struct {
	__u32 head;
	__u32 element;		/* Queue element pointer */
} uhci_qh_t, *puhci_qh_t;

typedef struct {
	union {
		uhci_td_t td;
		uhci_qh_t qh;
	} hw;
	uhci_desc_type_t type;
	dma_addr_t dma_addr;
	struct list_head horizontal;
	struct list_head vertical;
	struct list_head desc_list;
	int last_used;
} uhci_desc_t, *puhci_desc_t;

typedef struct {
	struct list_head desc_list;	// list pointer to all corresponding TDs/QHs associated with this request
	dma_addr_t setup_packet_dma;
	dma_addr_t transfer_buffer_dma;
	unsigned long started;
#ifdef USB2420
        struct urb *next_queued_urb;    // next queued urb for this EP
        struct urb *prev_queued_urb;
#else
        urb_t *next_queued_urb;         
        urb_t *prev_queued_urb;
#endif
	uhci_desc_t *bottom_qh;
	uhci_desc_t *next_qh;       	// next helper QH
	char use_loop;
	char flags;
} urb_priv_t, *purb_priv_t;

struct virt_root_hub {
	int devnum;		/* Address of Root Hub endpoint */
	void *urb;
	void *int_addr;
	int send;
	int interval;
	int numports;
	int c_p_r[8];
	struct timer_list rh_int_timer;
};

typedef struct uhci {
	int irq;
	unsigned int io_addr;
	unsigned int io_size;
	unsigned int maxports;
	int running;

	int apm_state;

	struct uhci *next;	// chain of uhci device contexts

	struct list_head urb_list;	// list of all pending urbs

	spinlock_t urb_list_lock;	// lock to keep consistency

	int unlink_urb_done;
	atomic_t avoid_bulk;

	struct usb_bus *bus;	// our bus

	__u32 *framelist;
	dma_addr_t framelist_dma;
	uhci_desc_t **iso_td;
	uhci_desc_t *int_chain[8];
	uhci_desc_t *ls_control_chain;
	uhci_desc_t *control_chain;
	uhci_desc_t *bulk_chain;
	uhci_desc_t *chain_end;
	uhci_desc_t *td1ms;
	uhci_desc_t *td32ms;
	struct list_head free_desc;
	spinlock_t qh_lock;
	spinlock_t td_lock;
	struct virt_root_hub rh;	//private data of the virtual root hub
	int loop_usage;            // URBs using bandwidth reclamation

	struct list_head urb_unlinked;	// list of all unlinked  urbs
	long timeout_check;
	int timeout_urbs;
	struct pci_dev *uhci_pci;
	struct pci_pool *desc_pool;
	long last_error_time;          // last error output in uhci_interrupt()
} uhci_t, *puhci_t;
#endif

static struct ztdummy *ztd;

static int debug = 0;

#ifdef LINUX26
#ifdef USE_HIGHRESTIMER
#define CLOCK_SRC "HRtimer"
struct hrtimer zaptimer;
#elif defined(USE_RTC)
#define CLOCK_SRC "RTC"
static int rtc_rate = 0;
static int current_rate = 0;
static int taskletpending = 0;
static struct tasklet_struct ztd_tlet;
static void ztd_tasklet(unsigned long data);
#else /* Linux 2.6, but no RTC or HRTIMER used */
#define CLOCK_SRC "Linux26"
/* 2.6 kernel timer stuff */
static struct timer_list timer;
#endif
#else
#if LINUX_VERSION_CODE < VERSION_CODE(2,4,5)
#  error "This kernel is too old: not supported by this file"
#endif
#define CLOCK_SRC "UHCI"
/* Old UCHI stuff */
static    uhci_desc_t  *td;
static    uhci_t *s;
static int monitor = 0;

/* exported kernel symbols */
extern int insert_td (uhci_t *s, uhci_desc_t *qh, uhci_desc_t* new, int flags);
extern int alloc_td (uhci_t *s, uhci_desc_t ** new, int flags);
extern  int insert_td_horizontal (uhci_t *s, uhci_desc_t *td, uhci_desc_t* new);
extern int unlink_td (uhci_t *s, uhci_desc_t *element, int phys_unlink);
extern void fill_td (uhci_desc_t *td, int status, int info, __u32 buffer);
extern void uhci_interrupt (int irq, void *__uhci, struct pt_regs *regs);
extern int delete_desc (uhci_t *s, uhci_desc_t *element);
extern uhci_t **uhci_devices;

#endif


#define ZAPTEL_RATE 1000                     /* zaptel ticks per second */
#define ZAPTEL_TIME (1000000 / ZAPTEL_RATE)  /* zaptel tick time in us */
#define ZAPTEL_TIME_NS (ZAPTEL_TIME * 1000)  /* zaptel tick time in ns */

/* Different bits of the debug variable: */
#define DEBUG_GENERAL (1 << 0)
#define DEBUG_TICKS   (1 << 1)


#ifdef LINUX26
#ifdef USE_RTC
static void update_rtc_rate(struct ztdummy *ztd)
{
	if (((rtc_rate & (rtc_rate - 1)) != 0) || (rtc_rate > 8192) || (rtc_rate < 2)) {
		printk(KERN_NOTICE "Invalid RTC rate %d specified\n", rtc_rate);
		rtc_rate = current_rate;	/* Set default RTC rate */
	}
	if (!rtc_rate || (rtc_rate != current_rate)) {
		rtc_control(&ztd->rtc_task, RTC_IRQP_SET, current_rate = (rtc_rate ? rtc_rate : 1024));	/* 1024 Hz */
		printk(KERN_INFO "ztdummy: RTC rate is %d\n", rtc_rate);
		ztd->counter = 0;
	}
}

static void ztd_tasklet(unsigned long data)
{
	if (taskletpending)
		update_rtc_rate((struct ztdummy *)ztd);
	taskletpending = 0;
}

/* rtc_interrupt - called at 1024Hz from hook in RTC handler */
static void ztdummy_rtc_interrupt(void *private_data)
{
	struct ztdummy *ztd = private_data;
	unsigned long flags;

	/* Is spinlock required here??? */
	spin_lock_irqsave(&ztd->rtclock, flags);
	ztd->counter += ZAPTEL_TIME;
	while (ztd->counter >= current_rate) {
		ztd->counter -= current_rate;
		/* Update of RTC IRQ rate isn't possible from interrupt handler :( */
		if (!taskletpending && (current_rate != rtc_rate)) {
			taskletpending = 1;
			tasklet_hi_schedule(&ztd_tlet);
		}
		dahdi_receive(&ztd->span);
		dahdi_transmit(&ztd->span);
	}
	spin_unlock_irqrestore(&ztd->rtclock, flags);
}
#elif defined(USE_HIGHRESTIMER)
static enum hrtimer_restart ztdummy_hr_int(struct hrtimer *htmr)
{
	unsigned long overrun;
	
	/* Trigger Zaptel */
	dahdi_receive(&ztd->span);
	dahdi_transmit(&ztd->span);

	/* Overrun should always return 1, since we are in the timer that 
	 * expired.
	 * We should worry if overrun is 2 or more; then we really missed 
	 * a tick */
	overrun = hrtimer_forward(&zaptimer, htmr->expires, 
			ktime_set(0, ZAPTEL_TIME_NS));
	if(overrun > 1) {
		if(printk_ratelimit())
			printk(KERN_NOTICE "ztdummy: HRTimer missed %lu ticks\n", 
					overrun - 1);
	}

	if(debug && DEBUG_TICKS) {
		static int count = 0;
		/* Printk every 5 seconds, good test to see if timer is 
		 * running properly */
		if (count++ % 5000 == 0)
			printk(KERN_DEBUG "ztdummy: 5000 ticks from hrtimer\n");
	}

	/* Always restart the timer */
	return HRTIMER_RESTART;
}
#else
/* use kernel system tick timer if PC architecture RTC is not available */
static void ztdummy_timer(unsigned long param)
{
	timer.expires = jiffies + 1;
	add_timer(&timer);

	ztd->counter += ZAPTEL_TIME;
	while (ztd->counter >= HZ) {
		ztd->counter -= HZ;
		dahdi_receive(&ztd->span);
		dahdi_transmit(&ztd->span);
	}
}
#endif
#else
static void ztdummy_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned short status;
	unsigned int io_addr = s->io_addr;

	status = inw (io_addr + USBSTS);
	if (status != 0)  {	/* interrupt from our USB port */
		static int check_int = 0;
		dahdi_receive(&ztd->span);
		dahdi_transmit(&ztd->span);
		/* TODO: What's the relation between monitor and
		 * DEBUG_TICKS */
		if (monitor && check_int) {
			check_int = 1;
			printk(KERN_NOTICE "ztdummy: interrupt triggered \n");     
		}   
	}
	return;
}
#endif

static int ztdummy_initialize(struct ztdummy *ztd)
{
	/* Zapata stuff */
	sprintf(ztd->span.name, "ZTDUMMY/1");
	snprintf(ztd->span.desc, sizeof(ztd->span.desc) - 1, "%s (source: " CLOCK_SRC ") %d", ztd->span.name, 1);
	sprintf(ztd->chan.name, "ZTDUMMY/%d/%d", 1, 0);
	dahdi_copy_string(ztd->span.devicetype, "Zaptel Dummy Timing Driver", sizeof(ztd->span.devicetype));
	ztd->chan.chanpos = 1;
	ztd->span.chans = &ztd->chan;
	ztd->span.channels = 0;		/* no channels on our span */
	ztd->span.deflaw = DAHDI_LAW_MULAW;
	init_waitqueue_head(&ztd->span.maintq);
	ztd->span.pvt = ztd;
	ztd->chan.pvt = ztd;
	if (dahdi_register(&ztd->span, 0)) {
		return -1;
	}
	return 0;
}

int init_module(void)
{
#ifdef LINUX26
#ifdef USE_RTC
	int err;
#endif
#else
	int irq;
#ifdef DEFINE_SPINLOCK
	DEFINE_SPINLOCK(mylock);
#else
	spinlock_t mylock = SPIN_LOCK_UNLOCKED;
#endif
	
	if (uhci_devices==NULL) {
		printk (KERN_ERR "ztdummy: Uhci_devices pointer error.\n");
		return -ENODEV;
	}
	s=*uhci_devices;	/* uhci device */
	if (s==NULL) {
		printk (KERN_ERR "ztdummy: No uhci_device found.\n");
		return -ENODEV;
	}
#endif

	ztd = kmalloc(sizeof(struct ztdummy), GFP_KERNEL);
	if (ztd == NULL) {
		printk(KERN_ERR "ztdummy: Unable to allocate memory\n");
		return -ENOMEM;
	}

	memset(ztd, 0x0, sizeof(struct ztdummy));

	if (ztdummy_initialize(ztd)) {
		printk(KERN_ERR "ztdummy: Unable to intialize zaptel driver\n");
		kfree(ztd);
		return -ENODEV;
	}

#ifdef LINUX26
	ztd->counter = 0;
#ifdef USE_RTC
	ztd->rtclock = SPIN_LOCK_UNLOCKED;
	ztd->rtc_task.func = ztdummy_rtc_interrupt;
	ztd->rtc_task.private_data = ztd;
	err = rtc_register(&ztd->rtc_task);
	if (err < 0) {
		printk(KERN_ERR "ztdummy: Unable to register zaptel rtc driver\n");
		dahdi_unregister(&ztd->span);
		kfree(ztd);
		return err;
	}
	/* Set default RTC interrupt rate to 1024Hz */
	if (!rtc_rate)
		rtc_rate = 1024;
	update_rtc_rate(ztd);
	rtc_control(&ztd->rtc_task, RTC_PIE_ON, 0);
	tasklet_init(&ztd_tlet, ztd_tasklet, 0);
#elif defined(USE_HIGHRESTIMER)
	printk(KERN_DEBUG "ztdummy: Trying to load High Resolution Timer\n");
	hrtimer_init(&zaptimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	printk(KERN_DEBUG "ztdummy: Initialized High Resolution Timer\n");

	/* Set timer callback function */
	zaptimer.function = ztdummy_hr_int;

	printk(KERN_DEBUG "ztdummy: Starting High Resolution Timer\n");
	hrtimer_start(&zaptimer, ktime_set(0, ZAPTEL_TIME_NS), HRTIMER_MODE_REL);
	printk(KERN_INFO "ztdummy: High Resolution Timer started, good to go\n");
#else
	init_timer(&timer);
	timer.function = ztdummy_timer;
	timer.expires = jiffies + 1;
	add_timer(&timer);
#endif
#else
	irq=s->irq;
	spin_lock_irq(&mylock);
	free_irq(s->irq, s);	/* remove uhci_interrupt temporaly */
	if (request_irq (irq, ztdummy_interrupt, DAHDI_IRQ_SHARED, "ztdummy", ztd)) {
		spin_unlock_irq(&mylock);
		err("Our request_irq %d failed!",irq);
		kfree(ztd);
		return -EIO;
	}		/* we add our handler first, to assure, that our handler gets called first */
	if (request_irq (irq, uhci_interrupt, DAHDI_IRQ_SHARED, s->uhci_pci->driver->name, s)) {
		spin_unlock_irq(&mylock);
		err("Original request_irq %d failed!",irq);
	}
	spin_unlock_irq(&mylock);

	/* add td to usb host controller interrupt queue */
	alloc_td(s, &td, 0);
	fill_td(td, TD_CTRL_IOC, 0, 0);
	insert_td_horizontal(s, s->int_chain[0], td);	/* use int_chain[0] to get 1ms interrupts */
#endif	

	if (debug)
		printk(KERN_DEBUG "ztdummy: init() finished\n");
	return 0;
}


void cleanup_module(void)
{
#ifdef LINUX26
#ifdef USE_RTC
	if (taskletpending) {
		tasklet_disable(&ztd_tlet);
		tasklet_kill(&ztd_tlet);
	}
	rtc_control(&ztd->rtc_task, RTC_PIE_OFF, 0);
	rtc_unregister(&ztd->rtc_task);
#elif defined(USE_HIGHRESTIMER)
	/* Stop high resolution timer */
	hrtimer_cancel(&zaptimer);
#else
	del_timer(&timer);
#endif
#else
	free_irq(s->irq, ztd);  /* disable interrupts */
#endif
	dahdi_unregister(&ztd->span);
	kfree(ztd);
#ifndef LINUX26
	unlink_td(s, td, 1);
	delete_desc(s, td);
#endif
	if (debug)
		printk("ztdummy: cleanup() finished\n");
}



#ifdef LINUX26
module_param(debug, int, 0600);
#ifdef USE_RTC
module_param(rtc_rate, int, 0600);
#endif
#else
MODULE_PARM(debug, "i");
#endif

#ifndef LINUX26
MODULE_PARM(monitor, "i");
#endif
MODULE_DESCRIPTION("Dummy Zaptel Driver");
MODULE_AUTHOR("Robert Pleh <robert.pleh@hermes.si>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
