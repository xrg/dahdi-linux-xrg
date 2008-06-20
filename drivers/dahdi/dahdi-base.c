/*
 * DAHDI Telephony Interface Driver
 *
 * Written by Mark Spencer <markster@digium.com>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 * 
 * Special thanks to Steve Underwood <steve@coppice.org>
 * for substantial contributions to signal processing functions 
 * in DAHDI and the Zapata library.
 *
 * Yury Bokhoncovich <byg@cf1.ru>
 * Adaptation for 2.4.20+ kernels (HDLC API was changed)
 * The work has been performed as a part of our move
 * from Cisco 3620 to IBM x305 here in F1 Group
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001 -2006 Digium, Inc.
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


#include "dahdi_config.h"

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/moduleparam.h>
#include <linux/list.h>

#ifdef CONFIG_DAHDI_NET
#include <linux/netdevice.h>
#endif /* CONFIG_DAHDI_NET */

#include <linux/ppp_defs.h>
#ifdef CONFIG_DAHDI_PPP
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/if_ppp.h>
#endif

#include <asm/atomic.h>

#define module_printk(level, fmt, args...) printk(level "%s: " fmt, THIS_MODULE->name, ## args)

#ifndef CONFIG_OLD_HDLC_API
#define NEW_HDLC_INTERFACE
#endif

#define __ECHO_STATE_MUTE			(1 << 8)
#define ECHO_STATE_IDLE				(0)
#define ECHO_STATE_PRETRAINING		(1 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_STARTTRAINING	(2 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_AWAITINGECHO		(3 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_TRAINING			(4 | (__ECHO_STATE_MUTE))
#define ECHO_STATE_ACTIVE			(5)

/* #define BUF_MUNGE */

/* Grab fasthdlc with tables */
#define FAST_HDLC_NEED_TABLES
#include "fasthdlc.h"

#include <dahdi/version.h>
#include <dahdi/kernel.h>
#include <dahdi/user.h>

#include "hpec/hpec_user.h"

/* Get helper arithmetic */
#include "arith.h"
#if defined(CONFIG_DAHDI_MMX) || defined(ECHO_CAN_FP)
#include <asm/i387.h>
#endif

#define hdlc_to_ztchan(h) (((struct dahdi_hdlc *)(h))->chan)
#define dev_to_ztchan(h) (((struct dahdi_hdlc *)(dev_to_hdlc(h)->priv))->chan)
#define ztchan_to_dev(h) ((h)->hdlcnetdev->netdev)

/* macro-oni for determining a unit (channel) number */
#define	UNIT(file) MINOR(file->f_dentry->d_inode->i_rdev)

/* names of tx level settings */
static char *dahdi_txlevelnames[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
} ;

EXPORT_SYMBOL(dahdi_transcode_fops);
EXPORT_SYMBOL(dahdi_init_tone_state);
EXPORT_SYMBOL(dahdi_mf_tone);
EXPORT_SYMBOL(dahdi_register);
EXPORT_SYMBOL(dahdi_unregister);
EXPORT_SYMBOL(__dahdi_mulaw);
EXPORT_SYMBOL(__dahdi_alaw);
#ifdef CONFIG_CALC_XLAW
EXPORT_SYMBOL(__dahdi_lineartoulaw);
EXPORT_SYMBOL(__dahdi_lineartoalaw);
#else
EXPORT_SYMBOL(__dahdi_lin2mu);
EXPORT_SYMBOL(__dahdi_lin2a);
#endif
EXPORT_SYMBOL(dahdi_lboname);
EXPORT_SYMBOL(dahdi_transmit);
EXPORT_SYMBOL(dahdi_receive);
EXPORT_SYMBOL(dahdi_rbsbits);
EXPORT_SYMBOL(dahdi_qevent_nolock);
EXPORT_SYMBOL(dahdi_qevent_lock);
EXPORT_SYMBOL(dahdi_hooksig);
EXPORT_SYMBOL(dahdi_alarm_notify);
EXPORT_SYMBOL(dahdi_set_dynamic_ioctl);
EXPORT_SYMBOL(dahdi_ec_chunk);
EXPORT_SYMBOL(dahdi_ec_span);
EXPORT_SYMBOL(dahdi_hdlc_abort);
EXPORT_SYMBOL(dahdi_hdlc_finish);
EXPORT_SYMBOL(dahdi_hdlc_getbuf);
EXPORT_SYMBOL(dahdi_hdlc_putbuf);
EXPORT_SYMBOL(dahdi_alarm_channel);
EXPORT_SYMBOL(dahdi_register_chardev);
EXPORT_SYMBOL(dahdi_unregister_chardev);

EXPORT_SYMBOL(dahdi_register_echocan);
EXPORT_SYMBOL(dahdi_unregister_echocan);

EXPORT_SYMBOL(dahdi_set_hpec_ioctl);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_entries[DAHDI_MAX_SPANS]; 
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
#define CLASS_DEV_CREATE(class, devt, device, name) \
        class_device_create(class, NULL, devt, device, name)
#else
#define CLASS_DEV_CREATE(class, devt, device, name) \
        class_device_create(class, devt, device, name)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
static struct class *dahdi_class = NULL;
#else
static struct class_simple *dahdi_class = NULL;
#define class_create class_simple_create
#define class_destroy class_simple_destroy
#define class_device_create class_simple_device_add
#define class_device_destroy(a, b) class_simple_device_remove(b)
#endif

static int deftaps = 64;

static int debug;

/* states for transmit signalling */
typedef enum {DAHDI_TXSTATE_ONHOOK,DAHDI_TXSTATE_OFFHOOK,DAHDI_TXSTATE_START,
	DAHDI_TXSTATE_PREWINK,DAHDI_TXSTATE_WINK,DAHDI_TXSTATE_PREFLASH,
	DAHDI_TXSTATE_FLASH,DAHDI_TXSTATE_DEBOUNCE,DAHDI_TXSTATE_AFTERSTART,
	DAHDI_TXSTATE_RINGON,DAHDI_TXSTATE_RINGOFF,DAHDI_TXSTATE_KEWL,
	DAHDI_TXSTATE_AFTERKEWL,DAHDI_TXSTATE_PULSEBREAK,DAHDI_TXSTATE_PULSEMAKE,
	DAHDI_TXSTATE_PULSEAFTER
	} DAHDI_TXSTATE_t;

typedef short sumtype[DAHDI_MAX_CHUNKSIZE];

static sumtype sums[(DAHDI_MAX_CONF + 1) * 3];

/* Translate conference aliases into actual conferences 
   and vice-versa */
static short confalias[DAHDI_MAX_CONF + 1];
static short confrev[DAHDI_MAX_CONF + 1];

static sumtype *conf_sums_next;
static sumtype *conf_sums;
static sumtype *conf_sums_prev;

static struct dahdi_span *master;
static struct file_operations dahdi_fops;
struct file_operations *dahdi_transcode_fops = NULL;

static struct
{
	int	src;	/* source conf number */
	int	dst;	/* dst conf number */
} conf_links[DAHDI_MAX_CONF + 1];


/* There are three sets of conference sum accumulators. One for the current
sample chunk (conf_sums), one for the next sample chunk (conf_sums_next), and
one for the previous sample chunk (conf_sums_prev). The following routine 
(rotate_sums) "rotates" the pointers to these accululator arrays as part
of the events of sample chink processing as follows:

The following sequence is designed to be looked at from the reference point
of the receive routine of the master span.

1. All (real span) receive chunks are processed (with putbuf). The last one
to be processed is the master span. The data received is loaded into the
accumulators for the next chunk (conf_sums_next), to be in alignment with
current data after rotate_sums() is called (which immediately follows).
Keep in mind that putbuf is *also* a transmit routine for the pseudo parts
of channels that are in the REALANDPSEUDO conference mode. These channels
are processed from data in the current sample chunk (conf_sums), being
that this is a "transmit" function (for the pseudo part).

2. rotate_sums() is called.

3. All pseudo channel receive chunks are processed. This data is loaded into
the current sample chunk accumulators (conf_sums).

4. All conference links are processed (being that all receive data for this
chunk has already been processed by now).

5. All pseudo channel transmit chunks are processed. This data is loaded from
the current sample chunk accumulators (conf_sums).

6. All (real span) transmit chunks are processed (with getbuf).  This data is
loaded from the current sample chunk accumulators (conf_sums). Keep in mind
that getbuf is *also* a receive routine for the pseudo part of channels that
are in the REALANDPSEUDO conference mode. These samples are loaded into
the next sample chunk accumulators (conf_sums_next) to be processed as part
of the next sample chunk's data (next time around the world).

*/

#define DIGIT_MODE_DTMF 	0
#define DIGIT_MODE_MFR1		1
#define DIGIT_MODE_PULSE	2
#define DIGIT_MODE_MFR2_FWD	3
#define DIGIT_MODE_MFR2_REV	4

#include "digits.h"

static struct dahdi_dialparams global_dialparams = {
	.dtmf_tonelen = DEFAULT_DTMF_LENGTH,
	.mfv1_tonelen = DEFAULT_MFR1_LENGTH,
	.mfr2_tonelen = DEFAULT_MFR2_LENGTH,
};

static int dahdi_chan_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data, int unit);

#if defined(CONFIG_DAHDI_MMX) || defined(ECHO_CAN_FP)
#define dahdi_kernel_fpu_begin kernel_fpu_begin
#endif	

static struct dahdi_timer {
	int ms;			/* Countdown */
	int pos;		/* Position */
	int ping;		/* Whether we've been ping'd */
	int tripped;	/* Whether we're tripped */
	struct dahdi_timer *next;	/* Linked list */
	wait_queue_head_t sel;
} *zaptimers = NULL;

#ifdef DEFINE_SPINLOCK
static DEFINE_SPINLOCK(zaptimerlock);
static DEFINE_SPINLOCK(bigzaplock);
#else
static spinlock_t zaptimerlock = SPIN_LOCK_UNLOCKED;
static spinlock_t bigzaplock = SPIN_LOCK_UNLOCKED;
#endif

struct dahdi_zone {
	atomic_t refcount;
	char name[40];	/* Informational, only */
	int ringcadence[DAHDI_MAX_CADENCE];
	struct dahdi_tone *tones[DAHDI_TONE_MAX]; 
	/* Each of these is a circular list
	   of dahdi_tones to generate what we
	   want.  Use NULL if the tone is
	   unavailable */
	struct dahdi_tone dtmf[16];		/* DTMF tones for this zone, with desired length */
	struct dahdi_tone dtmf_continuous[16];	/* DTMF tones for this zone, continuous play */
	struct dahdi_tone mfr1[15];		/* MFR1 tones for this zone, with desired length */
	struct dahdi_tone mfr2_fwd[15];		/* MFR2 FWD tones for this zone, with desired length */
	struct dahdi_tone mfr2_rev[15];		/* MFR2 REV tones for this zone, with desired length */
	struct dahdi_tone mfr2_fwd_continuous[16];	/* MFR2 FWD tones for this zone, continuous play */
	struct dahdi_tone mfr2_rev_continuous[16];	/* MFR2 REV tones for this zone, continuous play */
};

static struct dahdi_span *spans[DAHDI_MAX_SPANS];
static struct dahdi_chan *chans[DAHDI_MAX_CHANNELS]; 

static int maxspans = 0;
static int maxchans = 0;
static int maxconfs = 0;
static int maxlinks = 0;

static int default_zone = -1;

short __dahdi_mulaw[256];
short __dahdi_alaw[256];

#ifndef CONFIG_CALC_XLAW
u_char __dahdi_lin2mu[16384];

u_char __dahdi_lin2a[16384];
#endif

static u_char defgain[256];

#ifdef DEFINE_RWLOCK
static DEFINE_RWLOCK(zone_lock);
static DEFINE_RWLOCK(chan_lock);
#else
static rwlock_t zone_lock = RW_LOCK_UNLOCKED;
static rwlock_t chan_lock = RW_LOCK_UNLOCKED;
#endif

static struct dahdi_zone *tone_zones[DAHDI_TONE_ZONE_MAX];

#define NUM_SIGS	10

#ifdef DEFINE_RWLOCK
static DEFINE_RWLOCK(echocan_list_lock);
#else
static rwlock_t echocan_list_lock = RW_LOCK_UNLOCKED;
#endif

LIST_HEAD(echocan_list);

struct echocan {
	const struct dahdi_echocan *ec;
	struct module *owner;
	struct list_head list;
};

int dahdi_register_echocan(const struct dahdi_echocan *ec)
{
	struct echocan *cur;

	write_lock(&echocan_list_lock);

	/* make sure it isn't already registered */
	list_for_each_entry(cur, &echocan_list, list) {
		if (cur->ec == ec) {
			write_unlock(&echocan_list_lock);
			return -EPERM;
		}
	}

	if (!(cur = kmalloc(sizeof(*cur), GFP_KERNEL))) {
		write_unlock(&echocan_list_lock);
		return -ENOMEM;
	}

	memset(cur, 0, sizeof(*cur));

	cur->ec = ec;
	INIT_LIST_HEAD(&cur->list);

	list_add_tail(&cur->list, &echocan_list);

	write_unlock(&echocan_list_lock);

	return 0;
}

void dahdi_unregister_echocan(const struct dahdi_echocan *ec)
{
	struct echocan *cur, *next;

	write_lock(&echocan_list_lock);

	list_for_each_entry_safe(cur, next, &echocan_list, list) {
		if (cur->ec == ec) {
			list_del(&cur->list);
			break;
		}
	}

	write_unlock(&echocan_list_lock);
}

static inline void rotate_sums(void)
{
	/* Rotate where we sum and so forth */
	static int pos = 0;
	conf_sums_prev = sums + (DAHDI_MAX_CONF + 1) * pos;
	conf_sums = sums + (DAHDI_MAX_CONF + 1) * ((pos + 1) % 3);
	conf_sums_next = sums + (DAHDI_MAX_CONF + 1) * ((pos + 2) % 3);
	pos = (pos + 1) % 3;
	memset(conf_sums_next, 0, maxconfs * sizeof(sumtype));
}

  /* return quiescent (idle) signalling states, for the various signalling types */
static int dahdi_q_sig(struct dahdi_chan *chan)
{
	int	x;

	static unsigned int in_sig[NUM_SIGS][2] = {
		{ DAHDI_SIG_NONE, 0},
		{ DAHDI_SIG_EM, 0 | (DAHDI_ABIT << 8)},
		{ DAHDI_SIG_FXSLS,DAHDI_BBIT | (DAHDI_BBIT << 8)},
		{ DAHDI_SIG_FXSGS,DAHDI_ABIT | DAHDI_BBIT | ((DAHDI_ABIT | DAHDI_BBIT) << 8)},
		{ DAHDI_SIG_FXSKS,DAHDI_BBIT | DAHDI_BBIT | ((DAHDI_ABIT | DAHDI_BBIT) << 8)},
		{ DAHDI_SIG_FXOLS,0 | (DAHDI_ABIT << 8)},
		{ DAHDI_SIG_FXOGS,DAHDI_BBIT | ((DAHDI_ABIT | DAHDI_BBIT) << 8)},
		{ DAHDI_SIG_FXOKS,0 | (DAHDI_ABIT << 8)},
		{ DAHDI_SIG_SF, 0},
		{ DAHDI_SIG_EM_E1, DAHDI_DBIT | ((DAHDI_ABIT | DAHDI_DBIT) << 8) },
	} ;
	
	/* must have span to begin with */
	if (!chan->span) return(-1);
	/* if RBS does not apply, return error */
	if (!(chan->span->flags & DAHDI_FLAG_RBS) || 
	    !chan->span->rbsbits) return(-1);
	if (chan->sig == DAHDI_SIG_CAS)
		return chan->idlebits;
	for (x=0;x<NUM_SIGS;x++) {
		if (in_sig[x][0] == chan->sig) return(in_sig[x][1]);
	}	return(-1); /* not found -- error */
}

#ifdef CONFIG_PROC_FS
static char *sigstr(int sig)
{
	switch (sig) {
		case DAHDI_SIG_FXSLS:
			return "FXSLS";
		case DAHDI_SIG_FXSKS:
			return "FXSKS";
		case DAHDI_SIG_FXSGS:
			return "FXSGS";
		case DAHDI_SIG_FXOLS:
			return "FXOLS";
		case DAHDI_SIG_FXOKS:
			return "FXOKS";
		case DAHDI_SIG_FXOGS:
			return "FXOGS";
		case DAHDI_SIG_EM:
			return "E&M";
		case DAHDI_SIG_EM_E1:
			return "E&M-E1";
		case DAHDI_SIG_CLEAR:
			return "Clear";
		case DAHDI_SIG_HDLCRAW:
			return "HDLCRAW";
		case DAHDI_SIG_HDLCFCS:
			return "HDLCFCS";
		case DAHDI_SIG_HDLCNET:
			return "HDLCNET";
		case DAHDI_SIG_HARDHDLC:
			return "Hardware-assisted HDLC";
		case DAHDI_SIG_MTP2:
			return "MTP2";
		case DAHDI_SIG_SLAVE:
			return "Slave";
		case DAHDI_SIG_CAS:
			return "CAS";
		case DAHDI_SIG_DACS:
			return "DACS";
		case DAHDI_SIG_DACS_RBS:
			return "DACS+RBS";
		case DAHDI_SIG_SF:
			return "SF (ToneOnly)";
		case DAHDI_SIG_NONE:
		default:
			return "Unconfigured";
	}

}

static inline int fill_alarm_string(char *buf, int count, int alarms)
{
	int	len = 0;

	if (alarms > 0) {
		if (alarms & DAHDI_ALARM_BLUE)
			len += snprintf(buf + len, count - len, "BLUE ");
		if (alarms & DAHDI_ALARM_YELLOW)
			len += snprintf(buf + len, count - len, "YELLOW ");
		if (alarms & DAHDI_ALARM_RED)
			len += snprintf(buf + len, count - len, "RED ");
		if (alarms & DAHDI_ALARM_LOOPBACK)
			len += snprintf(buf + len, count - len, "LOOP ");
		if (alarms & DAHDI_ALARM_RECOVER)
			len += snprintf(buf + len, count - len, "RECOVERING ");
		if (alarms & DAHDI_ALARM_NOTOPEN)
			len += snprintf(buf + len, count - len, "NOTOPEN ");
	}
	if(len > 0) {
		len--;
		buf[len] = '\0';	/* strip last space */
	}
	return len;
}

static int dahdi_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int x, len = 0;
	long span;

	/* In Linux 2.6, this MUST NOT EXECEED 1024 bytes in one read! */

	span = (long)data;

	if (!span)
		return 0;

	if (spans[span]->name) 
		len += sprintf(page + len, "Span %ld: %s ", span, spans[span]->name);
	if (spans[span]->desc)
		len += sprintf(page + len, "\"%s\"", spans[span]->desc);
	else
		len += sprintf(page + len, "\"\"");

	if(spans[span] == master)
		len += sprintf(page + len, " (MASTER)");

	if (spans[span]->lineconfig) {
		/* framing first */
		if (spans[span]->lineconfig & DAHDI_CONFIG_B8ZS)
			len += sprintf(page + len, " B8ZS/");
		else if (spans[span]->lineconfig & DAHDI_CONFIG_AMI)
			len += sprintf(page + len, " AMI/");
		else if (spans[span]->lineconfig & DAHDI_CONFIG_HDB3)
			len += sprintf(page + len, " HDB3/");
		/* then coding */
		if (spans[span]->lineconfig & DAHDI_CONFIG_ESF)
			len += sprintf(page + len, "ESF");
		else if (spans[span]->lineconfig & DAHDI_CONFIG_D4)
			len += sprintf(page + len, "D4");
		else if (spans[span]->lineconfig & DAHDI_CONFIG_CCS)
			len += sprintf(page + len, "CCS");
		/* E1's can enable CRC checking */
		if (spans[span]->lineconfig & DAHDI_CONFIG_CRC4)
			len += sprintf(page + len, "/CRC4");
	}

	len += sprintf(page + len, " ");

	/* list alarms */
	len += fill_alarm_string(page + len, count - len, spans[span]->alarms);
	if (spans[span]->syncsrc && (spans[span]->syncsrc == spans[span]->spanno))
		len += sprintf(page + len, "ClockSource ");
	len += sprintf(page + len, "\n");
	if (spans[span]->bpvcount)
		len += sprintf(page + len, "\tBPV count: %d\n", spans[span]->bpvcount);
	if (spans[span]->crc4count)
		len += sprintf(page + len, "\tCRC4 error count: %d\n", spans[span]->crc4count);
	if (spans[span]->ebitcount)
		len += sprintf(page + len, "\tE-bit error count: %d\n", spans[span]->ebitcount);
	if (spans[span]->fascount)
		len += sprintf(page + len, "\tFAS error count: %d\n", spans[span]->fascount);
	if (spans[span]->irqmisses)
		len += sprintf(page + len, "\tIRQ misses: %d\n", spans[span]->irqmisses);
	if (spans[span]->timingslips)
		len += sprintf(page + len, "\tTiming slips: %d\n", spans[span]->timingslips);
	len += sprintf(page + len, "\n");


        for (x=1;x<DAHDI_MAX_CHANNELS;x++) {	
		if (chans[x]) {
			if (chans[x]->span && (chans[x]->span->spanno == span)) {
				if (chans[x]->name)
					len += sprintf(page + len, "\t%4d %s ", x, chans[x]->name);
				if (chans[x]->sig) {
					if (chans[x]->sig == DAHDI_SIG_SLAVE)
						len += sprintf(page + len, "%s ", sigstr(chans[x]->master->sig));
					else {
						len += sprintf(page + len, "%s ", sigstr(chans[x]->sig));
						if (chans[x]->nextslave && chans[x]->master->channo == x)
							len += sprintf(page + len, "Master ");
					}
				}
				if (test_bit(DAHDI_FLAGBIT_OPEN, &chans[x]->flags)) {
					len += sprintf(page + len, "(In use) ");
				}
#ifdef	OPTIMIZE_CHANMUTE
				if (chans[x]->chanmute) {
					len += sprintf(page + len, "(no pcm) ");
				}
#endif
				len += fill_alarm_string(page + len, count - len, chans[x]->chan_alarms);

				if (chans[x]->ec_factory) {
					len += sprintf(page + len, " (EC: %s) ", chans[x]->ec_factory->name);
				}

				len += sprintf(page + len, "\n");
			}
			if (len <= off) { /* If everything printed so far is before beginning of request */
				off -= len;
				len = 0;
			}
			if (len > off+count) /* stop if we've already generated enough */
				break;
		}
	}
	if (len <= off) { /* If everything printed so far is before beginning of request */
		off -= len;
		len = 0;
	}
	*start = page + off;
	len -= off;     /* un-count any remaining offset */
	if (len > count) len = count;   /* don't return bytes not asked for */
	return len;
}
#endif

static int dahdi_first_empty_alias(void)
{
	/* Find the first conference which has no alias pointing to it */
	int x;
	for (x=1;x<DAHDI_MAX_CONF;x++) {
		if (!confrev[x])
			return x;
	}
	return -1;
}

static void recalc_maxconfs(void)
{
	int x;
	for (x=DAHDI_MAX_CONF-1;x>0;x--) {
		if (confrev[x]) {
			maxconfs = x+1;
			return;
		}
	}
	maxconfs = 0;
}

static void recalc_maxlinks(void)
{
	int x;
	for (x=DAHDI_MAX_CONF-1;x>0;x--) {
		if (conf_links[x].src || conf_links[x].dst) {
			maxlinks = x+1;
			return;
		}
	}
	maxlinks = 0;
}

static int dahdi_first_empty_conference(void)
{
	/* Find the first conference which has no alias */
	int x;
	for (x=DAHDI_MAX_CONF-1;x>0;x--) {
		if (!confalias[x])
			return x;
	}
	return -1;
}

static int dahdi_get_conf_alias(int x)
{
	int a;
	if (confalias[x]) {
		return confalias[x];
	}

	/* Allocate an alias */
	a = dahdi_first_empty_alias();
	confalias[x] = a;
	confrev[a] = x;

	/* Highest conference may have changed */
	recalc_maxconfs();
	return a;
}

static void dahdi_check_conf(int x)
{
	int y;

	/* return if no valid conf number */
	if (x <= 0) return;
	/* Return if there is no alias */
	if (!confalias[x])
		return;
	for (y=0;y<maxchans;y++) {
		if (chans[y] && (chans[y]->confna == x) &&
			((chans[y]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONF ||
			(chans[y]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFANN ||
			(chans[y]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFMON ||
			(chans[y]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFANNMON ||
			(chans[y]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_REALANDPSEUDO))
			return;
	}
	/* If we get here, nobody is in the conference anymore.  Clear it out
	   both forward and reverse */
	confrev[confalias[x]] = 0;
	confalias[x] = 0;

	/* Highest conference may have changed */
	recalc_maxconfs();
}

/* enqueue an event on a channel */
static void __qevent(struct dahdi_chan *chan, int event)
{

	  /* if full, ignore */
	if ((chan->eventoutidx == 0) && (chan->eventinidx == (DAHDI_MAX_EVENTSIZE - 1))) 
		return;
	  /* if full, ignore */
	if (chan->eventinidx == (chan->eventoutidx - 1)) return;
	  /* save the event */
	chan->eventbuf[chan->eventinidx++] = event;
	  /* wrap the index, if necessary */
	if (chan->eventinidx >= DAHDI_MAX_EVENTSIZE) chan->eventinidx = 0;
	  /* wake em all up */
	if (chan->iomask & DAHDI_IOMUX_SIGEVENT) wake_up_interruptible(&chan->eventbufq);
	wake_up_interruptible(&chan->readbufq);
	wake_up_interruptible(&chan->writebufq);
	wake_up_interruptible(&chan->sel);
	return;
}

void dahdi_qevent_nolock(struct dahdi_chan *chan, int event)
{
	__qevent(chan, event);
}

void dahdi_qevent_lock(struct dahdi_chan *chan, int event)
{
	unsigned long flags;
	spin_lock_irqsave(&chan->lock, flags);
	__qevent(chan, event);
	spin_unlock_irqrestore(&chan->lock, flags);
}

/* sleep in user space until woken up. Equivilant of tsleep() in BSD */
static int schluffen(wait_queue_head_t *q)
{
	DECLARE_WAITQUEUE(wait, current);
	add_wait_queue(q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	if (!signal_pending(current)) schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(q, &wait);
	if (signal_pending(current)) {
		return -ERESTARTSYS;
	}
	return(0);
}

static inline void calc_fcs(struct dahdi_chan *ss, int inwritebuf)
{
	int x;
	unsigned int fcs=PPP_INITFCS;
	unsigned char *data = ss->writebuf[inwritebuf];
	int len = ss->writen[inwritebuf];
	/* Not enough space to do FCS calculation */
	if (len < 2)
		return;
	for (x=0;x<len-2;x++)
		fcs = PPP_FCS(fcs, data[x]);
	fcs ^= 0xffff;
	/* Send out the FCS */
	data[len-2] = (fcs & 0xff);
	data[len-1] = (fcs >> 8) & 0xff;
}

static int dahdi_reallocbufs(struct dahdi_chan *ss, int j, int numbufs)
{
	unsigned char *newbuf, *oldbuf;
	unsigned long flags;
	int x;
	/* Check numbufs */
	if (numbufs < 2)
		numbufs = 2;
	if (numbufs > DAHDI_MAX_NUM_BUFS)
		numbufs = DAHDI_MAX_NUM_BUFS;
	/* We need to allocate our buffers now */
	if (j) {
		newbuf = kmalloc(j * 2 * numbufs, GFP_KERNEL);
		if (!newbuf) 
			return (-ENOMEM);
		memset(newbuf, 0, j * 2 * numbufs);
	} else
		newbuf = NULL;
	  /* Now that we've allocated our new buffer, we can safely
	     move things around... */
	spin_lock_irqsave(&ss->lock, flags);
	ss->blocksize = j; /* set the blocksize */
	oldbuf = ss->readbuf[0]; /* Keep track of the old buffer */
	ss->readbuf[0] = NULL;
	if (newbuf) {
		for (x=0;x<numbufs;x++) {
			ss->readbuf[x] = newbuf + x * j;
			ss->writebuf[x] = newbuf + (numbufs + x) * j;
		}
	} else {
		for (x=0;x<numbufs;x++) {
			ss->readbuf[x] = NULL;
			ss->writebuf[x] = NULL;
		}
	}
	/* Mark all buffers as empty */
	for (x=0;x<numbufs;x++) 
		ss->writen[x] = 
		ss->writeidx[x]=
		ss->readn[x]=
		ss->readidx[x] = 0;
	
	/* Keep track of where our data goes (if it goes
	   anywhere at all) */
	if (newbuf) {
		ss->inreadbuf = 0;
		ss->inwritebuf = 0;
	} else {
		ss->inreadbuf = -1;
		ss->inwritebuf = -1;
	}
	ss->outreadbuf = -1;
	ss->outwritebuf = -1;
	ss->numbufs = numbufs;
	if (ss->txbufpolicy == DAHDI_POLICY_WHEN_FULL)
		ss->txdisable = 1;
	else
		ss->txdisable = 0;

	if (ss->rxbufpolicy == DAHDI_POLICY_WHEN_FULL)
		ss->rxdisable = 1;
	else
		ss->rxdisable = 0;

	spin_unlock_irqrestore(&ss->lock, flags);
	if (oldbuf)
		kfree(oldbuf);
	return 0;
}

static int dahdi_hangup(struct dahdi_chan *chan);
static void dahdi_set_law(struct dahdi_chan *chan, int law);

/* Pull a DAHDI_CHUNKSIZE piece off the queue.  Returns
   0 on success or -1 on failure.  If failed, provides
   silence */
static int __buf_pull(struct confq *q, u_char *data, struct dahdi_chan *c, char *label)
{
	int oldoutbuf = q->outbuf;
	/* Ain't nuffin to read */
	if (q->outbuf < 0) {
		if (data)
			memset(data, DAHDI_LIN2X(0,c), DAHDI_CHUNKSIZE);
		return -1;
	}
	if (data)
		memcpy(data, q->buf[q->outbuf], DAHDI_CHUNKSIZE);
	q->outbuf = (q->outbuf + 1) % DAHDI_CB_SIZE;

	/* Won't be nuffin next time */
	if (q->outbuf == q->inbuf) {
		q->outbuf = -1;
	}

	/* If they thought there was no space then
	   there is now where we just read */
	if (q->inbuf < 0) 
		q->inbuf = oldoutbuf;
	return 0;
}

/* Returns a place to put stuff, or NULL if there is
   no room */

static u_char *__buf_pushpeek(struct confq *q)
{
	if (q->inbuf < 0)
		return NULL;
	return q->buf[q->inbuf];
}

static u_char *__buf_peek(struct confq *q)
{
	if (q->outbuf < 0)
		return NULL;
	return q->buf[q->outbuf];
}

#ifdef BUF_MUNGE
static u_char *__buf_cpush(struct confq *q)
{
	int pos;
	/* If we have no space, return where the
	   last space that we *did* have was */
	if (q->inbuf > -1)
		return NULL;
	pos = q->outbuf - 1;
	if (pos < 0)
		pos += DAHDI_CB_SIZE;
	return q->buf[pos];
}

static void __buf_munge(struct dahdi_chan *chan, u_char *old, u_char *new)
{
	/* Run a weighted average of the old and new, in order to
	   mask a missing sample */
	int x;
	int val;
	for (x=0;x<DAHDI_CHUNKSIZE;x++) {
		val = x * DAHDI_XLAW(new[x], chan) + (DAHDI_CHUNKSIZE - x - 1) * DAHDI_XLAW(old[x], chan);
		val = val / (DAHDI_CHUNKSIZE - 1);
		old[x] = DAHDI_LIN2X(val, chan);
	}
}
#endif
/* Push something onto the queue, or assume what
   is there is valid if data is NULL */
static int __buf_push(struct confq *q, u_char *data, char *label)
{
	int oldinbuf = q->inbuf;
	if (q->inbuf < 0) {
		return -1;
	}
	if (data)
		/* Copy in the data */
		memcpy(q->buf[q->inbuf], data, DAHDI_CHUNKSIZE);

	/* Advance the inbuf pointer */
	q->inbuf = (q->inbuf + 1) % DAHDI_CB_SIZE;

	if (q->inbuf == q->outbuf) {
		/* No space anymore... */	
		q->inbuf = -1;
	}
	/* If they don't think data is ready, let
	   them know it is now */
	if (q->outbuf < 0) {
		q->outbuf = oldinbuf;
	}
	return 0;
}

static void reset_conf(struct dahdi_chan *chan)
{
	int x;
	/* Empty out buffers and reset to initialization */
	for (x=0;x<DAHDI_CB_SIZE;x++)
		chan->confin.buf[x] = chan->confin.buffer + DAHDI_CHUNKSIZE * x;
	chan->confin.inbuf = 0;
	chan->confin.outbuf = -1;

	for (x=0;x<DAHDI_CB_SIZE;x++)
		chan->confout.buf[x] = chan->confout.buffer + DAHDI_CHUNKSIZE * x;
	chan->confout.inbuf = 0;
	chan->confout.outbuf = -1;
}


static inline int hw_echocancel_off(struct dahdi_chan *chan)
{
	struct dahdi_echocanparams ecp;
	
	int ret = -ENODEV;
	if (chan->span) {
		if (chan->span->echocan) {
			ret = chan->span->echocan(chan, 0);
		} else if (chan->span->echocan_with_params) {
			memset(&ecp, 0, sizeof(ecp));  /* Sets tap length to 0 */
			ret = chan->span->echocan_with_params(chan, &ecp, NULL);
		}
	}
	return ret;
}

static const struct dahdi_echocan *find_echocan(const char *name)
{
	struct echocan *cur;
	char name_upper[strlen(name) + 1];
	char *c;
	const char *d;
	char modname_buf[128] = "dahdi_echocan_";
	unsigned int tried_once = 0;
	int res;

	for (c = name_upper, d = name; *d; c++, d++) {
		*c = toupper(*d);
	}

	*c = '\0';

retry:
	read_lock(&echocan_list_lock);

	list_for_each_entry(cur, &echocan_list, list) {
		if (!strcmp(name_upper, cur->ec->name)) {
			if ((res = try_module_get(cur->owner))) {
				read_unlock(&echocan_list_lock);
				return cur->ec;
			} else {
				read_unlock(&echocan_list_lock);
				return NULL;
			}
		}
	}

	read_unlock(&echocan_list_lock);

	if (tried_once) {
		return NULL;
	}

	/* couldn't find it, let's try to load it */

	for (c = &modname_buf[strlen(modname_buf)], d = name; *d; c++, d++) {
		*c = tolower(*d);
	}

	request_module(modname_buf);

	tried_once = 1;

	/* and try one more time */
	goto retry;
}

static void release_echocan(const struct dahdi_echocan *ec)
{
	module_put(ec->owner);
}

static void close_channel(struct dahdi_chan *chan)
{
	unsigned long flags;
	void *rxgain = NULL;
	struct echo_can_state *ec_state;
	const struct dahdi_echocan *ec_current;
	int oldconf;
	short *readchunkpreec;
#ifdef CONFIG_DAHDI_PPP
	struct ppp_channel *ppp;
#endif

	/* XXX Buffers should be send out before reallocation!!! XXX */
	if (!(chan->flags & DAHDI_FLAG_NOSTDTXRX))
		dahdi_reallocbufs(chan, 0, 0); 
	spin_lock_irqsave(&chan->lock, flags);
#ifdef CONFIG_DAHDI_PPP
	ppp = chan->ppp;
	chan->ppp = NULL;
#endif
	ec_state = chan->ec_state;
	chan->ec_state = NULL;
	ec_current = chan->ec_current;
	chan->ec_current = NULL;
	readchunkpreec = chan->readchunkpreec;
	chan->readchunkpreec = NULL;
	chan->curtone = NULL;
	if (chan->curzone)
		atomic_dec(&chan->curzone->refcount);
	chan->curzone = NULL;
	chan->cadencepos = 0;
	chan->pdialcount = 0;
	dahdi_hangup(chan); 
	chan->itimerset = chan->itimer = 0;
	chan->pulsecount = 0;
	chan->pulsetimer = 0;
	chan->ringdebtimer = 0;
	init_waitqueue_head(&chan->sel);
	init_waitqueue_head(&chan->readbufq);
	init_waitqueue_head(&chan->writebufq);
	init_waitqueue_head(&chan->eventbufq);
	init_waitqueue_head(&chan->txstateq);
	chan->txdialbuf[0] = '\0';
	chan->digitmode = DIGIT_MODE_DTMF;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;
	  /* initialize IO MUX mask */
	chan->iomask = 0;
	/* save old conf number, if any */
	oldconf = chan->confna;
	  /* initialize conference variables */
	chan->_confn = 0;
	if ((chan->sig & __DAHDI_SIG_DACS) != __DAHDI_SIG_DACS) {
		chan->confna = 0;
		chan->confmode = 0;
	}
	chan->confmute = 0;
	/* release conference resource, if any to release */
	if (oldconf) dahdi_check_conf(oldconf);
	chan->gotgs = 0;
	reset_conf(chan);
	
	if (chan->gainalloc && chan->rxgain)
		rxgain = chan->rxgain;

	chan->rxgain = defgain;
	chan->txgain = defgain;
	chan->gainalloc = 0;
	chan->eventinidx = chan->eventoutidx = 0;
	chan->flags &= ~(DAHDI_FLAG_LOOPED | DAHDI_FLAG_LINEAR | DAHDI_FLAG_PPP | DAHDI_FLAG_SIGFREEZE);

	dahdi_set_law(chan,0);

	memset(chan->conflast, 0, sizeof(chan->conflast));
	memset(chan->conflast1, 0, sizeof(chan->conflast1));
	memset(chan->conflast2, 0, sizeof(chan->conflast2));

	if (chan->span && chan->span->dacs && oldconf)
		chan->span->dacs(chan, NULL);

	if (ec_state) {
		ec_current->echo_can_free(ec_state);
		release_echocan(ec_current);
	}

	spin_unlock_irqrestore(&chan->lock, flags);

	hw_echocancel_off(chan);

	if (rxgain)
		kfree(rxgain);
	if (readchunkpreec)
		kfree(readchunkpreec);

#ifdef CONFIG_DAHDI_PPP
	if (ppp) {
		tasklet_kill(&chan->ppp_calls);
		skb_queue_purge(&chan->ppp_rq);
		ppp_unregister_channel(ppp);
		kfree(ppp);
	}
#endif

}

static int free_tone_zone(int num)
{
	struct dahdi_zone *z;

	if ((num >= DAHDI_TONE_ZONE_MAX) || (num < 0))
		return -EINVAL;

	write_lock(&zone_lock);
	z = tone_zones[num];
	tone_zones[num] = NULL;
	write_unlock(&zone_lock);
	if (!z)
		return 0;

	if (atomic_read(&z->refcount)) {
		/* channels are still using this zone so put it back */
		write_lock(&zone_lock);
		tone_zones[num] = z;
		write_unlock(&zone_lock);

		return -EBUSY;
	} else {
		kfree(z);

		return 0;
	}
}

static int dahdi_register_tone_zone(int num, struct dahdi_zone *zone)
{
	int res = 0;

	if ((num >= DAHDI_TONE_ZONE_MAX) || (num < 0))
		return -EINVAL;

	write_lock(&zone_lock);
	if (tone_zones[num]) {
		res = -EINVAL;
	} else {
		res = 0;
		tone_zones[num] = zone;
	}
	write_unlock(&zone_lock);

	if (!res)
		module_printk(KERN_INFO, "Registered tone zone %d (%s)\n", num, zone->name);

	return res;
}

static int start_tone(struct dahdi_chan *chan, int tone)
{
	int res = -EINVAL;

	/* Stop the current tone, no matter what */
	chan->tonep = 0;
	chan->curtone = NULL;
	chan->pdialcount = 0;
	chan->txdialbuf[0] = '\0';
	chan->dialing = 0;

	if (tone == -1) {
		/* Just stop the current tone */
		res = 0;
	} else if (!chan->curzone) {
		static int __warnonce = 1;
		if (__warnonce) {
			__warnonce = 0;
			/* The tonezones are loaded by ztcfg based on /etc/dahdi.conf. */
			module_printk(KERN_WARNING, "DAHDI: Cannot start tones until tone zone is loaded.\n");
		}
		/* Note that no tone zone exists at the moment */
		res = -ENODATA;
	} else if ((tone >= 0 && tone <= DAHDI_TONE_MAX)) {
		/* Have a tone zone */
		if (chan->curzone->tones[tone]) {
			chan->curtone = chan->curzone->tones[tone];
			res = 0;
		} else { /* Indicate that zone is loaded but no such tone exists */
			res = -ENOSYS;
		}
	} else if (chan->digitmode == DIGIT_MODE_DTMF) {
		if ((tone >= DAHDI_TONE_DTMF_BASE) && (tone <= DAHDI_TONE_DTMF_MAX)) {
			chan->dialing = 1;
			res = 0;
			tone -= DAHDI_TONE_DTMF_BASE;
			if (chan->curzone) {
				/* Have a tone zone */
				if (chan->curzone->dtmf_continuous[tone].tonesamples) {
					chan->curtone = &chan->curzone->dtmf_continuous[tone];
					res = 0;
				} else {
					/* Indicate that zone is loaded but no such tone exists */
					res = -ENOSYS;
				}
			} else {
				/* Note that no tone zone exists at the moment */
				res = -ENODATA;
			}
		} else {
			res = -EINVAL;
		}
	} else if (chan->digitmode == DIGIT_MODE_MFR2_FWD) {
		if ((tone >= DAHDI_TONE_MFR2_FWD_BASE) && (tone <= DAHDI_TONE_MFR2_FWD_MAX)) {
			res = 0;
			tone -= DAHDI_TONE_MFR2_FWD_BASE;
			if (chan->curzone) {
				/* Have a tone zone */
				if (chan->curzone->mfr2_fwd_continuous[tone].tonesamples) {
					chan->curtone = &chan->curzone->mfr2_fwd_continuous[tone];
					res = 0;
				} else {
					/* Indicate that zone is loaded but no such tone exists */
					res = -ENOSYS;
				}
			} else {
				/* Note that no tone zone exists at the moment */
				res = -ENODATA;
			}
		} else {
			res = -EINVAL;
		}
	} else if (chan->digitmode == DIGIT_MODE_MFR2_REV) {
		if ((tone >= DAHDI_TONE_MFR2_REV_BASE) && (tone <= DAHDI_TONE_MFR2_REV_MAX)) {
			res = 0;
			tone -= DAHDI_TONE_MFR2_REV_BASE;
			if (chan->curzone) {
				/* Have a tone zone */
				if (chan->curzone->mfr2_rev_continuous[tone].tonesamples) {
					chan->curtone = &chan->curzone->mfr2_rev_continuous[tone];
					res = 0;
				} else {
					/* Indicate that zone is loaded but no such tone exists */
					res = -ENOSYS;
				}
			} else {
				/* Note that no tone zone exists at the moment */
				res = -ENODATA;
			}
		} else {
			res = -EINVAL;
		}
	} else {
		chan->dialing = 0;
		res = -EINVAL;
	}

	if (chan->curtone)
		dahdi_init_tone_state(&chan->ts, chan->curtone);
	
	return res;
}

static int set_tone_zone(struct dahdi_chan *chan, int zone)
{
	int res = 0;
	struct dahdi_zone *z;
	unsigned long flags;

	/* Do not call with the channel locked. */

	if (zone == -1)
		zone = default_zone;

	if ((zone >= DAHDI_TONE_ZONE_MAX) || (zone < 0))
		return -EINVAL;

	read_lock(&zone_lock);

	if ((z = tone_zones[zone])) {
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->curzone)
			atomic_dec(&chan->curzone->refcount);

		atomic_inc(&z->refcount);
		chan->curzone = z;
		chan->tonezone = zone;
		memcpy(chan->ringcadence, z->ringcadence, sizeof(chan->ringcadence));
		spin_unlock_irqrestore(&chan->lock, flags);
	} else {
		res = -ENODATA;
	}

	read_unlock(&zone_lock);

	return res;
}

static void dahdi_set_law(struct dahdi_chan *chan, int law)
{
	if (!law) {
		if (chan->deflaw)
			law = chan->deflaw;
		else
			if (chan->span) law = chan->span->deflaw;
			else law = DAHDI_LAW_MULAW;
	}
	if (law == DAHDI_LAW_ALAW) {
		chan->xlaw = __dahdi_alaw;
#ifdef CONFIG_CALC_XLAW
		chan->lineartoxlaw = __dahdi_lineartoalaw;
#else
		chan->lin2x = __dahdi_lin2a;
#endif
	} else {
		chan->xlaw = __dahdi_mulaw;
#ifdef CONFIG_CALC_XLAW
		chan->lineartoxlaw = __dahdi_lineartoulaw;
#else
		chan->lin2x = __dahdi_lin2mu;
#endif
	}
}

static int dahdi_chan_reg(struct dahdi_chan *chan)
{
	int x;
	int res=0;
	unsigned long flags;
	
	write_lock_irqsave(&chan_lock, flags);
	for (x=1;x<DAHDI_MAX_CHANNELS;x++) {
		if (!chans[x]) {
			spin_lock_init(&chan->lock);
			chans[x] = chan;
			if (maxchans < x + 1)
				maxchans = x + 1;
			chan->channo = x;
			if (!chan->master)
				chan->master = chan;
			if (!chan->readchunk)
				chan->readchunk = chan->sreadchunk;
			if (!chan->writechunk)
				chan->writechunk = chan->swritechunk;
			dahdi_set_law(chan, 0);
			close_channel(chan); 
			/* set this AFTER running close_channel() so that
				HDLC channels wont cause hangage */
			chan->flags |= DAHDI_FLAG_REGISTERED;
			res = 0;
			break;
		}
	}
	write_unlock_irqrestore(&chan_lock, flags);	
	if (x >= DAHDI_MAX_CHANNELS)
		module_printk(KERN_ERR, "No more channels available\n");
	return res;
}

char *dahdi_lboname(int x)
{
	if ((x < 0) || ( x > 7))
		return "Unknown";
	return dahdi_txlevelnames[x];
}

#if defined(CONFIG_DAHDI_NET) || defined(CONFIG_DAHDI_PPP)
#endif

#ifdef CONFIG_DAHDI_NET
#ifdef NEW_HDLC_INTERFACE
static int dahdi_net_open(struct net_device *dev)
{
	int res = hdlc_open(dev);
	struct dahdi_chan *ms = dev_to_ztchan(dev);
                                                                                                                            
/*	if (!dev->hard_start_xmit) return res; is this really necessary? --byg */
	if (res) /* this is necessary to avoid kernel panic when UNSPEC link encap, proven --byg */
		return res;
#else
static int dahdi_net_open(hdlc_device *hdlc)
{
	struct dahdi_chan *ms = hdlc_to_ztchan(hdlc);
	int res;
#endif
	if (!ms) {
		module_printk(KERN_NOTICE, "dahdi_net_open: nothing??\n");
		return -EINVAL;
	}
	if (test_bit(DAHDI_FLAGBIT_OPEN, &ms->flags)) {
		module_printk(KERN_NOTICE, "%s is already open!\n", ms->name);
		return -EBUSY;
	}
	if (!(ms->flags & DAHDI_FLAG_NETDEV)) {
		module_printk(KERN_NOTICE, "%s is not a net device!\n", ms->name);
		return -EINVAL;
	}
	ms->txbufpolicy = DAHDI_POLICY_IMMEDIATE;
	ms->rxbufpolicy = DAHDI_POLICY_IMMEDIATE;

	res = dahdi_reallocbufs(ms, DAHDI_DEFAULT_MTU_MRU, DAHDI_DEFAULT_NUM_BUFS);
	if (res) 
		return res;

	fasthdlc_init(&ms->rxhdlc);
	fasthdlc_init(&ms->txhdlc);
	ms->infcs = PPP_INITFCS;

	netif_start_queue(ztchan_to_dev(ms));

#ifdef CONFIG_DAHDI_DEBUG
	module_printk(KERN_NOTICE, "DAHDINET: Opened channel %d name %s\n", ms->channo, ms->name);
#endif
	return 0;
}

static int dahdi_register_hdlc_device(struct net_device *dev, const char *dev_name)
{
	int result;

	if (dev_name && *dev_name) {
		if ((result = dev_alloc_name(dev, dev_name)) < 0)
			return result;
	}
	result = register_netdev(dev);
	if (result != 0)
		return -EIO;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,14)
	if (netif_carrier_ok(dev))
		netif_carrier_off(dev); /* no carrier until DCD goes up */
#endif
	return 0;
}

#ifdef NEW_HDLC_INTERFACE
static int dahdi_net_stop(struct net_device *dev)
{
    hdlc_device *h = dev_to_hdlc(dev);
    struct dahdi_hdlc *hdlc = h->priv;

#else
static void dahdi_net_close(hdlc_device *hdlc)
{
#endif
	struct dahdi_chan *ms = hdlc_to_ztchan(hdlc);
	if (!ms) {
#ifdef NEW_HDLC_INTERFACE
		module_printk(KERN_NOTICE, "dahdi_net_stop: nothing??\n");
		return 0;
#else
		module_printk(KERN_NOTICE, "dahdi_net_close: nothing??\n");
		return;
#endif
	}
	if (!(ms->flags & DAHDI_FLAG_NETDEV)) {
#ifdef NEW_HDLC_INTERFACE
		module_printk(KERN_NOTICE, "dahdi_net_stop: %s is not a net device!\n", ms->name);
		return 0;
#else
		module_printk(KERN_NOTICE, "dahdi_net_close: %s is not a net device!\n", ms->name);
		return;
#endif
	}
	/* Not much to do here.  Just deallocate the buffers */
        netif_stop_queue(ztchan_to_dev(ms));
	dahdi_reallocbufs(ms, 0, 0);
	hdlc_close(dev);
#ifdef NEW_HDLC_INTERFACE
	return 0;
#else
	return;
#endif
}

#ifdef NEW_HDLC_INTERFACE
/* kernel 2.4.20+ has introduced attach function, dunno what to do,
 just copy sources from dscc4 to be sure and ready for further mastering,
 NOOP right now (i.e. really a stub)  --byg */
static int dahdi_net_attach(struct net_device *dev, unsigned short encoding,
        unsigned short parity)
{
/*        struct net_device *dev = hdlc_to_dev(hdlc);
        struct dscc4_dev_priv *dpriv = dscc4_priv(dev);

        if (encoding != ENCODING_NRZ &&
            encoding != ENCODING_NRZI &&
            encoding != ENCODING_FM_MARK &&
            encoding != ENCODING_FM_SPACE &&
            encoding != ENCODING_MANCHESTER)
                return -EINVAL;

        if (parity != PARITY_NONE &&
            parity != PARITY_CRC16_PR0_CCITT &&
            parity != PARITY_CRC16_PR1_CCITT &&
            parity != PARITY_CRC32_PR0_CCITT &&
            parity != PARITY_CRC32_PR1_CCITT)
                return -EINVAL;

        dpriv->encoding = encoding;
        dpriv->parity = parity;*/
        return 0;
}
#endif
																								 
static struct dahdi_hdlc *dahdi_hdlc_alloc(void)
{
	struct dahdi_hdlc *tmp;
	tmp = kmalloc(sizeof(struct dahdi_hdlc), GFP_KERNEL);
	if (tmp) {
		memset(tmp, 0, sizeof(struct dahdi_hdlc));
	}
	return tmp;
}

#ifdef NEW_HDLC_INTERFACE
static int dahdi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	/* FIXME: this construction seems to be not very optimal for me but I could find nothing better at the moment (Friday, 10PM :( )  --byg */
/*	struct dahdi_chan *ss = hdlc_to_ztchan(list_entry(dev, struct dahdi_hdlc, netdev.netdev));*/
	struct dahdi_chan *ss = dev_to_ztchan(dev);
	struct net_device_stats *stats = hdlc_stats(dev);

#else
static int dahdi_xmit(hdlc_device *hdlc, struct sk_buff *skb)
{
	struct dahdi_chan *ss = hdlc_to_ztchan(hdlc);
	struct net_device *dev = &ss->hdlcnetdev->netdev.netdev;
	struct net_device_stats *stats = &ss->hdlcnetdev->netdev.stats;
#endif
	int retval = 1;
	int x,oldbuf;
	unsigned int fcs;
	unsigned char *data;
	unsigned long flags;
	/* See if we have any buffers */
	spin_lock_irqsave(&ss->lock, flags);
	if (skb->len > ss->blocksize - 2) {
		module_printk(KERN_ERR, "dahdi_xmit(%s): skb is too large (%d > %d)\n", dev->name, skb->len, ss->blocksize -2);
		stats->tx_dropped++;
		retval = 0;
	} else if (ss->inwritebuf >= 0) {
		/* We have a place to put this packet */
		/* XXX We should keep the SKB and avoid the memcpy XXX */
		data = ss->writebuf[ss->inwritebuf];
		memcpy(data, skb->data, skb->len);
		ss->writen[ss->inwritebuf] = skb->len;
		ss->writeidx[ss->inwritebuf] = 0;
		/* Calculate the FCS */
		fcs = PPP_INITFCS;
		for (x=0;x<skb->len;x++)
			fcs = PPP_FCS(fcs, data[x]);
		/* Invert it */
		fcs ^= 0xffff;
		/* Send it out LSB first */
		data[ss->writen[ss->inwritebuf]++] = (fcs & 0xff);
		data[ss->writen[ss->inwritebuf]++] = (fcs >> 8) & 0xff;
		/* Advance to next window */
		oldbuf = ss->inwritebuf;
		ss->inwritebuf = (ss->inwritebuf + 1) % ss->numbufs;

		if (ss->inwritebuf == ss->outwritebuf) {
			/* Whoops, no more space.  */
		    ss->inwritebuf = -1;

		    netif_stop_queue(ztchan_to_dev(ss));
		}
		if (ss->outwritebuf < 0) {
			/* Let the interrupt handler know there's
			   some space for us */
			ss->outwritebuf = oldbuf;
		}
		dev->trans_start = jiffies;
		stats->tx_packets++;
		stats->tx_bytes += ss->writen[oldbuf];
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "Buffered %d bytes to go out in buffer %d\n", ss->writen[oldbuf], oldbuf);
		for (x=0;x<ss->writen[oldbuf];x++)
		     printk("%02x ", ss->writebuf[oldbuf][x]);
		printk("\n");
#endif
		retval = 0;
		/* Free the SKB */
		dev_kfree_skb_any(skb);
	}
	spin_unlock_irqrestore(&ss->lock, flags);
	return retval;
}

#ifdef NEW_HDLC_INTERFACE
static int dahdi_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return hdlc_ioctl(dev, ifr, cmd);
}
#else
static int dahdi_net_ioctl(hdlc_device *hdlc, struct ifreq *ifr, int cmd)
{
	return -EIO;
}
#endif

#endif

#ifdef CONFIG_DAHDI_PPP

static int dahdi_ppp_xmit(struct ppp_channel *ppp, struct sk_buff *skb)
{

	/* 
	 * If we can't handle the packet right now, return 0.  If we
	 * we handle or drop it, return 1.  Always free if we return
	 * 1 and never if we return 0
         */
	struct dahdi_chan *ss = ppp->private;
	int x,oldbuf;
	unsigned int fcs;
	unsigned char *data;
	long flags;
	int retval = 0;

	/* See if we have any buffers */
	spin_lock_irqsave(&ss->lock, flags);
	if (!(test_bit(DAHDI_FLAGBIT_OPEN, &ss->flags))) {
		module_printk(KERN_ERR, "Can't transmit on closed channel\n");
		retval = 1;
	} else if (skb->len > ss->blocksize - 4) {
		module_printk(KERN_ERR, "dahdi_ppp_xmit(%s): skb is too large (%d > %d)\n", ss->name, skb->len, ss->blocksize -2);
		retval = 1;
	} else if (ss->inwritebuf >= 0) {
		/* We have a place to put this packet */
		/* XXX We should keep the SKB and avoid the memcpy XXX */
		data = ss->writebuf[ss->inwritebuf];
		/* Start with header of two bytes */
		/* Add "ALL STATIONS" and "UNNUMBERED" */
		data[0] = 0xff;
		data[1] = 0x03;
		ss->writen[ss->inwritebuf] = 2;

		/* Copy real data and increment amount written */
		memcpy(data + 2, skb->data, skb->len);

		ss->writen[ss->inwritebuf] += skb->len;

		/* Re-set index back to zero */
		ss->writeidx[ss->inwritebuf] = 0;

		/* Calculate the FCS */
		fcs = PPP_INITFCS;
		for (x=0;x<skb->len + 2;x++)
			fcs = PPP_FCS(fcs, data[x]);
		/* Invert it */
		fcs ^= 0xffff;

		/* Point past the real data now */
		data += (skb->len + 2);

		/* Send FCS out LSB first */
		data[0] = (fcs & 0xff);
		data[1] = (fcs >> 8) & 0xff;

		/* Account for FCS length */
		ss->writen[ss->inwritebuf]+=2;

		/* Advance to next window */
		oldbuf = ss->inwritebuf;
		ss->inwritebuf = (ss->inwritebuf + 1) % ss->numbufs;

		if (ss->inwritebuf == ss->outwritebuf) {
			/* Whoops, no more space.  */
			ss->inwritebuf = -1;
		}
		if (ss->outwritebuf < 0) {
			/* Let the interrupt handler know there's
			   some space for us */
			ss->outwritebuf = oldbuf;
		}
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "Buffered %d bytes (skblen = %d) to go out in buffer %d\n", ss->writen[oldbuf], skb->len, oldbuf);
		for (x=0;x<ss->writen[oldbuf];x++)
		     printk("%02x ", ss->writebuf[oldbuf][x]);
		printk("\n");
#endif
		retval = 1;
	}
	spin_unlock_irqrestore(&ss->lock, flags);
	if (retval) {
		/* Get rid of the SKB if we're returning non-zero */
		/* N.B. this is called in process or BH context so
		   dev_kfree_skb is OK. */
		dev_kfree_skb(skb);
	}
	return retval;
}

static int dahdi_ppp_ioctl(struct ppp_channel *ppp, unsigned int cmd, unsigned long flags)
{
	return -EIO;
}

static struct ppp_channel_ops ztppp_ops =
{
	start_xmit: dahdi_ppp_xmit,
	ioctl: dahdi_ppp_ioctl,
};

#endif

static void dahdi_chan_unreg(struct dahdi_chan *chan)
{
	int x;
	unsigned long flags;
#ifdef CONFIG_DAHDI_NET
	if (chan->flags & DAHDI_FLAG_NETDEV) {
		unregister_hdlc_device(chan->hdlcnetdev->netdev);
		free_netdev(chan->hdlcnetdev->netdev);
		kfree(chan->hdlcnetdev);
		chan->hdlcnetdev = NULL;
	}
#endif
	write_lock_irqsave(&chan_lock, flags);
	if (chan->flags & DAHDI_FLAG_REGISTERED) {
		chans[chan->channo] = NULL;
		chan->flags &= ~DAHDI_FLAG_REGISTERED;
	}
#ifdef CONFIG_DAHDI_PPP
	if (chan->ppp) {
		module_printk(KERN_NOTICE, "HUH???  PPP still attached??\n");
	}
#endif
	maxchans = 0;
	for (x=1;x<DAHDI_MAX_CHANNELS;x++) 
		if (chans[x]) {
			maxchans = x + 1;
			/* Remove anyone pointing to us as master
			   and make them their own thing */
			if (chans[x]->master == chan) {
				chans[x]->master = chans[x];
			}
			if ((chans[x]->confna == chan->channo) &&
				((chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORTX ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORBOTH ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_RX_PREECHO ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_TX_PREECHO ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORBOTH_PREECHO ||
				(chans[x]->confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_DIGITALMON)) {
				/* Take them out of conference with us */
				/* release conference resource if any */
				if (chans[x]->confna) {
					dahdi_check_conf(chans[x]->confna);
					if (chans[x]->span && chans[x]->span->dacs)
						chans[x]->span->dacs(chans[x], NULL);
				}
				chans[x]->confna = 0;
				chans[x]->_confn = 0;
				chans[x]->confmode = 0;
			}
		}
	chan->channo = -1;
	write_unlock_irqrestore(&chan_lock, flags);
}

static ssize_t dahdi_chan_read(struct file *file, char *usrbuf, size_t count, int unit)
{
	struct dahdi_chan *chan = chans[unit];
	int amnt;
	int res, rv;
	int oldbuf,x;
	unsigned long flags;
	/* Make sure count never exceeds 65k, and make sure it's unsigned */
	count &= 0xffff;
	if (!chan) 
		return -EINVAL;
	if (count < 1)
		return -EINVAL;
	for(;;) {
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->eventinidx != chan->eventoutidx) {
			spin_unlock_irqrestore(&chan->lock, flags);
			return -ELAST /* - chan->eventbuf[chan->eventoutidx]*/;
		}
		res = chan->outreadbuf;
		if (chan->rxdisable)
			res = -1;
		spin_unlock_irqrestore(&chan->lock, flags);
		if (res >= 0) break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		rv = schluffen(&chan->readbufq);
		if (rv) return (rv);
	}
	amnt = count;
/* added */
#if 0
	if ((unit == 24) || (unit == 48) || (unit == 16) || (unit == 47)) { 
		int myamnt = amnt;
		int x;
		if (amnt > chan->readn[res])
			myamnt = chan->readn[res];
		module_printk(KERN_NOTICE, "dahdi_chan_read(unit: %d, inwritebuf: %d, outwritebuf: %d amnt: %d\n", 
			      unit, chan->inwritebuf, chan->outwritebuf, myamnt);
		printk("\t("); for (x = 0; x < myamnt; x++) printk((x ? " %02x" : "%02x"), (unsigned char)usrbuf[x]);
		printk(")\n");
	}
#endif
/* end addition */
	if (chan->flags & DAHDI_FLAG_LINEAR) {
		if (amnt > (chan->readn[res] << 1))
			amnt = chan->readn[res] << 1;
		if (amnt) {
			/* There seems to be a max stack size, so we have
			   to do this in smaller pieces */
			short lindata[128];
			int left = amnt >> 1; /* amnt is in bytes */
			int pos = 0;
			int pass;
			while(left) {
				pass = left;
				if (pass > 128)
					pass = 128;
				for (x=0;x<pass;x++)
					lindata[x] = DAHDI_XLAW(chan->readbuf[res][x + pos], chan);
				if (copy_to_user(usrbuf + (pos << 1), lindata, pass << 1))
					return -EFAULT;
				left -= pass;
				pos += pass;
			}
		}
	} else {
		if (amnt > chan->readn[res])
			amnt = chan->readn[res];
		if (amnt) {
			if (copy_to_user(usrbuf, chan->readbuf[res], amnt))
				return -EFAULT;
		}
	}
	spin_lock_irqsave(&chan->lock, flags);
	chan->readidx[res] = 0;
	chan->readn[res] = 0;
	oldbuf = res;
	chan->outreadbuf = (res + 1) % chan->numbufs;
	if (chan->outreadbuf == chan->inreadbuf) {
		/* Out of stuff */
		chan->outreadbuf = -1;
		if (chan->rxbufpolicy == DAHDI_POLICY_WHEN_FULL)
			chan->rxdisable = 1;
	}
	if (chan->inreadbuf < 0) {
		/* Notify interrupt handler that we have some space now */
		chan->inreadbuf = oldbuf;
	}
	spin_unlock_irqrestore(&chan->lock, flags);
	
	return amnt;
}

static ssize_t dahdi_chan_write(struct file *file, const char *usrbuf, size_t count, int unit)
{
	unsigned long flags;
	struct dahdi_chan *chan = chans[unit];
	int res, amnt, oldbuf, rv,x;
	/* Make sure count never exceeds 65k, and make sure it's unsigned */
	count &= 0xffff;
	if (!chan) 
		return -EINVAL;
	if (count < 1)
		return -EINVAL;
	for(;;) {
		spin_lock_irqsave(&chan->lock, flags);
		if ((chan->curtone || chan->pdialcount) && !(chan->flags & DAHDI_FLAG_PSEUDO)) {
			chan->curtone = NULL;
			chan->tonep = 0;
			chan->dialing = 0;
			chan->txdialbuf[0] = '\0';
			chan->pdialcount = 0;
		}
		if (chan->eventinidx != chan->eventoutidx) {
			spin_unlock_irqrestore(&chan->lock, flags);
			return -ELAST;
		}
		res = chan->inwritebuf;
		spin_unlock_irqrestore(&chan->lock, flags);
		if (res >= 0) 
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		/* Wait for something to be available */
		rv = schluffen(&chan->writebufq);
		if (rv)
			return rv;
	}

	amnt = count;
	if (chan->flags & DAHDI_FLAG_LINEAR) {
		if (amnt > (chan->blocksize << 1))
			amnt = chan->blocksize << 1;
	} else {
		if (amnt > chan->blocksize)
			amnt = chan->blocksize;
	}

#ifdef CONFIG_DAHDI_DEBUG
	module_printk(KERN_NOTICE, "dahdi_chan_write(unit: %d, res: %d, outwritebuf: %d amnt: %d\n",
		      unit, chan->res, chan->outwritebuf, amnt);
#endif
#if 0
 	if ((unit == 24) || (unit == 48) || (unit == 16) || (unit == 47)) { 
 		int x;
 		module_printk(KERN_NOTICE, "dahdi_chan_write/in(unit: %d, res: %d, outwritebuf: %d amnt: %d, txdisable: %d)\n",
			      unit, res, chan->outwritebuf, amnt, chan->txdisable);
 		printk("\t("); for (x = 0; x < amnt; x++) printk((x ? " %02x" : "%02x"), (unsigned char)usrbuf[x]);
 		printk(")\n");
 	}
#endif

	if (amnt) {
		if (chan->flags & DAHDI_FLAG_LINEAR) {
			/* There seems to be a max stack size, so we have
			   to do this in smaller pieces */
			short lindata[128];
			int left = amnt >> 1; /* amnt is in bytes */
			int pos = 0;
			int pass;
			while(left) {
				pass = left;
				if (pass > 128)
					pass = 128;
				if (copy_from_user(lindata, usrbuf + (pos << 1), pass << 1))
					return -EFAULT;
				left -= pass;
				for (x=0;x<pass;x++)
					chan->writebuf[res][x + pos] = DAHDI_LIN2X(lindata[x], chan);
				pos += pass;
			}
			chan->writen[res] = amnt >> 1;
		} else {
			if (copy_from_user(chan->writebuf[res], usrbuf, amnt))
				return -EFAULT;
			chan->writen[res] = amnt;
		}
		chan->writeidx[res] = 0;
		if (chan->flags & DAHDI_FLAG_FCS)
			calc_fcs(chan, res);
		oldbuf = res;
		spin_lock_irqsave(&chan->lock, flags);
		chan->inwritebuf = (res + 1) % chan->numbufs;
		if (chan->inwritebuf == chan->outwritebuf) {
			/* Don't stomp on the transmitter, just wait for them to 
			   wake us up */
			chan->inwritebuf = -1;
			/* Make sure the transmitter is transmitting in case of POLICY_WHEN_FULL */
			chan->txdisable = 0;
		}
		if (chan->outwritebuf < 0) {
			/* Okay, the interrupt handler has been waiting for us.  Give them a buffer */
			chan->outwritebuf = oldbuf;
		}
		spin_unlock_irqrestore(&chan->lock, flags);

		if (chan->flags & DAHDI_FLAG_NOSTDTXRX && chan->span->hdlc_hard_xmit)
			chan->span->hdlc_hard_xmit(chan);
	}
	return amnt;
}

static int dahdi_ctl_open(struct inode *inode, struct file *file)
{
	/* Nothing to do, really */
	return 0;
}

static int dahdi_chan_open(struct inode *inode, struct file *file)
{
	/* Nothing to do here for now either */
	return 0;
}

static int dahdi_ctl_release(struct inode *inode, struct file *file)
{
	/* Nothing to do */
	return 0;
}

static int dahdi_chan_release(struct inode *inode, struct file *file)
{
	/* Nothing to do for now */
	return 0;
}

static void set_txtone(struct dahdi_chan *ss,int fac, int init_v2, int init_v3)
{
	if (fac == 0)
	{
		ss->v2_1 = 0;
		ss->v3_1 = 0;
		return;
	}
	ss->txtone = fac;
	ss->v1_1 = 0;
	ss->v2_1 = init_v2;
	ss->v3_1 = init_v3;
	return;
}

static void dahdi_rbs_sethook(struct dahdi_chan *chan, int txsig, int txstate, int timeout)
{
static int outs[NUM_SIGS][5] = {
/* We set the idle case of the DAHDI_SIG_NONE to this pattern to make idle E1 CAS
channels happy. Should not matter with T1, since on an un-configured channel, 
who cares what the sig bits are as long as they are stable */
	{ DAHDI_SIG_NONE, 		DAHDI_ABIT | DAHDI_CBIT | DAHDI_DBIT, 0, 0, 0 },  /* no signalling */
	{ DAHDI_SIG_EM, 		0, DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT,
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 0 },  /* E and M */
	{ DAHDI_SIG_FXSLS, 	DAHDI_BBIT | DAHDI_DBIT, 
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT,
			DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 0 }, /* FXS Loopstart */
	{ DAHDI_SIG_FXSGS, 	DAHDI_BBIT | DAHDI_DBIT, 
#ifdef CONFIG_CAC_GROUNDSTART
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 0, 0 }, /* FXS Groundstart (CAC-style) */
#else
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, DAHDI_ABIT | DAHDI_CBIT, 0 }, /* FXS Groundstart (normal) */
#endif
	{ DAHDI_SIG_FXSKS,		DAHDI_BBIT | DAHDI_DBIT, 
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT,
			DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 0 }, /* FXS Kewlstart */
	{ DAHDI_SIG_FXOLS,		DAHDI_BBIT | DAHDI_DBIT, DAHDI_BBIT | DAHDI_DBIT, 0, 0 }, /* FXO Loopstart */
	{ DAHDI_SIG_FXOGS,		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT,
		 DAHDI_BBIT | DAHDI_DBIT, 0, 0 }, /* FXO Groundstart */
	{ DAHDI_SIG_FXOKS,		DAHDI_BBIT | DAHDI_DBIT, DAHDI_BBIT | DAHDI_DBIT, 0, 
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT }, /* FXO Kewlstart */
	{ DAHDI_SIG_SF,	DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 
			DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 
			DAHDI_ABIT | DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT, 
			DAHDI_BBIT | DAHDI_CBIT | DAHDI_DBIT },  /* no signalling */
	{ DAHDI_SIG_EM_E1, 	DAHDI_DBIT, DAHDI_ABIT | DAHDI_BBIT | DAHDI_DBIT,
		DAHDI_ABIT | DAHDI_BBIT | DAHDI_DBIT, DAHDI_DBIT },  /* E and M  E1 */
	} ;
	int x;

	/* if no span, return doing nothing */
	if (!chan->span) return;
	if (!chan->span->flags & DAHDI_FLAG_RBS) {
		module_printk(KERN_NOTICE, "dahdi_rbs: Tried to set RBS hook state on non-RBS channel %s\n", chan->name);
		return;
	}
	if ((txsig > 3) || (txsig < 0)) {
		module_printk(KERN_NOTICE, "dahdi_rbs: Tried to set RBS hook state %d (> 3) on  channel %s\n", txsig, chan->name);
		return;
	}
	if (!chan->span->rbsbits && !chan->span->hooksig) {
		module_printk(KERN_NOTICE, "dahdi_rbs: Tried to set RBS hook state %d on channel %s while span %s lacks rbsbits or hooksig function\n",
			txsig, chan->name, chan->span->name);
		return;
	}
	/* Don't do anything for RBS */
	if (chan->sig == DAHDI_SIG_DACS_RBS)
		return;
	chan->txstate = txstate;
	
	/* if tone signalling */
	if (chan->sig == DAHDI_SIG_SF)
	{
		chan->txhooksig = txsig;
		if (chan->txtone) /* if set to make tone for tx */
		{
			if ((txsig && !(chan->toneflags & DAHDI_REVERSE_TXTONE)) ||
			 ((!txsig) && (chan->toneflags & DAHDI_REVERSE_TXTONE))) 
			{
				set_txtone(chan,chan->txtone,chan->tx_v2,chan->tx_v3);
			}
			else
			{
				set_txtone(chan,0,0,0);
			}
		}
		chan->otimer = timeout * DAHDI_CHUNKSIZE;			/* Otimer is timer in samples */
		return;
	}
	if (chan->span->hooksig) {
		if (chan->txhooksig != txsig) {
			chan->txhooksig = txsig;
			chan->span->hooksig(chan, txsig);
		}
		chan->otimer = timeout * DAHDI_CHUNKSIZE;			/* Otimer is timer in samples */
		return;
	} else {
		for (x=0;x<NUM_SIGS;x++) {
			if (outs[x][0] == chan->sig) {
#ifdef CONFIG_DAHDI_DEBUG
				module_printk(KERN_NOTICE, "Setting bits to %d for channel %s state %d in %d signalling\n", outs[x][txsig + 1], chan->name, txsig, chan->sig);
#endif
				chan->txhooksig = txsig;
				chan->txsig = outs[x][txsig+1];
				chan->span->rbsbits(chan, chan->txsig);
				chan->otimer = timeout * DAHDI_CHUNKSIZE;	/* Otimer is timer in samples */
				return;
			}
		}
	}
	module_printk(KERN_NOTICE, "dahdi_rbs: Don't know RBS signalling type %d on channel %s\n", chan->sig, chan->name);
}

static int dahdi_cas_setbits(struct dahdi_chan *chan, int bits)
{
	/* if no span, return as error */
	if (!chan->span) return -1;
	if (chan->span->rbsbits) {
		chan->txsig = bits;
		chan->span->rbsbits(chan, bits);
	} else {
		module_printk(KERN_NOTICE, "Huh?  CAS setbits, but no RBS bits function\n");
	}
	return 0;
}

static int dahdi_hangup(struct dahdi_chan *chan)
{
	int x,res=0;

	/* Can't hangup pseudo channels */
	if (!chan->span)
		return 0;
	/* Can't hang up a clear channel */
	if (chan->flags & (DAHDI_FLAG_CLEAR | DAHDI_FLAG_NOSTDTXRX))
		return -EINVAL;

	chan->kewlonhook = 0;


	if ((chan->sig == DAHDI_SIG_FXSLS) || (chan->sig == DAHDI_SIG_FXSKS) ||
		(chan->sig == DAHDI_SIG_FXSGS)) chan->ringdebtimer = RING_DEBOUNCE_TIME;

	if (chan->span->flags & DAHDI_FLAG_RBS) {
		if (chan->sig == DAHDI_SIG_CAS) {
			dahdi_cas_setbits(chan, chan->idlebits);
		} else if ((chan->sig == DAHDI_SIG_FXOKS) && (chan->txstate != DAHDI_TXSTATE_ONHOOK)
			/* if other party is already on-hook we shouldn't do any battery drop */
			&& !((chan->rxhooksig == DAHDI_RXSIG_ONHOOK) && (chan->itimer <= 0))) {
			/* Do RBS signalling on the channel's behalf */
			dahdi_rbs_sethook(chan, DAHDI_TXSIG_KEWL, DAHDI_TXSTATE_KEWL, DAHDI_KEWLTIME);
		} else
			dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_ONHOOK, 0);
	} else {
		/* Let the driver hang up the line if it wants to  */
		if (chan->span->sethook) {
			if (chan->txhooksig != DAHDI_ONHOOK) {
				chan->txhooksig = DAHDI_ONHOOK;
				res = chan->span->sethook(chan, DAHDI_ONHOOK);
			} else
				res = 0;
		}
	}
	/* if not registered yet, just return here */
	if (!(chan->flags & DAHDI_FLAG_REGISTERED)) return res;
	/* Mark all buffers as empty */
	for (x = 0;x < chan->numbufs;x++) {
		chan->writen[x] = 
		chan->writeidx[x]=
		chan->readn[x]=
		chan->readidx[x] = 0;
	}	
	if (chan->readbuf[0]) {
		chan->inreadbuf = 0;
		chan->inwritebuf = 0;
	} else {
		chan->inreadbuf = -1;
		chan->inwritebuf = -1;
	}
	chan->outreadbuf = -1;
	chan->outwritebuf = -1;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;
	chan->curtone = NULL;
	chan->pdialcount = 0;
	chan->cadencepos = 0;
	chan->txdialbuf[0] = 0;
	return res;
}

static int initialize_channel(struct dahdi_chan *chan)
{
	int res;
	unsigned long flags;
	void *rxgain=NULL;
	struct echo_can_state *ec_state;
	const struct dahdi_echocan *ec_current;

	if ((res = dahdi_reallocbufs(chan, DAHDI_DEFAULT_BLOCKSIZE, DAHDI_DEFAULT_NUM_BUFS)))
		return res;

	spin_lock_irqsave(&chan->lock, flags);

	chan->rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
	chan->txbufpolicy = DAHDI_POLICY_IMMEDIATE;

	ec_state = chan->ec_state;
	chan->ec_state = NULL;
	ec_current = chan->ec_current;
	chan->ec_current = NULL;
	chan->echocancel = 0;
	chan->echostate = ECHO_STATE_IDLE;
	chan->echolastupdate = 0;
	chan->echotimer = 0;

	chan->txdisable = 0;
	chan->rxdisable = 0;

	chan->digitmode = DIGIT_MODE_DTMF;
	chan->dialing = 0;
	chan->afterdialingtimer = 0;

	chan->cadencepos = 0;
	chan->firstcadencepos = 0; /* By default loop back to first cadence position */

	/* HDLC & FCS stuff */
	fasthdlc_init(&chan->rxhdlc);
	fasthdlc_init(&chan->txhdlc);
	chan->infcs = PPP_INITFCS;
	
	/* Timings for RBS */
	chan->prewinktime = DAHDI_DEFAULT_PREWINKTIME;
	chan->preflashtime = DAHDI_DEFAULT_PREFLASHTIME;
	chan->winktime = DAHDI_DEFAULT_WINKTIME;
	chan->flashtime = DAHDI_DEFAULT_FLASHTIME;
	
	if (chan->sig & __DAHDI_SIG_FXO)
		chan->starttime = DAHDI_DEFAULT_RINGTIME;
	else
		chan->starttime = DAHDI_DEFAULT_STARTTIME;
	chan->rxwinktime = DAHDI_DEFAULT_RXWINKTIME;
	chan->rxflashtime = DAHDI_DEFAULT_RXFLASHTIME;
	chan->debouncetime = DAHDI_DEFAULT_DEBOUNCETIME;
	chan->pulsemaketime = DAHDI_DEFAULT_PULSEMAKETIME;
	chan->pulsebreaktime = DAHDI_DEFAULT_PULSEBREAKTIME;
	chan->pulseaftertime = DAHDI_DEFAULT_PULSEAFTERTIME;
	
	/* Initialize RBS timers */
	chan->itimerset = chan->itimer = chan->otimer = 0;
	chan->ringdebtimer = 0;		

	init_waitqueue_head(&chan->sel);
	init_waitqueue_head(&chan->readbufq);
	init_waitqueue_head(&chan->writebufq);
	init_waitqueue_head(&chan->eventbufq);
	init_waitqueue_head(&chan->txstateq);

	/* Reset conferences */
	reset_conf(chan);
	
	/* I/O Mask, etc */
	chan->iomask = 0;
	/* release conference resource if any */
	if (chan->confna) dahdi_check_conf(chan->confna);
	if ((chan->sig & __DAHDI_SIG_DACS) != __DAHDI_SIG_DACS) {
		chan->confna = 0;
		chan->confmode = 0;
		if (chan->span && chan->span->dacs)
			chan->span->dacs(chan, NULL);
	}
	chan->_confn = 0;
	memset(chan->conflast, 0, sizeof(chan->conflast));
	memset(chan->conflast1, 0, sizeof(chan->conflast1));
	memset(chan->conflast2, 0, sizeof(chan->conflast2));
	chan->confmute = 0;
	chan->gotgs = 0;
	chan->curtone = NULL;
	chan->tonep = 0;
	chan->pdialcount = 0;
	if (chan->gainalloc && chan->rxgain)
		rxgain = chan->rxgain;
	chan->rxgain = defgain;
	chan->txgain = defgain;
	chan->gainalloc = 0;
	chan->eventinidx = chan->eventoutidx = 0;
	dahdi_set_law(chan,0);
	dahdi_hangup(chan);

	/* Make sure that the audio flag is cleared on a clear channel */
	if ((chan->sig & DAHDI_SIG_CLEAR) || (chan->sig & DAHDI_SIG_HARDHDLC))
		chan->flags &= ~DAHDI_FLAG_AUDIO;

	if ((chan->sig == DAHDI_SIG_CLEAR) || (chan->sig == DAHDI_SIG_HARDHDLC))
		chan->flags &= ~(DAHDI_FLAG_PPP | DAHDI_FLAG_FCS | DAHDI_FLAG_HDLC);

	chan->flags &= ~DAHDI_FLAG_LINEAR;
	if (chan->curzone) {
		/* Take cadence from tone zone */
		memcpy(chan->ringcadence, chan->curzone->ringcadence, sizeof(chan->ringcadence));
	} else {
		/* Do a default */
		memset(chan->ringcadence, 0, sizeof(chan->ringcadence));
		chan->ringcadence[0] = chan->starttime;
		chan->ringcadence[1] = DAHDI_RINGOFFTIME;
	}

	if (ec_state) {
		ec_current->echo_can_free(ec_state);
		release_echocan(ec_current);
	}

	spin_unlock_irqrestore(&chan->lock, flags);

	set_tone_zone(chan, -1);

	hw_echocancel_off(chan);

	if (rxgain)
		kfree(rxgain);

	return 0;
}

static int dahdi_timing_open(struct inode *inode, struct file *file)
{
	struct dahdi_timer *t;
	unsigned long flags;
	t = kmalloc(sizeof(struct dahdi_timer), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	/* Allocate a new timer */
	memset(t, 0, sizeof(struct dahdi_timer));
	init_waitqueue_head(&t->sel);
	file->private_data = t;
	spin_lock_irqsave(&zaptimerlock, flags);
	t->next = zaptimers;
	zaptimers = t;
	spin_unlock_irqrestore(&zaptimerlock, flags);
	return 0;
}

static int dahdi_timer_release(struct inode *inode, struct file *file)
{
	struct dahdi_timer *t, *cur, *prev;
	unsigned long flags;
	t = file->private_data;
	if (t) {
		spin_lock_irqsave(&zaptimerlock, flags);
		prev = NULL;
		cur = zaptimers;
		while(cur) {
			if (t == cur)
				break;
			prev = cur;
			cur = cur->next;
		}
		if (cur) {
			if (prev)
				prev->next = cur->next;
			else
				zaptimers = cur->next;
		}
		spin_unlock_irqrestore(&zaptimerlock, flags);
		if (!cur) {
			module_printk(KERN_NOTICE, "Timer: Not on list??\n");
			return 0;
		}
		kfree(t);
	}
	return 0;
}

static int dahdi_specchan_open(struct inode *inode, struct file *file, int unit, int inc)
{
	int res = 0;

	if (chans[unit] && chans[unit]->sig) {
		/* Make sure we're not already open, a net device, or a slave device */
		if (chans[unit]->flags & DAHDI_FLAG_NETDEV)
			res = -EBUSY;
		else if (chans[unit]->master != chans[unit])
			res = -EBUSY;
		else if ((chans[unit]->sig & __DAHDI_SIG_DACS) == __DAHDI_SIG_DACS)
			res = -EBUSY;
		else if (!test_and_set_bit(DAHDI_FLAGBIT_OPEN, &chans[unit]->flags)) {
			unsigned long flags;
			res = initialize_channel(chans[unit]);
			if (res) {
				/* Reallocbufs must have failed */
				clear_bit(DAHDI_FLAGBIT_OPEN, &chans[unit]->flags);
				return res;
			}
			spin_lock_irqsave(&chans[unit]->lock, flags);
			if (chans[unit]->flags & DAHDI_FLAG_PSEUDO) 
				chans[unit]->flags |= DAHDI_FLAG_AUDIO;
			if (chans[unit]->span && chans[unit]->span->open) {
				res = chans[unit]->span->open(chans[unit]);
			}
			if (!res) {
				chans[unit]->file = file;
				spin_unlock_irqrestore(&chans[unit]->lock, flags);
			} else {
				spin_unlock_irqrestore(&chans[unit]->lock, flags);
				close_channel(chans[unit]);
				clear_bit(DAHDI_FLAGBIT_OPEN, &chans[unit]->flags);
			}
		} else
			res = -EBUSY;
	} else
		res = -ENXIO;
	return res;
}

static int dahdi_specchan_release(struct inode *node, struct file *file, int unit)
{
	int res=0;
	unsigned long flags;

	if (chans[unit]) {
		/* Chan lock protects contents against potentially non atomic accesses.
		 * So if the pointer setting is not atomic, we should protect */
		spin_lock_irqsave(&chans[unit]->lock, flags);
		chans[unit]->file = NULL;
		spin_unlock_irqrestore(&chans[unit]->lock, flags);
		close_channel(chans[unit]);
		if (chans[unit]->span && chans[unit]->span->close)
			res = chans[unit]->span->close(chans[unit]);
		clear_bit(DAHDI_FLAGBIT_OPEN, &chans[unit]->flags);
	} else
		res = -ENXIO;
	return res;
}

static struct dahdi_chan *dahdi_alloc_pseudo(void)
{
	struct dahdi_chan *pseudo;
	unsigned long flags;
	/* Don't allow /dev/dahdi/pseudo to open if there are no spans */
	if (maxspans < 1)
		return NULL;
	pseudo = kmalloc(sizeof(struct dahdi_chan), GFP_KERNEL);
	if (!pseudo)
		return NULL;
	memset(pseudo, 0, sizeof(struct dahdi_chan));
	pseudo->sig = DAHDI_SIG_CLEAR;
	pseudo->sigcap = DAHDI_SIG_CLEAR;
	pseudo->flags = DAHDI_FLAG_PSEUDO | DAHDI_FLAG_AUDIO;
	spin_lock_irqsave(&bigzaplock, flags);
	if (dahdi_chan_reg(pseudo)) {
		kfree(pseudo);
		pseudo = NULL;
	} else
		sprintf(pseudo->name, "Pseudo/%d", pseudo->channo);
	spin_unlock_irqrestore(&bigzaplock, flags);
	return pseudo;	
}

static void dahdi_free_pseudo(struct dahdi_chan *pseudo)
{
	unsigned long flags;
	if (pseudo) {
		spin_lock_irqsave(&bigzaplock, flags);
		dahdi_chan_unreg(pseudo);
		spin_unlock_irqrestore(&bigzaplock, flags);
		kfree(pseudo);
	}
}

static int dahdi_open(struct inode *inode, struct file *file)
{
	int unit = UNIT(file);
	int ret = -ENXIO;
	struct dahdi_chan *chan;
	/* Minor 0: Special "control" descriptor */
	if (!unit) 
		return dahdi_ctl_open(inode, file);
	if (unit == 250) {
		if (!dahdi_transcode_fops)
			request_module("dahdi_transcode");
		if (dahdi_transcode_fops && dahdi_transcode_fops->open) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
			if (dahdi_transcode_fops->owner) {
				__MOD_INC_USE_COUNT (dahdi_transcode_fops->owner);
#else
			if (try_module_get(dahdi_transcode_fops->owner)) {
#endif
				ret = dahdi_transcode_fops->open(inode, file);
				if (ret)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
					__MOD_DEC_USE_COUNT (dahdi_transcode_fops->owner);
#else
					module_put(dahdi_transcode_fops->owner);
#endif
			}
			return ret;
		}
		return -ENXIO;
	}
	if (unit == 253) {
		if (maxspans) {
			return dahdi_timing_open(inode, file);
		} else {
			return -ENXIO;
		}
	}
	if (unit == 254)
		return dahdi_chan_open(inode, file);
	if (unit == 255) {
		if (maxspans) {
			chan = dahdi_alloc_pseudo();
			if (chan) {
				file->private_data = chan;
				return dahdi_specchan_open(inode, file, chan->channo, 1);
			} else {
				return -ENXIO;
			}
		} else
			return -ENXIO;
	}
	return dahdi_specchan_open(inode, file, unit, 1);
}

#if 0
static int dahdi_open(struct inode *inode, struct file *file)
{
	int res;
	unsigned long flags;
	spin_lock_irqsave(&bigzaplock, flags);
	res = __dahdi_open(inode, file);
	spin_unlock_irqrestore(&bigzaplock, flags);
	return res;
}
#endif

static ssize_t dahdi_read(struct file *file, char *usrbuf, size_t count, loff_t *ppos)
{
	int unit = UNIT(file);
	struct dahdi_chan *chan;

	/* Can't read from control */
	if (!unit) {
		return -EINVAL;
	}
	
	if (unit == 253) 
		return -EINVAL;
	
	if (unit == 254) {
		chan = file->private_data;
		if (!chan)
			return -EINVAL;
		return dahdi_chan_read(file, usrbuf, count, chan->channo);
	}
	
	if (unit == 255) {
		chan = file->private_data;
		if (!chan) {
			module_printk(KERN_NOTICE, "No pseudo channel structure to read?\n");
			return -EINVAL;
		}
		return dahdi_chan_read(file, usrbuf, count, chan->channo);
	}
	if (count < 0)
		return -EINVAL;

	return dahdi_chan_read(file, usrbuf, count, unit);
}

static ssize_t dahdi_write(struct file *file, const char *usrbuf, size_t count, loff_t *ppos)
{
	int unit = UNIT(file);
	struct dahdi_chan *chan;
	/* Can't read from control */
	if (!unit)
		return -EINVAL;
	if (count < 0)
		return -EINVAL;
	if (unit == 253)
		return -EINVAL;
	if (unit == 254) {
		chan = file->private_data;
		if (!chan)
			return -EINVAL;
		return dahdi_chan_write(file, usrbuf, count, chan->channo);
	}
	if (unit == 255) {
		chan = file->private_data;
		if (!chan) {
			module_printk(KERN_NOTICE, "No pseudo channel structure to read?\n");
			return -EINVAL;
		}
		return dahdi_chan_write(file, usrbuf, count, chan->channo);
	}
	return dahdi_chan_write(file, usrbuf, count, unit);
	
}

static int dahdi_set_default_zone(int defzone)
{
	if ((defzone < 0) || (defzone >= DAHDI_TONE_ZONE_MAX))
		return -EINVAL;
	write_lock(&zone_lock);
	if (!tone_zones[defzone]) {
		write_unlock(&zone_lock);
		return -EINVAL;
	}
	if ((default_zone != -1) && tone_zones[default_zone])
		atomic_dec(&tone_zones[default_zone]->refcount);
	atomic_inc(&tone_zones[defzone]->refcount);
	default_zone = defzone;
	write_unlock(&zone_lock);
	return 0;
}

/* No bigger than 32k for everything per tone zone */
#define MAX_SIZE 32768
/* No more than 128 subtones */
#define MAX_TONES 128

/* The tones to be loaded can (will) be a mix of regular tones,
   DTMF tones and MF tones. We need to load DTMF and MF tones
   a bit differently than regular tones because their storage
   format is much simpler (an array structure field of the zone
   structure, rather an array of pointers).
*/
static int ioctl_load_zone(unsigned long data)
{
	struct dahdi_tone *samples[MAX_TONES] = { NULL, };
	short next[MAX_TONES] = { 0, };
	struct dahdi_tone_def_header th;
	struct dahdi_tone_def td;
	struct dahdi_zone *z;
	struct dahdi_tone *t;
	void *slab, *ptr;
	int x;
	size_t space;
	size_t size;
	int res;
	
	if (copy_from_user(&th, (struct dahdi_tone_def_header *) data, sizeof(th)))
		return -EFAULT;

	data += sizeof(th);

	if ((th.count < 0) || (th.count > MAX_TONES)) {
		module_printk(KERN_NOTICE, "Too many tones included\n");
		return -EINVAL;
	}

	space = size = sizeof(*z) + th.count * sizeof(*t);

	if (size > MAX_SIZE)
		return -E2BIG;

	if (!(z = ptr = slab = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;

	memset(slab, 0, size);

	ptr += sizeof(*z);
	space -= sizeof(*z);

	dahdi_copy_string(z->name, th.name, sizeof(z->name));

	for (x = 0; x < DAHDI_MAX_CADENCE; x++)
		z->ringcadence[x] = th.ringcadence[x];

	atomic_set(&z->refcount, 0);

	for (x = 0; x < th.count; x++) {
		enum {
			REGULAR_TONE,
			DTMF_TONE,
			MFR1_TONE,
			MFR2_FWD_TONE,
			MFR2_REV_TONE,
		} tone_type;

		if (space < sizeof(*t)) {
			kfree(slab);
			module_printk(KERN_NOTICE, "Insufficient tone zone space\n");
			return -EINVAL;
		}

		if (copy_from_user(&td, (struct dahdi_tone_def *) data, sizeof(td))) {
			kfree(slab);
			return -EFAULT;
		}

		data += sizeof(td);

		if ((td.tone >= 0) && (td.tone < DAHDI_TONE_MAX)) {
			tone_type = REGULAR_TONE;

			t = samples[x] = ptr;

			space -= sizeof(*t);
			ptr += sizeof(*t);

			/* Remember which sample is next */
			next[x] = td.next;
			
			/* Make sure the "next" one is sane */
			if ((next[x] >= th.count) || (next[x] < 0)) {
				module_printk(KERN_NOTICE, "Invalid 'next' pointer: %d\n", next[x]);
				kfree(slab);
				return -EINVAL;
			}
		} else if ((td.tone >= DAHDI_TONE_DTMF_BASE) &&
			   (td.tone <= DAHDI_TONE_DTMF_MAX)) {
			tone_type = DTMF_TONE;
			td.tone -= DAHDI_TONE_DTMF_BASE;
			t = &z->dtmf[td.tone];
		} else if ((td.tone >= DAHDI_TONE_MFR1_BASE) &&
			   (td.tone <= DAHDI_TONE_MFR1_MAX)) {
			tone_type = MFR1_TONE;
			td.tone -= DAHDI_TONE_MFR1_BASE;
			t = &z->mfr1[td.tone];
		} else if ((td.tone >= DAHDI_TONE_MFR2_FWD_BASE) &&
			   (td.tone <= DAHDI_TONE_MFR2_FWD_MAX)) {
			tone_type = MFR2_FWD_TONE;
			td.tone -= DAHDI_TONE_MFR2_FWD_BASE;
			t = &z->mfr2_fwd[td.tone];
		} else if ((td.tone >= DAHDI_TONE_MFR2_REV_BASE) &&
			   (td.tone <= DAHDI_TONE_MFR2_REV_MAX)) {
			tone_type = MFR2_REV_TONE;
			td.tone -= DAHDI_TONE_MFR2_REV_BASE;
			t = &z->mfr2_rev[td.tone];
		} else {
			module_printk(KERN_NOTICE, "Invalid tone (%d) defined\n", td.tone);
			kfree(slab);
			return -EINVAL;
		}

		t->fac1 = td.fac1;
		t->init_v2_1 = td.init_v2_1;
		t->init_v3_1 = td.init_v3_1;
		t->fac2 = td.fac2;
		t->init_v2_2 = td.init_v2_2;
		t->init_v3_2 = td.init_v3_2;
		t->modulate = td.modulate;

		switch (tone_type) {
		case REGULAR_TONE:
			t->tonesamples = td.samples;
			if (!z->tones[td.tone])
				z->tones[td.tone] = t;
			break;
		case DTMF_TONE:
			t->tonesamples = global_dialparams.dtmf_tonelen;
			t->next = &dtmf_silence;
			z->dtmf_continuous[td.tone] = *t;
			z->dtmf_continuous[td.tone].next = &z->dtmf_continuous[td.tone];
			break;
		case MFR1_TONE:
			switch (td.tone + DAHDI_TONE_MFR1_BASE) {
			case DAHDI_TONE_MFR1_KP:
			case DAHDI_TONE_MFR1_ST:
			case DAHDI_TONE_MFR1_STP:
			case DAHDI_TONE_MFR1_ST2P:
			case DAHDI_TONE_MFR1_ST3P:
				/* signaling control tones are always 100ms */
				t->tonesamples = 100 * DAHDI_CHUNKSIZE;
				break;
			default:
				t->tonesamples = global_dialparams.mfv1_tonelen;
				break;
			}
			t->next = &mfr1_silence;
			break;
		case MFR2_FWD_TONE:
			t->tonesamples = global_dialparams.mfr2_tonelen;
			t->next = &dtmf_silence;
			z->mfr2_fwd_continuous[td.tone] = *t;
			z->mfr2_fwd_continuous[td.tone].next = &z->mfr2_fwd_continuous[td.tone];
			break;
		case MFR2_REV_TONE:
			t->tonesamples = global_dialparams.mfr2_tonelen;
			t->next = &dtmf_silence;
			z->mfr2_rev_continuous[td.tone] = *t;
			z->mfr2_rev_continuous[td.tone].next = &z->mfr2_rev_continuous[td.tone];
			break;
		}
	}

	for (x = 0; x < th.count; x++) {
		if (samples[x])
			samples[x]->next = samples[next[x]];
	}

	if ((res = dahdi_register_tone_zone(th.zone, z))) {
		kfree(slab);
	} else {
		if ( -1 == default_zone ) {
			dahdi_set_default_zone(th.zone);
		}
	}

	return res;
}

void dahdi_init_tone_state(struct dahdi_tone_state *ts, struct dahdi_tone *zt)
{
	ts->v1_1 = 0;
	ts->v2_1 = zt->init_v2_1;
	ts->v3_1 = zt->init_v3_1;
	ts->v1_2 = 0;
	ts->v2_2 = zt->init_v2_2;
	ts->v3_2 = zt->init_v3_2;
	ts->modulate = zt->modulate;
}

struct dahdi_tone *dahdi_mf_tone(const struct dahdi_chan *chan, char digit, int digitmode)
{
	unsigned int tone_index;

	if (!chan->curzone) {
		static int __warnonce = 1;
		if (__warnonce) {
			__warnonce = 0;
			/* The tonezones are loaded by ztcfg based on /etc/dahdi.conf. */
			module_printk(KERN_WARNING, "Cannot get dtmf tone until tone zone is loaded.\n");
		}
		return NULL;
	}

	switch (digitmode) {
	case DIGIT_MODE_DTMF:
		switch (digit) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			tone_index = DAHDI_TONE_DTMF_0 + (digit - '0');
			break;
		case '*':
			tone_index = DAHDI_TONE_DTMF_s;
			break;
		case '#':
			tone_index = DAHDI_TONE_DTMF_p;
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
			tone_index = DAHDI_TONE_DTMF_A + (digit - 'A');
		case 'W':
			return &tone_pause;
		default:
			return NULL;
		}
		return &chan->curzone->dtmf[tone_index - DAHDI_TONE_DTMF_BASE];
	case DIGIT_MODE_MFR1:
		switch (digit) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			tone_index = DAHDI_TONE_MFR1_0 + (digit - '0');
			break;
		case '*':
			tone_index = DAHDI_TONE_MFR1_KP;
			break;
		case '#':
			tone_index = DAHDI_TONE_MFR1_ST;
			break;
		case 'A':
			tone_index = DAHDI_TONE_MFR1_STP;
			break;
		case 'B':
			tone_index = DAHDI_TONE_MFR1_ST2P;
			break;
		case 'C':
			tone_index = DAHDI_TONE_MFR1_ST3P;
			break;
		case 'W':
			return &tone_pause;
		default:
			return NULL;
		}
		return &chan->curzone->mfr1[tone_index - DAHDI_TONE_MFR1_BASE];
	case DIGIT_MODE_MFR2_FWD:
		switch (digit) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			tone_index = DAHDI_TONE_MFR2_FWD_1 + (digit - '1');
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
			tone_index = DAHDI_TONE_MFR2_FWD_10 + (digit - 'A');
			break;
		case 'W':
			return &tone_pause;
		default:
			return NULL;
		}
		return &chan->curzone->mfr2_fwd[tone_index - DAHDI_TONE_MFR2_FWD_BASE];
	case DIGIT_MODE_MFR2_REV:
		switch (digit) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			tone_index = DAHDI_TONE_MFR2_REV_1 + (digit - '1');
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
			tone_index = DAHDI_TONE_MFR2_REV_10 + (digit - 'A');
			break;
		case 'W':
			return &tone_pause;
		default:
			return NULL;
		}
		return &chan->curzone->mfr2_rev[tone_index - DAHDI_TONE_MFR2_REV_BASE];
	default:
		return NULL;
	}
}

static void __do_dtmf(struct dahdi_chan *chan)
{
	char c;

	/* Called with chan->lock held */
	while ((c = chan->txdialbuf[0])) {
		memmove(chan->txdialbuf, chan->txdialbuf + 1, sizeof(chan->txdialbuf) - 1);
		switch (c) {
		case 'T':
			chan->digitmode = DIGIT_MODE_DTMF;
			chan->tonep = 0;
			break;
		case 'M':
			chan->digitmode = DIGIT_MODE_MFR1;
			chan->tonep = 0;
			break;
		case 'O':
			chan->digitmode = DIGIT_MODE_MFR2_FWD;
			chan->tonep = 0;
			break;
		case 'R':
			chan->digitmode = DIGIT_MODE_MFR2_REV;
			chan->tonep = 0;
			break;
		case 'P':
			chan->digitmode = DIGIT_MODE_PULSE;
			chan->tonep = 0;
			break;
		default:
			if ((c != 'W') && (chan->digitmode == DIGIT_MODE_PULSE)) {
				if ((c >= '0') && (c <= '9') && (chan->txhooksig == DAHDI_TXSIG_OFFHOOK)) {
					chan->pdialcount = (c == '0') ? 10 : c - '0';
					dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_PULSEBREAK,
						       chan->pulsebreaktime);
					return;
				}
			} else {
				chan->curtone = dahdi_mf_tone(chan, c, chan->digitmode);
				chan->tonep = 0;
				if (chan->curtone) {
					dahdi_init_tone_state(&chan->ts, chan->curtone);
					return;
				}
			}
		}
	}

	/* Notify userspace process if there is nothing left */
	chan->dialing = 0;
	__qevent(chan, DAHDI_EVENT_DIALCOMPLETE);
}

static int dahdi_release(struct inode *inode, struct file *file)
{
	int unit = UNIT(file);
	int res;
	struct dahdi_chan *chan;

	if (!unit) 
		return dahdi_ctl_release(inode, file);
	if (unit == 253) {
		return dahdi_timer_release(inode, file);
	}
	if (unit == 250) {
		res = dahdi_transcode_fops->release(inode, file);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		if (dahdi_transcode_fops->owner)
			__MOD_DEC_USE_COUNT (dahdi_transcode_fops->owner);
#else
		module_put(dahdi_transcode_fops->owner);
#endif
		return res;
	}
	if (unit == 254) {
		chan = file->private_data;
		if (!chan)
			return dahdi_chan_release(inode, file);
		else
			return dahdi_specchan_release(inode, file, chan->channo);
	}
	if (unit == 255) {
		chan = file->private_data;
		if (chan) {
			res = dahdi_specchan_release(inode, file, chan->channo);
			dahdi_free_pseudo(chan);
		} else {
			module_printk(KERN_NOTICE, "Pseudo release and no private data??\n");
			res = 0;
		}
		return res;
	}
	return dahdi_specchan_release(inode, file, unit);
}

#if 0
static int dahdi_release(struct inode *inode, struct file *file)
{
	/* Lock the big zap lock when handling a release */
	unsigned long flags;
	int res;
	spin_lock_irqsave(&bigzaplock, flags);
	res = __dahdi_release(inode, file);
	spin_unlock_irqrestore(&bigzaplock, flags);
	return res;
}
#endif


void dahdi_alarm_channel(struct dahdi_chan *chan, int alarms)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->chan_alarms != alarms) {
		chan->chan_alarms = alarms;
		dahdi_qevent_nolock(chan, alarms ? DAHDI_EVENT_ALARM : DAHDI_EVENT_NOALARM);
	}
	spin_unlock_irqrestore(&chan->lock, flags);
}

void dahdi_alarm_notify(struct dahdi_span *span)
{
	int x;

	span->alarms &= ~DAHDI_ALARM_LOOPBACK;
	/* Determine maint status */
	if (span->maintstat || span->mainttimer)
		span->alarms |= DAHDI_ALARM_LOOPBACK;
	/* DON'T CHANGE THIS AGAIN. THIS WAS DONE FOR A REASON.
 	   The expression (a != b) does *NOT* do the same thing
	   as ((!a) != (!b)) */
	/* if change in general state */
	if ((!span->alarms) != (!span->lastalarms)) {
		span->lastalarms = span->alarms;
		for (x = 0; x < span->channels; x++)
			dahdi_alarm_channel(&span->chans[x], span->alarms);
		/* Switch to other master if current master in alarm */
		for (x=1; x<maxspans; x++) {
			if (spans[x] && !spans[x]->alarms && (spans[x]->flags & DAHDI_FLAG_RUNNING)) {
				if(master != spans[x])
					module_printk(KERN_NOTICE, "Master changed to %s\n", spans[x]->name);
				master = spans[x];
				break;
			}
		}
	}
}

#define VALID_SPAN(j) do { \
	if ((j >= DAHDI_MAX_SPANS) || (j < 1)) \
		return -EINVAL; \
	if (!spans[j]) \
		return -ENXIO; \
} while(0)

#define CHECK_VALID_SPAN(j) do { \
	/* Start a given span */ \
	if (get_user(j, (int *)data)) \
		return -EFAULT; \
	VALID_SPAN(j); \
} while(0)

#define VALID_CHANNEL(j) do { \
	if ((j >= DAHDI_MAX_CHANNELS) || (j < 1)) \
		return -EINVAL; \
	if (!chans[j]) \
		return -ENXIO; \
} while(0)

static int dahdi_timer_ioctl(struct inode *node, struct file *file, unsigned int cmd, unsigned long data, struct dahdi_timer *timer)
{
	int j;
	unsigned long flags;
	switch(cmd) {
	case DAHDI_TIMERCONFIG:
		get_user(j, (int *)data);
		if (j < 0)
			j = 0;
		spin_lock_irqsave(&zaptimerlock, flags);
		timer->ms = timer->pos = j;
		spin_unlock_irqrestore(&zaptimerlock, flags);
		break;
	case DAHDI_TIMERACK:
		get_user(j, (int *)data);
		spin_lock_irqsave(&zaptimerlock, flags);
		if ((j < 1) || (j > timer->tripped))
			j = timer->tripped;
		timer->tripped -= j;
		spin_unlock_irqrestore(&zaptimerlock, flags);
		break;
	case DAHDI_GETEVENT:  /* Get event on queue */
		j = DAHDI_EVENT_NONE;
		spin_lock_irqsave(&zaptimerlock, flags);
		  /* set up for no event */
		if (timer->tripped)
			j = DAHDI_EVENT_TIMER_EXPIRED;
		if (timer->ping)
			j = DAHDI_EVENT_TIMER_PING;
		spin_unlock_irqrestore(&zaptimerlock, flags);
		put_user(j,(int *)data);
		break;
	case DAHDI_TIMERPING:
		spin_lock_irqsave(&zaptimerlock, flags);
		timer->ping = 1;
		wake_up_interruptible(&timer->sel);
		spin_unlock_irqrestore(&zaptimerlock, flags);
		break;
	case DAHDI_TIMERPONG:
		spin_lock_irqsave(&zaptimerlock, flags);
		timer->ping = 0;
		spin_unlock_irqrestore(&zaptimerlock, flags);
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static int dahdi_common_ioctl(struct inode *node, struct file *file, unsigned int cmd, unsigned long data, int unit)
{
	union {
		struct dahdi_gains gain;
		struct dahdi_spaninfo spaninfo;
		struct dahdi_params param;
	} stack;
	struct dahdi_chan *chan;
	unsigned long flags;
	unsigned char *txgain, *rxgain;
	struct dahdi_chan *mychan;
	int i,j;
	int return_master = 0;
	size_t size_to_copy;

	switch(cmd) {
		/* get channel parameters */
	case DAHDI_GET_PARAMS:
		size_to_copy = sizeof(struct dahdi_params);
		if (copy_from_user(&stack.param, (struct dahdi_params *) data, size_to_copy))
			return -EFAULT;

		/* check to see if the caller wants to receive our master channel number */
		if (stack.param.channo & DAHDI_GET_PARAMS_RETURN_MASTER) {
			return_master = 1;
			stack.param.channo &= ~DAHDI_GET_PARAMS_RETURN_MASTER;
		}

		/* Pick the right channo's */
		if (!stack.param.channo || unit) {
			stack.param.channo = unit;
		}
		/* Check validity of channel */
		VALID_CHANNEL(stack.param.channo);
		chan = chans[stack.param.channo];

		/* point to relevant structure */
		stack.param.sigtype = chan->sig;  /* get signalling type */
		/* return non-zero if rx not in idle state */
		if (chan->span) {
			j = dahdi_q_sig(chan); 
			if (j >= 0) { /* if returned with success */
				stack.param.rxisoffhook = ((chan->rxsig & (j >> 8)) != (j & 0xff));
			} else {
				stack.param.rxisoffhook = ((chan->rxhooksig != DAHDI_RXSIG_ONHOOK) &&
					(chan->rxhooksig != DAHDI_RXSIG_INITIAL));
			}
		} else if ((chan->txstate == DAHDI_TXSTATE_KEWL) || (chan->txstate == DAHDI_TXSTATE_AFTERKEWL))
			stack.param.rxisoffhook = 1;
		else
			stack.param.rxisoffhook = 0;
		if (chan->span && chan->span->rbsbits && !(chan->sig & DAHDI_SIG_CLEAR)) {
			stack.param.rxbits = chan->rxsig;
			stack.param.txbits = chan->txsig;
			stack.param.idlebits = chan->idlebits;
		} else {
			stack.param.rxbits = -1;
			stack.param.txbits = -1;
			stack.param.idlebits = 0;
		}
		if (chan->span && (chan->span->rbsbits || chan->span->hooksig) && 
			!(chan->sig & DAHDI_SIG_CLEAR)) {
			stack.param.rxhooksig = chan->rxhooksig;
			stack.param.txhooksig = chan->txhooksig;
		} else {
			stack.param.rxhooksig = -1;
			stack.param.txhooksig = -1;
		}
		stack.param.prewinktime = chan->prewinktime; 
		stack.param.preflashtime = chan->preflashtime;		
		stack.param.winktime = chan->winktime;
		stack.param.flashtime = chan->flashtime;
		stack.param.starttime = chan->starttime;
		stack.param.rxwinktime = chan->rxwinktime;
		stack.param.rxflashtime = chan->rxflashtime;
		stack.param.debouncetime = chan->debouncetime;
		stack.param.channo = chan->channo;
		stack.param.chan_alarms = chan->chan_alarms;

		/* if requested, put the master channel number in the top 16 bits of the result */
		if (return_master)
			stack.param.channo |= chan->master->channo << 16;

		stack.param.pulsemaketime = chan->pulsemaketime;
		stack.param.pulsebreaktime = chan->pulsebreaktime;
		stack.param.pulseaftertime = chan->pulseaftertime;
		if (chan->span) stack.param.spanno = chan->span->spanno;
			else stack.param.spanno = 0;
		dahdi_copy_string(stack.param.name, chan->name, sizeof(stack.param.name));
		stack.param.chanpos = chan->chanpos;
		stack.param.sigcap = chan->sigcap;
		/* Return current law */
		if (chan->xlaw == __dahdi_alaw)
			stack.param.curlaw = DAHDI_LAW_ALAW;
		else
			stack.param.curlaw = DAHDI_LAW_MULAW;

		if (copy_to_user((struct dahdi_params *) data, &stack.param, size_to_copy))
			return -EFAULT;

		break;
		/* set channel parameters */
	case DAHDI_SET_PARAMS:
		if (copy_from_user(&stack.param, (struct dahdi_params *) data, sizeof(struct dahdi_params)))
			return -EFAULT;

		stack.param.chan_alarms = 0; /* be explicit about the above */

		/* Pick the right channo's */
		if (!stack.param.channo || unit) {
			stack.param.channo = unit;
		}
		/* Check validity of channel */
		VALID_CHANNEL(stack.param.channo);
		chan = chans[stack.param.channo];
		  /* point to relevant structure */
		/* NOTE: sigtype is *not* included in this */
		  /* get timing stack.paramters */
		chan->prewinktime = stack.param.prewinktime;
		chan->preflashtime = stack.param.preflashtime;
		chan->winktime = stack.param.winktime;
		chan->flashtime = stack.param.flashtime;
		chan->starttime = stack.param.starttime;
		/* Update ringtime if not using a tone zone */
		if (!chan->curzone)
			chan->ringcadence[0] = chan->starttime;
		chan->rxwinktime = stack.param.rxwinktime;
		chan->rxflashtime = stack.param.rxflashtime;
		chan->debouncetime = stack.param.debouncetime;
		chan->pulsemaketime = stack.param.pulsemaketime;
		chan->pulsebreaktime = stack.param.pulsebreaktime;
		chan->pulseaftertime = stack.param.pulseaftertime;
		break;
	case DAHDI_GETGAINS:  /* get gain stuff */
		if (copy_from_user(&stack.gain,(struct dahdi_gains *) data,sizeof(stack.gain)))
			return -EFAULT;
		i = stack.gain.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = unit;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > DAHDI_MAX_CHANNELS) || !chans[i]) return(-EINVAL);
		
		if (!(chans[i]->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		stack.gain.chan = i; /* put the span # in here */
		for (j=0;j<256;j++)  {
			stack.gain.txgain[j] = chans[i]->txgain[j];
			stack.gain.rxgain[j] = chans[i]->rxgain[j];
		}
		if (copy_to_user((struct dahdi_gains *) data,&stack.gain,sizeof(stack.gain)))
			return -EFAULT;
		break;
	case DAHDI_SETGAINS:  /* set gain stuff */
		if (copy_from_user(&stack.gain,(struct dahdi_gains *) data,sizeof(stack.gain)))
			return -EFAULT;
		i = stack.gain.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = unit;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > DAHDI_MAX_CHANNELS) || !chans[i]) return(-EINVAL);
		if (!(chans[i]->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);

		rxgain = kmalloc(512, GFP_KERNEL);
		if (!rxgain)
			return -ENOMEM;

		stack.gain.chan = i; /* put the span # in here */
		txgain = rxgain + 256;

		for (j=0;j<256;j++) {
			rxgain[j] = stack.gain.rxgain[j];
			txgain[j] = stack.gain.txgain[j];
		}

		if (!memcmp(rxgain, defgain, 256) && 
		    !memcmp(txgain, defgain, 256)) {
			if (rxgain)
				kfree(rxgain);
			spin_lock_irqsave(&chans[i]->lock, flags);
			if (chans[i]->gainalloc)
				kfree(chans[i]->rxgain);
			chans[i]->gainalloc = 0;
			chans[i]->rxgain = defgain;
			chans[i]->txgain = defgain;
			spin_unlock_irqrestore(&chans[i]->lock, flags);
		} else {
			/* This is a custom gain setting */
			spin_lock_irqsave(&chans[i]->lock, flags);
			if (chans[i]->gainalloc)
				kfree(chans[i]->rxgain);
			chans[i]->gainalloc = 1;
			chans[i]->rxgain = rxgain;
			chans[i]->txgain = txgain;
			spin_unlock_irqrestore(&chans[i]->lock, flags);
		}
		if (copy_to_user((struct dahdi_gains *) data,&stack.gain,sizeof(stack.gain)))
			return -EFAULT;
		break;
	case DAHDI_SPANSTAT:
		size_to_copy = sizeof(struct dahdi_spaninfo);
		if (copy_from_user(&stack.spaninfo, (struct dahdi_spaninfo *) data, size_to_copy))
			return -EFAULT;
		i = stack.spaninfo.spanno; /* get specified span number */
		if ((i < 0) || (i >= maxspans)) return(-EINVAL);  /* if bad span no */
		if (i == 0) {
			/* if to figure it out for this chan */
			if (!chans[unit])
				return -EINVAL;
			i = chans[unit]->span->spanno;
		}
		if (!spans[i])
			return -EINVAL;
		stack.spaninfo.spanno = i; /* put the span # in here */
		stack.spaninfo.totalspans = 0;
		if (maxspans) stack.spaninfo.totalspans = maxspans - 1; /* put total number of spans here */
		dahdi_copy_string(stack.spaninfo.desc, spans[i]->desc, sizeof(stack.spaninfo.desc));
		dahdi_copy_string(stack.spaninfo.name, spans[i]->name, sizeof(stack.spaninfo.name));
		stack.spaninfo.alarms = spans[i]->alarms;		/* get alarm status */
		stack.spaninfo.bpvcount = spans[i]->bpvcount;	/* get BPV count */
		stack.spaninfo.rxlevel = spans[i]->rxlevel;	/* get rx level */
		stack.spaninfo.txlevel = spans[i]->txlevel;	/* get tx level */
		stack.spaninfo.crc4count = spans[i]->crc4count;	/* get CRC4 error count */
		stack.spaninfo.ebitcount = spans[i]->ebitcount;	/* get E-bit error count */
		stack.spaninfo.fascount = spans[i]->fascount;	/* get FAS error count */
		stack.spaninfo.irqmisses = spans[i]->irqmisses;	/* get IRQ miss count */
		stack.spaninfo.syncsrc = spans[i]->syncsrc;	/* get active sync source */
		stack.spaninfo.totalchans = spans[i]->channels;
		stack.spaninfo.numchans = 0;
		for (j = 0; j < spans[i]->channels; j++) {
			if (spans[i]->chans[j].sig)
				stack.spaninfo.numchans++;
		}
		stack.spaninfo.lbo = spans[i]->lbo;
		stack.spaninfo.lineconfig = spans[i]->lineconfig;
		stack.spaninfo.irq = spans[i]->irq;
		stack.spaninfo.linecompat = spans[i]->linecompat;
		dahdi_copy_string(stack.spaninfo.lboname, dahdi_lboname(spans[i]->lbo), sizeof(stack.spaninfo.lboname));
		if (spans[i]->manufacturer)
			dahdi_copy_string(stack.spaninfo.manufacturer, spans[i]->manufacturer,
				sizeof(stack.spaninfo.manufacturer));
		if (spans[i]->devicetype)
			dahdi_copy_string(stack.spaninfo.devicetype, spans[i]->devicetype, sizeof(stack.spaninfo.devicetype));
		dahdi_copy_string(stack.spaninfo.location, spans[i]->location, sizeof(stack.spaninfo.location));
		if (spans[i]->spantype)
			dahdi_copy_string(stack.spaninfo.spantype, spans[i]->spantype, sizeof(stack.spaninfo.spantype));
		
		if (copy_to_user((struct dahdi_spaninfo *) data, &stack.spaninfo, size_to_copy))
			return -EFAULT;
		break;
	case DAHDI_CHANDIAG:
		get_user(j, (int *)data); /* get channel number from user */
		/* make sure its a valid channel number */
		if ((j < 1) || (j >= maxchans))
			return -EINVAL;
		/* if channel not mapped, not there */
		if (!chans[j]) 
			return -EINVAL;

		if (!(mychan = kmalloc(sizeof(*mychan), GFP_KERNEL)))
			return -ENOMEM;

		/* lock channel */
		spin_lock_irqsave(&chans[j]->lock, flags);
		/* make static copy of channel */
		memcpy(mychan, chans[j], sizeof(*mychan));
		/* release it. */
		spin_unlock_irqrestore(&chans[j]->lock, flags);

		module_printk(KERN_INFO, "Dump of DAHDI Channel %d (%s,%d,%d):\n\n",j,
			mychan->name,mychan->channo,mychan->chanpos);
		module_printk(KERN_INFO, "flags: %x hex, writechunk: %08lx, readchunk: %08lx\n",
			(unsigned int) mychan->flags, (long) mychan->writechunk, (long) mychan->readchunk);
		module_printk(KERN_INFO, "rxgain: %08lx, txgain: %08lx, gainalloc: %d\n",
			(long) mychan->rxgain, (long)mychan->txgain, mychan->gainalloc);
		module_printk(KERN_INFO, "span: %08lx, sig: %x hex, sigcap: %x hex\n",
			(long)mychan->span, mychan->sig, mychan->sigcap);
		module_printk(KERN_INFO, "inreadbuf: %d, outreadbuf: %d, inwritebuf: %d, outwritebuf: %d\n",
			mychan->inreadbuf, mychan->outreadbuf, mychan->inwritebuf, mychan->outwritebuf);
		module_printk(KERN_INFO, "blocksize: %d, numbufs: %d, txbufpolicy: %d, txbufpolicy: %d\n",
			mychan->blocksize, mychan->numbufs, mychan->txbufpolicy, mychan->rxbufpolicy);
		module_printk(KERN_INFO, "txdisable: %d, rxdisable: %d, iomask: %d\n",
			mychan->txdisable, mychan->rxdisable, mychan->iomask);
		module_printk(KERN_INFO, "curzone: %08lx, tonezone: %d, curtone: %08lx, tonep: %d\n",
			(long) mychan->curzone, mychan->tonezone, (long) mychan->curtone, mychan->tonep);
		module_printk(KERN_INFO, "digitmode: %d, txdialbuf: %s, dialing: %d, aftdialtimer: %d, cadpos. %d\n",
			mychan->digitmode, mychan->txdialbuf, mychan->dialing,
				mychan->afterdialingtimer, mychan->cadencepos);
		module_printk(KERN_INFO, "confna: %d, confn: %d, confmode: %d, confmute: %d\n",
			mychan->confna, mychan->_confn, mychan->confmode, mychan->confmute);
		module_printk(KERN_INFO, "ec: %08lx, echocancel: %d, deflaw: %d, xlaw: %08lx\n",
			(long) mychan->ec_state, mychan->echocancel, mychan->deflaw, (long) mychan->xlaw);
		module_printk(KERN_INFO, "echostate: %02x, echotimer: %d, echolastupdate: %d\n",
			(int) mychan->echostate, mychan->echotimer, mychan->echolastupdate);
		module_printk(KERN_INFO, "itimer: %d, otimer: %d, ringdebtimer: %d\n\n",
			mychan->itimer, mychan->otimer, mychan->ringdebtimer);
#if 0
		if (mychan->ec_state) {
			int x;
			/* Dump the echo canceller parameters */
			for (x=0;x<mychan->ec_state->taps;x++) {
				module_printk(KERN_INFO, "tap %d: %d\n", x, mychan->ec_state->fir_taps[x]);
			}
		}
#endif
		kfree(mychan);
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static int (*dahdi_dynamic_ioctl)(unsigned int cmd, unsigned long data);

void dahdi_set_dynamic_ioctl(int (*func)(unsigned int cmd, unsigned long data)) 
{
	dahdi_dynamic_ioctl = func;
}

static int (*dahdi_hpec_ioctl)(unsigned int cmd, unsigned long data);

void dahdi_set_hpec_ioctl(int (*func)(unsigned int cmd, unsigned long data)) 
{
	dahdi_hpec_ioctl = func;
}

static void recalc_slaves(struct dahdi_chan *chan)
{
	int x;
	struct dahdi_chan *last = chan;

	/* Makes no sense if you don't have a span */
	if (!chan->span)
		return;

#ifdef CONFIG_DAHDI_DEBUG
	module_printk(KERN_NOTICE, "Recalculating slaves on %s\n", chan->name);
#endif

	/* Link all slaves appropriately */
	for (x=chan->chanpos;x<chan->span->channels;x++)
		if (chan->span->chans[x].master == chan) {
#ifdef CONFIG_DAHDI_DEBUG
			module_printk(KERN_NOTICE, "Channel %s, slave to %s, last is %s, its next will be %d\n", 
				      chan->span->chans[x].name, chan->name, last->name, x);
#endif
			last->nextslave = x;
			last = &chan->span->chans[x];
		}
	/* Terminate list */
	last->nextslave = 0;
#ifdef CONFIG_DAHDI_DEBUG
	module_printk(KERN_NOTICE, "Done Recalculating slaves on %s (last is %s)\n", chan->name, last->name);
#endif
}

static int dahdi_ctl_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data)
{
	/* I/O CTL's for control interface */
	int i,j;
	int sigcap;
	int res = 0;
	int x,y;
	struct dahdi_chan *newmaster;
	unsigned long flags;
	int rv;
	switch(cmd) {
	case DAHDI_INDIRECT:
	{
		struct dahdi_indirect_data ind;

		if (copy_from_user(&ind, (struct dahdi_indirect_data *)data, sizeof(ind)))
			return -EFAULT;
		VALID_CHANNEL(ind.chan);
		return dahdi_chan_ioctl(inode, file, ind.op, (unsigned long) ind.data, ind.chan);
	}
	case DAHDI_SPANCONFIG:
	{
		struct dahdi_lineconfig lc;

		if (copy_from_user(&lc, (struct dahdi_lineconfig *)data, sizeof(lc)))
			return -EFAULT;
		VALID_SPAN(lc.span);
		if ((lc.lineconfig & 0x07f0 & spans[lc.span]->linecompat) != (lc.lineconfig & 0x07f0))
			return -EINVAL;
		if (spans[lc.span]->spanconfig) {
			spans[lc.span]->lineconfig = lc.lineconfig;
			spans[lc.span]->lbo = lc.lbo;
			spans[lc.span]->txlevel = lc.lbo;
			spans[lc.span]->rxlevel = 0;

			return spans[lc.span]->spanconfig(spans[lc.span], &lc);
		}
		return 0;
	}
	case DAHDI_STARTUP:
		CHECK_VALID_SPAN(j);
		if (spans[j]->flags & DAHDI_FLAG_RUNNING)
			return 0;
		if (spans[j]->startup)
			res = spans[j]->startup(spans[j]);
		if (!res) {
			/* Mark as running and hangup any channels */
			spans[j]->flags |= DAHDI_FLAG_RUNNING;
			for (x=0;x<spans[j]->channels;x++) {
				y = dahdi_q_sig(&spans[j]->chans[x]) & 0xff;
				if (y >= 0) spans[j]->chans[x].rxsig = (unsigned char)y;
				spin_lock_irqsave(&spans[j]->chans[x].lock, flags);
				dahdi_hangup(&spans[j]->chans[x]);
				spin_unlock_irqrestore(&spans[j]->chans[x].lock, flags);
				spans[j]->chans[x].rxhooksig = DAHDI_RXSIG_INITIAL;
			}
		}
		return 0;
	case DAHDI_SHUTDOWN:
		CHECK_VALID_SPAN(j);
		if (spans[j]->shutdown)
			res =  spans[j]->shutdown(spans[j]);
		spans[j]->flags &= ~DAHDI_FLAG_RUNNING;
		return 0;
	case DAHDI_ATTACH_ECHOCAN:
	{
		struct dahdi_attach_echocan ae;
		const struct dahdi_echocan *new = NULL, *old;

		if (copy_from_user(&ae, (struct dahdi_attach_echocan *) data, sizeof(ae))) {
			return -EFAULT;
		}

		VALID_CHANNEL(ae.chan);

		if (ae.echocan[0]) {
			if (!(new = find_echocan(ae.echocan))) {
				return -EINVAL;
			}
		}

		spin_lock_irqsave(&chans[ae.chan]->lock, flags);
		old = chans[ae.chan]->ec_factory;
		chans[ae.chan]->ec_factory = new;
		spin_unlock_irqrestore(&chans[ae.chan]->lock, flags);

		if (old) {
			release_echocan(old);
		}

		break;
	}
	case DAHDI_CHANCONFIG:
	{
		struct dahdi_chanconfig ch;

		if (copy_from_user(&ch, (struct dahdi_chanconfig *)data, sizeof(ch)))
			return -EFAULT;
		VALID_CHANNEL(ch.chan);
		if (ch.sigtype == DAHDI_SIG_SLAVE) {
			/* We have to use the master's sigtype */
			if ((ch.master < 1) || (ch.master >= DAHDI_MAX_CHANNELS))
				return -EINVAL;
			if (!chans[ch.master])
				return -EINVAL;
			ch.sigtype = chans[ch.master]->sig;
			newmaster = chans[ch.master];
		} else if ((ch.sigtype & __DAHDI_SIG_DACS) == __DAHDI_SIG_DACS) {
			newmaster = chans[ch.chan];
			if ((ch.idlebits < 1) || (ch.idlebits >= DAHDI_MAX_CHANNELS))
				return -EINVAL;
			if (!chans[ch.idlebits])
				return -EINVAL;
		} else {
			newmaster = chans[ch.chan];
		}
		spin_lock_irqsave(&chans[ch.chan]->lock, flags);
#ifdef CONFIG_DAHDI_NET
		if (chans[ch.chan]->flags & DAHDI_FLAG_NETDEV) {
			if (ztchan_to_dev(chans[ch.chan])->flags & IFF_UP) {
				spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);
				module_printk(KERN_WARNING, "Can't switch HDLC net mode on channel %s, since current interface is up\n", chans[ch.chan]->name);
				return -EBUSY;
			}
			spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);
			unregister_hdlc_device(chans[ch.chan]->hdlcnetdev->netdev);
			spin_lock_irqsave(&chans[ch.chan]->lock, flags);
			free_netdev(chans[ch.chan]->hdlcnetdev->netdev);
			kfree(chans[ch.chan]->hdlcnetdev);
			chans[ch.chan]->hdlcnetdev = NULL;
			chans[ch.chan]->flags &= ~DAHDI_FLAG_NETDEV;
		}
#else
		if (ch.sigtype == DAHDI_SIG_HDLCNET) {
			spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);
			module_printk(KERN_WARNING, "DAHDI networking not supported by this build.\n");
			return -ENOSYS;
		}
#endif			
		sigcap = chans[ch.chan]->sigcap;
		/* If they support clear channel, then they support the HDLC and such through
		   us.  */
		if (sigcap & DAHDI_SIG_CLEAR) 
			sigcap |= (DAHDI_SIG_HDLCRAW | DAHDI_SIG_HDLCFCS | DAHDI_SIG_HDLCNET | DAHDI_SIG_DACS);
		
		if ((sigcap & ch.sigtype) != ch.sigtype)
			res = -EINVAL;	
		
		if (!res && chans[ch.chan]->span->chanconfig)
			res = chans[ch.chan]->span->chanconfig(chans[ch.chan], ch.sigtype);

		if (chans[ch.chan]->master) {
			/* Clear the master channel */
			recalc_slaves(chans[ch.chan]->master);
			chans[ch.chan]->nextslave = 0;
		}

		if (!res) {
			chans[ch.chan]->sig = ch.sigtype;
			if (chans[ch.chan]->sig == DAHDI_SIG_CAS)
				chans[ch.chan]->idlebits = ch.idlebits;
			else
				chans[ch.chan]->idlebits = 0;
			if ((ch.sigtype & DAHDI_SIG_CLEAR) == DAHDI_SIG_CLEAR) {
				/* Set clear channel flag if appropriate */
				chans[ch.chan]->flags &= ~DAHDI_FLAG_AUDIO;
				chans[ch.chan]->flags |= DAHDI_FLAG_CLEAR;
			} else {
				/* Set audio flag and not clear channel otherwise */
				chans[ch.chan]->flags |= DAHDI_FLAG_AUDIO;
				chans[ch.chan]->flags &= ~DAHDI_FLAG_CLEAR;
			}
			if ((ch.sigtype & DAHDI_SIG_HDLCRAW) == DAHDI_SIG_HDLCRAW) {
				/* Set the HDLC flag */
				chans[ch.chan]->flags |= DAHDI_FLAG_HDLC;
			} else {
				/* Clear the HDLC flag */
				chans[ch.chan]->flags &= ~DAHDI_FLAG_HDLC;
			}
			if ((ch.sigtype & DAHDI_SIG_HDLCFCS) == DAHDI_SIG_HDLCFCS) {
				/* Set FCS to be calculated if appropriate */
				chans[ch.chan]->flags |= DAHDI_FLAG_FCS;
			} else {
				/* Clear FCS flag */
				chans[ch.chan]->flags &= ~DAHDI_FLAG_FCS;
			}
			if ((ch.sigtype & __DAHDI_SIG_DACS) == __DAHDI_SIG_DACS) {
				/* Setup conference properly */
				chans[ch.chan]->confmode = DAHDI_CONF_DIGITALMON;
				chans[ch.chan]->confna = ch.idlebits;
				if (chans[ch.chan]->span && 
				    chans[ch.chan]->span->dacs && 
				    chans[ch.idlebits] && 
				    chans[ch.chan]->span && 
				    (chans[ch.chan]->span->dacs == chans[ch.idlebits]->span->dacs)) 
					chans[ch.chan]->span->dacs(chans[ch.chan], chans[ch.idlebits]);
			} else if (chans[ch.chan]->span && chans[ch.chan]->span->dacs) {
				chans[ch.chan]->span->dacs(chans[ch.chan], NULL);
			}
			chans[ch.chan]->master = newmaster;
			/* Note new slave if we are not our own master */
			if (newmaster != chans[ch.chan]) {
				recalc_slaves(chans[ch.chan]->master);
			}
			if ((ch.sigtype & DAHDI_SIG_HARDHDLC) == DAHDI_SIG_HARDHDLC) {
				chans[ch.chan]->flags &= ~DAHDI_FLAG_FCS;
				chans[ch.chan]->flags &= ~DAHDI_FLAG_HDLC;
				chans[ch.chan]->flags |= DAHDI_FLAG_NOSTDTXRX;
			} else {
				chans[ch.chan]->flags &= ~DAHDI_FLAG_NOSTDTXRX;
			}

			if ((ch.sigtype & DAHDI_SIG_MTP2) == DAHDI_SIG_MTP2)
				chans[ch.chan]->flags |= DAHDI_FLAG_MTP2;
			else
				chans[ch.chan]->flags &= ~DAHDI_FLAG_MTP2;
		}
#ifdef CONFIG_DAHDI_NET
		if (!res && 
		    (newmaster == chans[ch.chan]) && 
		    (chans[ch.chan]->sig == DAHDI_SIG_HDLCNET)) {
			chans[ch.chan]->hdlcnetdev = dahdi_hdlc_alloc();
			if (chans[ch.chan]->hdlcnetdev) {
/*				struct hdlc_device *hdlc = chans[ch.chan]->hdlcnetdev;
				struct net_device *d = hdlc_to_dev(hdlc); mmm...get it right later --byg */

				chans[ch.chan]->hdlcnetdev->netdev = alloc_hdlcdev(chans[ch.chan]->hdlcnetdev);
				if (chans[ch.chan]->hdlcnetdev->netdev) {
					chans[ch.chan]->hdlcnetdev->chan = chans[ch.chan];
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
					SET_MODULE_OWNER(chans[ch.chan]->hdlcnetdev->netdev);
#endif
					chans[ch.chan]->hdlcnetdev->netdev->irq = chans[ch.chan]->span->irq;
					chans[ch.chan]->hdlcnetdev->netdev->tx_queue_len = 50;
					chans[ch.chan]->hdlcnetdev->netdev->do_ioctl = dahdi_net_ioctl;
					chans[ch.chan]->hdlcnetdev->netdev->open = dahdi_net_open;
					chans[ch.chan]->hdlcnetdev->netdev->stop = dahdi_net_stop;
					dev_to_hdlc(chans[ch.chan]->hdlcnetdev->netdev)->attach = dahdi_net_attach;
					dev_to_hdlc(chans[ch.chan]->hdlcnetdev->netdev)->xmit = dahdi_xmit;
					spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);
					/* Briefly restore interrupts while we register the device */
					res = dahdi_register_hdlc_device(chans[ch.chan]->hdlcnetdev->netdev, ch.netdev_name);
					spin_lock_irqsave(&chans[ch.chan]->lock, flags);
				} else {
					module_printk(KERN_NOTICE, "Unable to allocate hdlc: *shrug*\n");
					res = -1;
				}
				if (!res)
					chans[ch.chan]->flags |= DAHDI_FLAG_NETDEV;
			} else {
				module_printk(KERN_NOTICE, "Unable to allocate netdev: out of memory\n");
				res = -1;
			}
		}
#endif			
		if ((chans[ch.chan]->sig == DAHDI_SIG_HDLCNET) && 
		    (chans[ch.chan] == newmaster) &&
		    !(chans[ch.chan]->flags & DAHDI_FLAG_NETDEV))
			module_printk(KERN_NOTICE, "Unable to register HDLC device for channel %s\n", chans[ch.chan]->name);
		if (!res) {
			/* Setup default law */
			chans[ch.chan]->deflaw = ch.deflaw;
			/* Copy back any modified settings */
			spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);
			if (copy_to_user((struct dahdi_chanconfig *)data, &ch, sizeof(ch)))
				return -EFAULT;
			spin_lock_irqsave(&chans[ch.chan]->lock, flags);
			/* And hangup */
			dahdi_hangup(chans[ch.chan]);
			y = dahdi_q_sig(chans[ch.chan]) & 0xff;
			if (y >= 0)
				chans[ch.chan]->rxsig = (unsigned char) y;
			chans[ch.chan]->rxhooksig = DAHDI_RXSIG_INITIAL;
		}
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "Configured channel %s, flags %04x, sig %04x\n", chans[ch.chan]->name, chans[ch.chan]->flags, chans[ch.chan]->sig);
#endif			
		spin_unlock_irqrestore(&chans[ch.chan]->lock, flags);

		return res;
	}
	case DAHDI_SFCONFIG:
	{
		struct dahdi_sfconfig sf;

		if (copy_from_user(&sf, (struct dahdi_chanconfig *)data, sizeof(sf)))
			return -EFAULT;
		VALID_CHANNEL(sf.chan);
		if (chans[sf.chan]->sig != DAHDI_SIG_SF) return -EINVAL;
		spin_lock_irqsave(&chans[sf.chan]->lock, flags);
		chans[sf.chan]->rxp1 = sf.rxp1;
		chans[sf.chan]->rxp2 = sf.rxp2;
		chans[sf.chan]->rxp3 = sf.rxp3;
		chans[sf.chan]->txtone = sf.txtone;
		chans[sf.chan]->tx_v2 = sf.tx_v2;
		chans[sf.chan]->tx_v3 = sf.tx_v3;
		chans[sf.chan]->toneflags = sf.toneflag;
		if (sf.txtone) /* if set to make tone for tx */
		{
			if ((chans[sf.chan]->txhooksig && !(sf.toneflag & DAHDI_REVERSE_TXTONE)) ||
			 ((!chans[sf.chan]->txhooksig) && (sf.toneflag & DAHDI_REVERSE_TXTONE))) 
			{
				set_txtone(chans[sf.chan],sf.txtone,sf.tx_v2,sf.tx_v3);
			}
			else
			{
				set_txtone(chans[sf.chan],0,0,0);
			}
		}
		spin_unlock_irqrestore(&chans[sf.chan]->lock, flags);
		return res;
	}
	case DAHDI_DEFAULTZONE:
		if (get_user(j,(int *)data))
			return -EFAULT;
		return dahdi_set_default_zone(j);
	case DAHDI_LOADZONE:
		return ioctl_load_zone(data);
	case DAHDI_FREEZONE:
		get_user(j, (int *) data);
		return free_tone_zone(j);
	case DAHDI_SET_DIALPARAMS:
	{
		struct dahdi_dialparams tdp;

		if (copy_from_user(&tdp, (struct dahdi_dialparams *) data, sizeof(tdp)))
			return -EFAULT;

		if ((tdp.dtmf_tonelen <= 4000) || (tdp.dtmf_tonelen >= 10)) {
			global_dialparams.dtmf_tonelen = tdp.dtmf_tonelen;
		}
		if ((tdp.mfv1_tonelen <= 4000) || (tdp.mfv1_tonelen >= 10)) {
			global_dialparams.mfv1_tonelen = tdp.mfv1_tonelen;
		}
		if ((tdp.mfr2_tonelen <= 4000) || (tdp.mfr2_tonelen >= 10)) {
			global_dialparams.mfr2_tonelen = tdp.mfr2_tonelen;
		}

		/* update the lengths in all currently loaded zones */
		write_lock(&zone_lock);
		for (j = 0; j < sizeof(tone_zones) / sizeof(tone_zones[0]); j++) {
			struct dahdi_zone *z = tone_zones[j];

			if (!z)
				continue;

			for (i = 0; i < sizeof(z->dtmf) / sizeof(z->dtmf[0]); i++) {
				z->dtmf[i].tonesamples = global_dialparams.dtmf_tonelen * DAHDI_CHUNKSIZE;
			}

			/* for MFR1, we only adjust the length of the digits */
			for (i = DAHDI_TONE_MFR1_0; i <= DAHDI_TONE_MFR1_9; i++) {
				z->mfr1[i - DAHDI_TONE_MFR1_BASE].tonesamples = global_dialparams.mfv1_tonelen * DAHDI_CHUNKSIZE;
			}

			for (i = 0; i < sizeof(z->mfr2_fwd) / sizeof(z->mfr2_fwd[0]); i++) {
				z->mfr2_fwd[i].tonesamples = global_dialparams.mfr2_tonelen * DAHDI_CHUNKSIZE;
			}

			for (i = 0; i < sizeof(z->mfr2_rev) / sizeof(z->mfr2_rev[0]); i++) {
				z->mfr2_rev[i].tonesamples = global_dialparams.mfr2_tonelen * DAHDI_CHUNKSIZE;
			}
		}
		write_unlock(&zone_lock);

		dtmf_silence.tonesamples = global_dialparams.dtmf_tonelen * DAHDI_CHUNKSIZE;
		mfr1_silence.tonesamples = global_dialparams.mfv1_tonelen * DAHDI_CHUNKSIZE;
		mfr2_silence.tonesamples = global_dialparams.mfr2_tonelen * DAHDI_CHUNKSIZE;

		break;
	}
	case DAHDI_GET_DIALPARAMS:
	{
		struct dahdi_dialparams tdp;

		tdp = global_dialparams;
		if (copy_to_user((struct dahdi_dialparams *) data, &tdp, sizeof(tdp)))
			return -EFAULT;
		break;
	}
	case DAHDI_GETVERSION:
	{
		struct dahdi_versioninfo vi;
		struct echocan *cur;
		size_t space = sizeof(vi.echo_canceller) - 1;

		memset(&vi, 0, sizeof(vi));
		dahdi_copy_string(vi.version, DAHDI_VERSION, sizeof(vi.version));
		read_lock(&echocan_list_lock);
		list_for_each_entry(cur, &echocan_list, list) {
			strncat(vi.echo_canceller + strlen(vi.echo_canceller), cur->ec->name, space);
			space -= strlen(cur->ec->name);
			if (space < 1) {
				break;
			}
			if (cur->list.next && (cur->list.next != &echocan_list)) {
				strncat(vi.echo_canceller + strlen(vi.echo_canceller), ", ", space);
				space -= 2;
				if (space < 1) {
					break;
				}
			}
		}
		read_unlock(&echocan_list_lock);
		if (copy_to_user((struct dahdi_versioninfo *) data, &vi, sizeof(vi)))
			return -EFAULT;
		break;
	}
	case DAHDI_MAINT:  /* do maintenance stuff */
	{
		struct dahdi_maintinfo maint;
		  /* get struct from user */
		if (copy_from_user(&maint,(struct dahdi_maintinfo *) data, sizeof(maint)))
			return -EFAULT;
		/* must be valid span number */
		if ((maint.spanno < 1) || (maint.spanno > DAHDI_MAX_SPANS) || (!spans[maint.spanno]))
			return -EINVAL;
		if (!spans[maint.spanno]->maint)
			return -ENOSYS;
		spin_lock_irqsave(&spans[maint.spanno]->lock, flags);
		  /* save current maint state */
		i = spans[maint.spanno]->maintstat;
		  /* set maint mode */
		spans[maint.spanno]->maintstat = maint.command;
		switch(maint.command) {
		case DAHDI_MAINT_NONE:
		case DAHDI_MAINT_LOCALLOOP:
		case DAHDI_MAINT_REMOTELOOP:
			/* if same, ignore it */
			if (i == maint.command) break;
			rv = spans[maint.spanno]->maint(spans[maint.spanno], maint.command);
			spin_unlock_irqrestore(&spans[maint.spanno]->lock, flags);
			if (rv) return rv;
			spin_lock_irqsave(&spans[maint.spanno]->lock, flags);
			break;
		case DAHDI_MAINT_LOOPUP:
		case DAHDI_MAINT_LOOPDOWN:
			spans[maint.spanno]->mainttimer = DAHDI_LOOPCODE_TIME * DAHDI_CHUNKSIZE;
			rv = spans[maint.spanno]->maint(spans[maint.spanno], maint.command);
			spin_unlock_irqrestore(&spans[maint.spanno]->lock, flags);
			if (rv) return rv;
			rv = schluffen(&spans[maint.spanno]->maintq);
			if (rv) return rv;
			spin_lock_irqsave(&spans[maint.spanno]->lock, flags);
			break;
		default:
			module_printk(KERN_NOTICE, "Unknown maintenance event: %d\n", maint.command);
		}
		dahdi_alarm_notify(spans[maint.spanno]);  /* process alarm-related events */
		spin_unlock_irqrestore(&spans[maint.spanno]->lock, flags);
		break;
	}
	case DAHDI_DYNAMIC_CREATE:
	case DAHDI_DYNAMIC_DESTROY:
		if (dahdi_dynamic_ioctl) {
			return dahdi_dynamic_ioctl(cmd, data);
		} else {
			request_module("dahdi_dynamic");
			if (dahdi_dynamic_ioctl)
				return dahdi_dynamic_ioctl(cmd, data);
		}
		return -ENOSYS;
	case DAHDI_EC_LICENSE_CHALLENGE:
	case DAHDI_EC_LICENSE_RESPONSE:
		if (dahdi_hpec_ioctl) {
			return dahdi_hpec_ioctl(cmd, data);
		} else {
			request_module("dahdi_echocan_hpec");
			if (dahdi_hpec_ioctl)
				return dahdi_hpec_ioctl(cmd, data);
		}
		return -ENOSYS;
	default:
		return dahdi_common_ioctl(inode, file, cmd, data, 0);
	}
	return 0;
}

static int ioctl_dahdi_dial(struct dahdi_chan *chan, unsigned long data)
{
	struct dahdi_dialoperation *tdo;
	unsigned long flags;
	char *s;
	int rv;

	tdo = kmalloc(sizeof(*tdo), GFP_KERNEL);

	if (!tdo)
		return -ENOMEM;

	if (copy_from_user(tdo, (struct dahdi_dialoperation *)data, sizeof(*tdo)))
		return -EFAULT;
	rv = 0;
	/* Force proper NULL termination and uppercase entry */
	tdo->dialstr[DAHDI_MAX_DTMF_BUF - 1] = '\0';
	for (s = tdo->dialstr; *s; s++)
		*s = toupper(*s);
	spin_lock_irqsave(&chan->lock, flags);
	if (!chan->curzone) {
		spin_unlock_irqrestore(&chan->lock, flags);
		/* The tone zones are loaded by ztcfg from /etc/dahdi.conf */
		module_printk(KERN_WARNING, "Cannot dial until a tone zone is loaded.\n");
		return -ENODATA;
	}
	switch (tdo->op) {
	case DAHDI_DIAL_OP_CANCEL:
		chan->curtone = NULL;
		chan->dialing = 0;
		chan->txdialbuf[0] = '\0';
		chan->tonep = 0;
		chan->pdialcount = 0;
		break;
	case DAHDI_DIAL_OP_REPLACE:
		strcpy(chan->txdialbuf, tdo->dialstr);
		chan->dialing = 1;
		__do_dtmf(chan);
		break;
	case DAHDI_DIAL_OP_APPEND:
		if (strlen(tdo->dialstr) + strlen(chan->txdialbuf) >= (DAHDI_MAX_DTMF_BUF - 1)) {
			rv = -EBUSY;
			break;
		}
		dahdi_copy_string(chan->txdialbuf + strlen(chan->txdialbuf), tdo->dialstr, DAHDI_MAX_DTMF_BUF - strlen(chan->txdialbuf));
		if (!chan->dialing) {
			chan->dialing = 1;
			__do_dtmf(chan);
		}
		break;
	default:
		rv = -EINVAL;
	}
	spin_unlock_irqrestore(&chan->lock, flags);
	return rv;
}

static int dahdi_chanandpseudo_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data, int unit)
{
	struct dahdi_chan *chan = chans[unit];
	union {
		struct dahdi_bufferinfo bi;
		struct dahdi_confinfo conf;
		struct dahdi_ring_cadence cad;
	} stack;
	unsigned long flags, flagso;
	int i, j, k, rv;
	int ret, c;
	
	if (!chan)
		return -EINVAL;
	switch(cmd) {
	case DAHDI_DIALING:
		spin_lock_irqsave(&chan->lock, flags);
		j = chan->dialing;
		spin_unlock_irqrestore(&chan->lock, flags);
		if (copy_to_user((int *)data,&j,sizeof(int)))
			return -EFAULT;
		return 0;
	case DAHDI_DIAL:
		return ioctl_dahdi_dial(chan, data);
	case DAHDI_GET_BUFINFO:
		stack.bi.rxbufpolicy = chan->rxbufpolicy;
		stack.bi.txbufpolicy = chan->txbufpolicy;
		stack.bi.numbufs = chan->numbufs;
		stack.bi.bufsize = chan->blocksize;
		/* XXX FIXME! XXX */
		stack.bi.readbufs = -1;
		stack.bi.writebufs = -1;
		if (copy_to_user((struct dahdi_bufferinfo *)data, &stack.bi, sizeof(stack.bi)))
			return -EFAULT;
		break;
	case DAHDI_SET_BUFINFO:
		if (copy_from_user(&stack.bi, (struct dahdi_bufferinfo *)data, sizeof(stack.bi)))
			return -EFAULT;
		if (stack.bi.bufsize > DAHDI_MAX_BLOCKSIZE)
			return -EINVAL;
		if (stack.bi.bufsize < 16)
			return -EINVAL;
		if (stack.bi.bufsize * stack.bi.numbufs > DAHDI_MAX_BUF_SPACE)
			return -EINVAL;
		chan->rxbufpolicy = stack.bi.rxbufpolicy & 0x1;
		chan->txbufpolicy = stack.bi.txbufpolicy & 0x1;
		if ((rv = dahdi_reallocbufs(chan,  stack.bi.bufsize, stack.bi.numbufs)))
			return (rv);
		break;
	case DAHDI_GET_BLOCKSIZE:  /* get blocksize */
		put_user(chan->blocksize,(int *)data); /* return block size */
		break;
	case DAHDI_SET_BLOCKSIZE:  /* set blocksize */
		get_user(j,(int *)data);
		  /* cannot be larger than max amount */
		if (j > DAHDI_MAX_BLOCKSIZE) return(-EINVAL);
		  /* cannot be less then 16 */
		if (j < 16) return(-EINVAL);
		  /* allocate a single kernel buffer which we then
		     sub divide into four pieces */
		if ((rv = dahdi_reallocbufs(chan, j, chan->numbufs)))
			return (rv);
		break;
	case DAHDI_FLUSH:  /* flush input buffer, output buffer, and/or event queue */
		get_user(i,(int *)data);  /* get param */
		spin_lock_irqsave(&chan->lock, flags);
		if (i & DAHDI_FLUSH_READ)  /* if for read (input) */
		   {
			  /* initialize read buffers and pointers */
			chan->inreadbuf = 0;
			chan->outreadbuf = -1;
			for (j=0;j<chan->numbufs;j++) {
				/* Do we need this? */
				chan->readn[j] = 0;
				chan->readidx[j] = 0;
			}
			wake_up_interruptible(&chan->readbufq);  /* wake_up_interruptible waiting on read */
			wake_up_interruptible(&chan->sel); /* wake_up_interruptible waiting on select */
		   }
		if (i & DAHDI_FLUSH_WRITE) /* if for write (output) */
		   {
			  /* initialize write buffers and pointers */
			chan->outwritebuf = -1;
			chan->inwritebuf = 0;
			for (j=0;j<chan->numbufs;j++) {
				/* Do we need this? */
				chan->writen[j] = 0;
				chan->writeidx[j] = 0;
			}
			wake_up_interruptible(&chan->writebufq); /* wake_up_interruptible waiting on write */
			wake_up_interruptible(&chan->sel);  /* wake_up_interruptible waiting on select */
			   /* if IO MUX wait on write empty, well, this
				certainly *did* empty the write */
			if (chan->iomask & DAHDI_IOMUX_WRITEEMPTY)
				wake_up_interruptible(&chan->eventbufq); /* wake_up_interruptible waiting on IOMUX */
		   }
		if (i & DAHDI_FLUSH_EVENT) /* if for events */
		   {
			   /* initialize the event pointers */
			chan->eventinidx = chan->eventoutidx = 0;
		   }
		spin_unlock_irqrestore(&chan->lock, flags);
		break;
	case DAHDI_SYNC:  /* wait for no tx */
		for(;;)  /* loop forever */
		   {
			spin_lock_irqsave(&chan->lock, flags);
			  /* Know if there is a write pending */
			i = (chan->outwritebuf > -1);
			spin_unlock_irqrestore(&chan->lock, flags);
			if (!i) break; /* skip if none */
			rv = schluffen(&chan->writebufq);
			if (rv) return(rv);
		   }
		break;
	case DAHDI_IOMUX: /* wait for something to happen */
		get_user(chan->iomask,(int*)data);  /* save mask */
		if (!chan->iomask) return(-EINVAL);  /* cant wait for nothing */
		for(;;)  /* loop forever */
		   {
			  /* has to have SOME mask */
			ret = 0;  /* start with empty return value */
			spin_lock_irqsave(&chan->lock, flags);
			  /* if looking for read */
			if (chan->iomask & DAHDI_IOMUX_READ)
			   {
				/* if read available */
				if ((chan->outreadbuf > -1)  && !chan->rxdisable)
					ret |= DAHDI_IOMUX_READ;
			   }
			  /* if looking for write avail */
			if (chan->iomask & DAHDI_IOMUX_WRITE)
			   {
				if (chan->inwritebuf > -1)
					ret |= DAHDI_IOMUX_WRITE;
			   }
			  /* if looking for write empty */
			if (chan->iomask & DAHDI_IOMUX_WRITEEMPTY)
			   {
				  /* if everything empty -- be sure the transmitter is enabled */
				chan->txdisable = 0;
				if (chan->outwritebuf < 0)
					ret |= DAHDI_IOMUX_WRITEEMPTY;
			   }
			  /* if looking for signalling event */
			if (chan->iomask & DAHDI_IOMUX_SIGEVENT)
			   {
				  /* if event */
				if (chan->eventinidx != chan->eventoutidx)
					ret |= DAHDI_IOMUX_SIGEVENT;
			   }
			spin_unlock_irqrestore(&chan->lock, flags);
			  /* if something to return, or not to wait */
			if (ret || (chan->iomask & DAHDI_IOMUX_NOWAIT))
			   {
				  /* set return value */
				put_user(ret,(int *)data);
				break; /* get out of loop */
			   }
			rv = schluffen(&chan->eventbufq);
			if (rv) return(rv);
		   }
		  /* clear IO MUX mask */
		chan->iomask = 0;
		break;
	case DAHDI_GETEVENT:  /* Get event on queue */
		  /* set up for no event */
		j = DAHDI_EVENT_NONE;
		spin_lock_irqsave(&chan->lock, flags);
		  /* if some event in queue */
		if (chan->eventinidx != chan->eventoutidx)
		   {
			j = chan->eventbuf[chan->eventoutidx++];
			  /* get the data, bump index */
			  /* if index overflow, set to beginning */
			if (chan->eventoutidx >= DAHDI_MAX_EVENTSIZE)
				chan->eventoutidx = 0;
		   }		
		spin_unlock_irqrestore(&chan->lock, flags);
		put_user(j,(int *)data);
		break;
	case DAHDI_CONFMUTE:  /* set confmute flag */
		get_user(j,(int *)data);  /* get conf # */
		if (!(chan->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		spin_lock_irqsave(&bigzaplock, flags);
		chan->confmute = j;
		spin_unlock_irqrestore(&bigzaplock, flags);
		break;
	case DAHDI_GETCONFMUTE:  /* get confmute flag */
		if (!(chan->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		j = chan->confmute;
		put_user(j,(int *)data);  /* get conf # */
		rv = 0;
		break;
	case DAHDI_SETTONEZONE:
		get_user(j, (int *) data);
		rv = set_tone_zone(chan, j);
		return rv;
	case DAHDI_GETTONEZONE:
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->curzone)
			j = chan->tonezone;
		spin_unlock_irqrestore(&chan->lock, flags);
		put_user(j, (int *) data);
		break;
	case DAHDI_SENDTONE:
		get_user(j,(int *)data);
		spin_lock_irqsave(&chan->lock, flags);
		rv = start_tone(chan, j);	
		spin_unlock_irqrestore(&chan->lock, flags);
		return rv;
	case DAHDI_GETCONF:  /* get conf stuff */
		if (copy_from_user(&stack.conf,(struct dahdi_confinfo *) data,sizeof(stack.conf)))
			return -EFAULT;
		i = stack.conf.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = chan->channo;
		  /* make sure channel number makes sense */
		if ((i < 0) || (i > DAHDI_MAX_CONF) || (!chans[i])) return(-EINVAL);
		if (!(chans[i]->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		stack.conf.chan = i;  /* get channel number */
		stack.conf.confno = chans[i]->confna;  /* get conference number */
		stack.conf.confmode = chans[i]->confmode; /* get conference mode */
		if (copy_to_user((struct dahdi_confinfo *) data,&stack.conf,sizeof(stack.conf)))
			return -EFAULT;
		break;
	case DAHDI_SETCONF:  /* set conf stuff */
		if (copy_from_user(&stack.conf,(struct dahdi_confinfo *) data,sizeof(stack.conf)))
			return -EFAULT;
		i = stack.conf.chan;  /* get channel no */
		   /* if zero, use current channel no */
		if (!i) i = chan->channo;
		  /* make sure channel number makes sense */
		if ((i < 1) || (i > DAHDI_MAX_CHANNELS) || (!chans[i])) return(-EINVAL);
		if (!(chans[i]->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL); 
		if ((stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORTX ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORBOTH ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_RX_PREECHO ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_TX_PREECHO ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORBOTH_PREECHO) {
			/* Monitor mode -- it's a channel */
			if ((stack.conf.confno < 0) || (stack.conf.confno >= DAHDI_MAX_CHANNELS) || !chans[stack.conf.confno]) return(-EINVAL);
		} else {
			  /* make sure conf number makes sense, too */
			if ((stack.conf.confno < -1) || (stack.conf.confno > DAHDI_MAX_CONF)) return(-EINVAL);
		}
			
		  /* if taking off of any conf, must have 0 mode */
		if ((!stack.conf.confno) && stack.conf.confmode) return(-EINVAL);
		  /* likewise if 0 mode must have no conf */
		if ((!stack.conf.confmode) && stack.conf.confno) return (-EINVAL);
		stack.conf.chan = i;  /* return with real channel # */
		spin_lock_irqsave(&bigzaplock, flagso);
		spin_lock_irqsave(&chan->lock, flags);
		if (stack.conf.confno == -1) 
			stack.conf.confno = dahdi_first_empty_conference();
		if ((stack.conf.confno < 1) && (stack.conf.confmode)) {
			/* No more empty conferences */
			spin_unlock_irqrestore(&chan->lock, flags);
			spin_unlock_irqrestore(&bigzaplock, flagso);
			return -EBUSY;
		}
		  /* if changing confs, clear last added info */
		if (stack.conf.confno != chans[i]->confna) {
			memset(chans[i]->conflast, 0, DAHDI_MAX_CHUNKSIZE);
			memset(chans[i]->conflast1, 0, DAHDI_MAX_CHUNKSIZE);
			memset(chans[i]->conflast2, 0, DAHDI_MAX_CHUNKSIZE);
		}
		j = chans[i]->confna;  /* save old conference number */
		chans[i]->confna = stack.conf.confno;   /* set conference number */
		chans[i]->confmode = stack.conf.confmode;  /* set conference mode */
		chans[i]->_confn = 0;		     /* Clear confn */
		dahdi_check_conf(j);
		dahdi_check_conf(stack.conf.confno);
		if (chans[i]->span && chans[i]->span->dacs) {
			if (((stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_DIGITALMON) &&
			    chans[stack.conf.confno]->span &&
			    chans[stack.conf.confno]->span->dacs == chans[i]->span->dacs &&
			    chans[i]->txgain == defgain &&
			    chans[i]->rxgain == defgain &&
			    chans[stack.conf.confno]->txgain == defgain &&
			    chans[stack.conf.confno]->rxgain == defgain) {
				chans[i]->span->dacs(chans[i], chans[stack.conf.confno]);
			} else {
				chans[i]->span->dacs(chans[i], NULL);
			}
		}
		/* if we are going onto a conf */
		if (stack.conf.confno &&
			((stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONF ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFANN ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFMON ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_CONFANNMON ||
			(stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_REALANDPSEUDO)) {
			/* Get alias */
			chans[i]->_confn = dahdi_get_conf_alias(stack.conf.confno);
		}

		if (chans[stack.conf.confno]) {
			if ((stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_RX_PREECHO ||
			    (stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITOR_TX_PREECHO ||
			    (stack.conf.confmode & DAHDI_CONF_MODE_MASK) == DAHDI_CONF_MONITORBOTH_PREECHO)
				chans[stack.conf.confno]->readchunkpreec = kmalloc(sizeof(*chans[stack.conf.confno]->readchunkpreec) * DAHDI_CHUNKSIZE, GFP_ATOMIC);
			else {
				if (chans[stack.conf.confno]->readchunkpreec) {
					kfree(chans[stack.conf.confno]->readchunkpreec);
					chans[stack.conf.confno]->readchunkpreec = NULL;
				}
			}
		}

		spin_unlock_irqrestore(&chan->lock, flags);
		spin_unlock_irqrestore(&bigzaplock, flagso);
		if (copy_to_user((struct dahdi_confinfo *) data,&stack.conf,sizeof(stack.conf)))
			return -EFAULT;
		break;
	case DAHDI_CONFLINK:  /* do conf link stuff */
		if (!(chan->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		if (copy_from_user(&stack.conf,(struct dahdi_confinfo *) data,sizeof(stack.conf)))
			return -EFAULT;
		  /* check sanity of arguments */
		if ((stack.conf.chan < 0) || (stack.conf.chan > DAHDI_MAX_CONF)) return(-EINVAL);
		if ((stack.conf.confno < 0) || (stack.conf.confno > DAHDI_MAX_CONF)) return(-EINVAL);
		  /* cant listen to self!! */
		if (stack.conf.chan && (stack.conf.chan == stack.conf.confno)) return(-EINVAL);
		spin_lock_irqsave(&bigzaplock, flagso);
		spin_lock_irqsave(&chan->lock, flags);
		  /* if to clear all links */
		if ((!stack.conf.chan) && (!stack.conf.confno))
		   {
			   /* clear all the links */
			memset(conf_links,0,sizeof(conf_links));
			recalc_maxlinks();
			spin_unlock_irqrestore(&chan->lock, flags);
			spin_unlock_irqrestore(&bigzaplock, flagso);
			break;
		   }
		rv = 0;  /* clear return value */
		/* look for already existant specified combination */
		for(i = 1; i <= DAHDI_MAX_CONF; i++)
		   {
			  /* if found, exit */
			if ((conf_links[i].src == stack.conf.chan) &&
				(conf_links[i].dst == stack.conf.confno)) break;
		   }
		if (i <= DAHDI_MAX_CONF) /* if found */
		   {
			if (!stack.conf.confmode) /* if to remove link */
			   {
				conf_links[i].src = conf_links[i].dst = 0;
			   }
			else /* if to add and already there, error */
			   {
				rv = -EEXIST;
			   }
		   }
		else  /* if not found */
		   {
			if (stack.conf.confmode) /* if to add link */
			   {
				/* look for empty location */
				for(i = 1; i <= DAHDI_MAX_CONF; i++)
				   {
					  /* if empty, exit loop */
					if ((!conf_links[i].src) &&
						 (!conf_links[i].dst)) break;
				   }
				   /* if empty spot found */
				if (i <= DAHDI_MAX_CONF)
				   {
					conf_links[i].src = stack.conf.chan;
					conf_links[i].dst = stack.conf.confno;
				   }
				else /* if no empties -- error */
				   {
					rv = -ENOSPC;
				   }
			   }
			else /* if to remove, and not found -- error */
			   {
				rv = -ENOENT;
			   }
		   }
		recalc_maxlinks();
		spin_unlock_irqrestore(&chan->lock, flags);
		spin_unlock_irqrestore(&bigzaplock, flagso);
		return(rv);
	case DAHDI_CONFDIAG:  /* output diagnostic info to console */
		if (!(chan->flags & DAHDI_FLAG_AUDIO)) return (-EINVAL);
		get_user(j,(int *)data);  /* get conf # */
 		  /* loop thru the interesting ones */
		for(i = ((j) ? j : 1); i <= ((j) ? j : DAHDI_MAX_CONF); i++)
		   {
			c = 0;
			for(k = 1; k < DAHDI_MAX_CHANNELS; k++)
			   {
				  /* skip if no pointer */
				if (!chans[k]) continue;
				  /* skip if not in this conf */
				if (chans[k]->confna != i) continue;
				if (!c) module_printk(KERN_NOTICE, "Conf #%d:\n",i);
				c = 1;
				module_printk(KERN_NOTICE, "chan %d, mode %x\n", k,chans[k]->confmode);
			   }
			rv = 0;
			for(k = 1; k <= DAHDI_MAX_CONF; k++)
			   {
				if (conf_links[k].dst == i)
				   {
					if (!c) module_printk(KERN_NOTICE, "Conf #%d:\n",i);
					c = 1;
					if (!rv) module_printk(KERN_NOTICE, "Snooping on:\n");
					rv = 1;
					module_printk(KERN_NOTICE, "conf %d\n",conf_links[k].src);
				   }
			   }
			if (c) module_printk(KERN_NOTICE, "\n");
		   }
		break;
	case DAHDI_CHANNO:  /* get channel number of stream */
		put_user(unit,(int *)data); /* return unit/channel number */
		break;
	case DAHDI_SETLAW:
		get_user(j, (int *)data);
		if ((j < 0) || (j > DAHDI_LAW_ALAW))
			return -EINVAL;
		dahdi_set_law(chan, j);
		break;
	case DAHDI_SETLINEAR:
		get_user(j, (int *)data);
		/* Makes no sense on non-audio channels */
		if (!(chan->flags & DAHDI_FLAG_AUDIO))
			return -EINVAL;

		if (j)
			chan->flags |= DAHDI_FLAG_LINEAR;
		else
			chan->flags &= ~DAHDI_FLAG_LINEAR;
		break;
	case DAHDI_SETCADENCE:
		if (data) {
			/* Use specific ring cadence */
			if (copy_from_user(&stack.cad, (struct dahdi_ring_cadence *)data, sizeof(stack.cad)))
				return -EFAULT;
			memcpy(chan->ringcadence, &stack.cad, sizeof(chan->ringcadence));
			chan->firstcadencepos = 0;
			/* Looking for negative ringing time indicating where to loop back into ringcadence */
			for (i=0; i<DAHDI_MAX_CADENCE; i+=2 ) {
				if (chan->ringcadence[i]<0) {
					chan->ringcadence[i] *= -1;
					chan->firstcadencepos = i;
					break;
				}
			}
		} else {
			/* Reset to default */
			chan->firstcadencepos = 0;
			if (chan->curzone) {
				memcpy(chan->ringcadence, chan->curzone->ringcadence, sizeof(chan->ringcadence));
				/* Looking for negative ringing time indicating where to loop back into ringcadence */
				for (i=0; i<DAHDI_MAX_CADENCE; i+=2 ) {
					if (chan->ringcadence[i]<0) {
						chan->ringcadence[i] *= -1;
						chan->firstcadencepos = i;
						break;
					}
				}
			} else {
				memset(chan->ringcadence, 0, sizeof(chan->ringcadence));
				chan->ringcadence[0] = chan->starttime;
				chan->ringcadence[1] = DAHDI_RINGOFFTIME;
			}
		}
		break;
	default:
		/* Check for common ioctl's and private ones */
		rv = dahdi_common_ioctl(inode, file, cmd, data, unit);
		/* if no span, just return with value */
		if (!chan->span) return rv;
		if ((rv == -ENOTTY) && chan->span->ioctl) 
			rv = chan->span->ioctl(chan, cmd, data);
		return rv;
		
	}
	return 0;
}

#ifdef CONFIG_DAHDI_PPP
/*
 * This is called at softirq (BH) level when there are calls
 * we need to make to the ppp_generic layer.  We do it this
 * way because the ppp_generic layer functions may not be called
 * at interrupt level.
 */
static void do_ppp_calls(unsigned long data)
{
	struct dahdi_chan *chan = (struct dahdi_chan *) data;
	struct sk_buff *skb;

	if (!chan->ppp)
		return;
	if (chan->do_ppp_wakeup) {
		chan->do_ppp_wakeup = 0;
		ppp_output_wakeup(chan->ppp);
	}
	while ((skb = skb_dequeue(&chan->ppp_rq)) != NULL)
		ppp_input(chan->ppp, skb);
	if (chan->do_ppp_error) {
		chan->do_ppp_error = 0;
		ppp_input_error(chan->ppp, 0);
	}
}
#endif

static int ioctl_echocancel(struct dahdi_chan *chan, struct dahdi_echocanparams *ecp, void *data)
{
	struct echo_can_state *ec = NULL, *ec_state;
	const struct dahdi_echocan *ec_current;
	struct dahdi_echocanparam *params;
	int ret;
	unsigned long flags;

	if (ecp->param_count > DAHDI_MAX_ECHOCANPARAMS)
		return -E2BIG;

	if (ecp->tap_length == 0) {
		/* disable mode, don't need to inspect params */
		spin_lock_irqsave(&chan->lock, flags);
		ec_state = chan->ec_state;
		chan->ec_state = NULL;
		ec_current = chan->ec_current;
		chan->ec_current = NULL;
		chan->echocancel = 0;
		chan->echostate = ECHO_STATE_IDLE;
		chan->echolastupdate = 0;
		chan->echotimer = 0;
		spin_unlock_irqrestore(&chan->lock, flags);
		if (ec_state) {
			ec_current->echo_can_free(ec_state);
			release_echocan(ec_current);
		}
		hw_echocancel_off(chan);

		return 0;
	}

	/* if parameters were supplied and this channel's span provides an echocan,
	   but not one that takes params, then we must punt here and return an error */
	if (ecp->param_count && chan->span && chan->span->echocan &&
	    !chan->span->echocan_with_params)
		return -EINVAL;
	
	params = kmalloc(sizeof(params[0]) * DAHDI_MAX_ECHOCANPARAMS, GFP_KERNEL);
	
	if (!params)
		return -ENOMEM;

	/* enable mode, need the params */
	
	if (copy_from_user(params, (struct dahdi_echocanparam *) data, sizeof(params[0]) * ecp->param_count)) {
		ret = -EFAULT;
		goto exit_with_free;
	}
	
	spin_lock_irqsave(&chan->lock, flags);
	ec_state = chan->ec_state;
	chan->ec_state = NULL;
	ec_current = chan->ec_current;
	chan->ec_current = NULL;
	spin_unlock_irqrestore(&chan->lock, flags);
	if (ec_state) {
		ec_current->echo_can_free(ec_state);
		release_echocan(ec_current);
	}
	
	ret = -ENODEV;
	
	/* attempt to use the span's echo canceler; fall back to built-in
	   if it fails (but not if an error occurs) */
	if (chan->span) {
		if (chan->span->echocan_with_params)
			ret = chan->span->echocan_with_params(chan, ecp, params);
		else if (chan->span->echocan)
			ret = chan->span->echocan(chan, ecp->tap_length);
	}
	
	if ((ret == -ENODEV) && chan->ec_factory) {
		const struct dahdi_echocan *ec_current;

		switch (ecp->tap_length) {
		case 32:
		case 64:
		case 128:
		case 256:
		case 512:
		case 1024:
			break;
		default:
			ecp->tap_length = deftaps;
		}
		
		/* try to get another reference to the module providing
		   this channel's echo canceler */
		if (!try_module_get(chan->ec_factory->owner)) {
			module_printk(KERN_ERR, "Cannot get a reference to the '%s' echo canceler\n", chan->ec_factory->name);
			goto exit_with_free;
		}

		/* got the reference, copy the pointer and use it for making
		   an echo canceler instance if possible */
		ec_current = chan->ec_current;

		if ((ret = ec_current->echo_can_create(ecp, params, &ec))) {
			release_echocan(ec_current);

			goto exit_with_free;
		}
		
		spin_lock_irqsave(&chan->lock, flags);
		chan->echocancel = ecp->tap_length;
		chan->ec_current = ec_current;
		chan->ec_state = ec;
		chan->echostate = ECHO_STATE_IDLE;
		chan->echolastupdate = 0;
		chan->echotimer = 0;
		echo_can_disable_detector_init(&chan->txecdis);
		echo_can_disable_detector_init(&chan->rxecdis);
		spin_unlock_irqrestore(&chan->lock, flags);
	}

exit_with_free:
	kfree(params);

	return ret;
}

static int dahdi_chan_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data, int unit)
{
	struct dahdi_chan *chan = chans[unit];
	unsigned long flags;
	int j, rv;
	int ret;
	int oldconf;
	void *rxgain=NULL;

	if (!chan)
		return -ENOSYS;

	switch(cmd) {
	case DAHDI_SIGFREEZE:
		get_user(j, (int *)data);
		spin_lock_irqsave(&chan->lock, flags);
		if (j) {
			chan->flags |= DAHDI_FLAG_SIGFREEZE;
		} else {
			chan->flags &= ~DAHDI_FLAG_SIGFREEZE;
		}
		spin_unlock_irqrestore(&chan->lock, flags);
		break;
	case DAHDI_GETSIGFREEZE:
		spin_lock_irqsave(&chan->lock, flags);
		if (chan->flags & DAHDI_FLAG_SIGFREEZE)
			j = 1;
		else
			j = 0;
		spin_unlock_irqrestore(&chan->lock, flags);
		put_user(j, (int *)data);
		break;
	case DAHDI_AUDIOMODE:
		/* Only literal clear channels can be put in  */
		if (chan->sig != DAHDI_SIG_CLEAR) return (-EINVAL);
		get_user(j, (int *)data);
		if (j) {
			spin_lock_irqsave(&chan->lock, flags);
			chan->flags |= DAHDI_FLAG_AUDIO;
			chan->flags &= ~(DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS);
			spin_unlock_irqrestore(&chan->lock, flags);
		} else {
			/* Coming out of audio mode, also clear all 
			   conferencing and gain related info as well
			   as echo canceller */
			struct echo_can_state *ec_state;
			const struct dahdi_echocan *ec_current;

			spin_lock_irqsave(&chan->lock, flags);
			chan->flags &= ~DAHDI_FLAG_AUDIO;
			/* save old conf number, if any */
			oldconf = chan->confna;
			  /* initialize conference variables */
			chan->_confn = 0;
			chan->confna = 0;
			if (chan->span && chan->span->dacs)
				chan->span->dacs(chan, NULL);
			chan->confmode = 0;
			chan->confmute = 0;
			memset(chan->conflast, 0, sizeof(chan->conflast));
			memset(chan->conflast1, 0, sizeof(chan->conflast1));
			memset(chan->conflast2, 0, sizeof(chan->conflast2));
			ec_state = chan->ec_state;
			chan->ec_state = NULL;
			ec_current = chan->ec_current;
			chan->ec_current = NULL;
			/* release conference resource, if any to release */
			reset_conf(chan);
			if (chan->gainalloc && chan->rxgain)
				rxgain = chan->rxgain;
			else
				rxgain = NULL;

			chan->rxgain = defgain;
			chan->txgain = defgain;
			chan->gainalloc = 0;
			spin_unlock_irqrestore(&chan->lock, flags);

			if (ec_state) {
				ec_current->echo_can_free(ec_state);
				release_echocan(ec_current);
			}

			/* Disable any native echo cancellation as well */
			hw_echocancel_off(chan);

			if (rxgain)
				kfree(rxgain);
			if (oldconf) dahdi_check_conf(oldconf);
		}
		break;
	case DAHDI_HDLCPPP:
#ifdef CONFIG_DAHDI_PPP
		if (chan->sig != DAHDI_SIG_CLEAR) return (-EINVAL);
		get_user(j, (int *)data);
		if (j) {
			if (!chan->ppp) {
				chan->ppp = kmalloc(sizeof(struct ppp_channel), GFP_KERNEL);
				if (chan->ppp) {
					struct echo_can_state *tec;
					memset(chan->ppp, 0, sizeof(struct ppp_channel));
					chan->ppp->private = chan;
					chan->ppp->ops = &ztppp_ops;
					chan->ppp->mtu = DAHDI_DEFAULT_MTU_MRU;
					chan->ppp->hdrlen = 0;
					skb_queue_head_init(&chan->ppp_rq);
					chan->do_ppp_wakeup = 0;
					tasklet_init(&chan->ppp_calls, do_ppp_calls,
						     (unsigned long)chan);
					if ((ret = dahdi_reallocbufs(chan, DAHDI_DEFAULT_MTU_MRU, DAHDI_DEFAULT_NUM_BUFS))) {
						kfree(chan->ppp);
						chan->ppp = NULL;
						return ret;
					}
						
					if ((ret = ppp_register_channel(chan->ppp))) {
						kfree(chan->ppp);
						chan->ppp = NULL;
						return ret;
					}
					tec = chan->ec_state;
					chan->ec_state = NULL;
					chan->echocancel = 0;
					chan->echostate = ECHO_STATE_IDLE;
					chan->echolastupdate = 0;
					chan->echotimer = 0;
					/* Make sure there's no gain */
					if (chan->gainalloc)
						kfree(chan->rxgain);
					chan->rxgain = defgain;
					chan->txgain = defgain;
					chan->gainalloc = 0;
					chan->flags &= ~DAHDI_FLAG_AUDIO;
					chan->flags |= (DAHDI_FLAG_PPP | DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS);
					hw_echocancel_off(chan);
					
					if (tec)
						chan->ec->echo_can_free(tec);
				} else
					return -ENOMEM;
			}
		} else {
			chan->flags &= ~(DAHDI_FLAG_PPP | DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS);
			if (chan->ppp) {
				struct ppp_channel *ppp = chan->ppp;
				chan->ppp = NULL;
				tasklet_kill(&chan->ppp_calls);
				skb_queue_purge(&chan->ppp_rq);
				ppp_unregister_channel(ppp);
				kfree(ppp);
			}
		}
#else
		module_printk(KERN_NOTICE, "PPP support not compiled in\n");
		return -ENOSYS;
#endif
		break;
	case DAHDI_HDLCRAWMODE:
		if (chan->sig != DAHDI_SIG_CLEAR)	return (-EINVAL);
		get_user(j, (int *)data);
		chan->flags &= ~(DAHDI_FLAG_AUDIO | DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS);
		if (j) {
			chan->flags |= DAHDI_FLAG_HDLC;
			fasthdlc_init(&chan->rxhdlc);
			fasthdlc_init(&chan->txhdlc);
		}
		break;
	case DAHDI_HDLCFCSMODE:
		if (chan->sig != DAHDI_SIG_CLEAR)	return (-EINVAL);
		get_user(j, (int *)data);
		chan->flags &= ~(DAHDI_FLAG_AUDIO | DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS);
		if (j) {
			chan->flags |= DAHDI_FLAG_HDLC | DAHDI_FLAG_FCS;
			fasthdlc_init(&chan->rxhdlc);
			fasthdlc_init(&chan->txhdlc);
		}
		break;
	case DAHDI_ECHOCANCEL_PARAMS:
	{
		struct dahdi_echocanparams ecp;

		if (!(chan->flags & DAHDI_FLAG_AUDIO))
			return -EINVAL;
		if (copy_from_user(&ecp, (struct dahdi_echocanparams *) data, sizeof(ecp)))
			return -EFAULT;
		data += sizeof(ecp);
		if ((ret = ioctl_echocancel(chan, &ecp, (void *) data)))
			return ret;
		break;
	}
	case DAHDI_ECHOCANCEL:
	{
		struct dahdi_echocanparams ecp;

		if (!(chan->flags & DAHDI_FLAG_AUDIO))
			return -EINVAL;
		get_user(j, (int *) data);
		ecp.tap_length = j;
		ecp.param_count = 0;
		if ((ret = ioctl_echocancel(chan, &ecp, NULL)))
			return ret;
		break;
	}
	case DAHDI_ECHOTRAIN:
		get_user(j, (int *)data); /* get pre-training time from user */
		if ((j < 0) || (j >= DAHDI_MAX_PRETRAINING))
			return -EINVAL;
		j <<= 3;
		if (chan->ec_state) {
			/* Start pretraining stage */
			chan->echostate = ECHO_STATE_PRETRAINING;
			chan->echotimer = j;
		} else
			return -EINVAL;
		break;
	case DAHDI_SETTXBITS:
		if (chan->sig != DAHDI_SIG_CAS)
			return -EINVAL;
		get_user(j,(int *)data);
		dahdi_cas_setbits(chan, j);
		rv = 0;
		break;
	case DAHDI_GETRXBITS:
		put_user(chan->rxsig, (int *)data);
		rv = 0;
		break;
	case DAHDI_LOOPBACK:
		get_user(j, (int *)data);
		spin_lock_irqsave(&chan->lock, flags);
		if (j)
			chan->flags |= DAHDI_FLAG_LOOPED;
		else
			chan->flags &= ~DAHDI_FLAG_LOOPED;
		spin_unlock_irqrestore(&chan->lock, flags);
		rv = 0;
		break;
	case DAHDI_HOOK:
		get_user(j,(int *)data);
		if (chan->flags & DAHDI_FLAG_CLEAR)
			return -EINVAL;
		if (chan->sig == DAHDI_SIG_CAS) 
			return -EINVAL;
		/* if no span, just do nothing */
		if (!chan->span) return(0);
		spin_lock_irqsave(&chan->lock, flags);
		/* if dialing, stop it */
		chan->curtone = NULL;
		chan->dialing = 0;
		chan->txdialbuf[0] = '\0';
		chan->tonep = 0;
		chan->pdialcount = 0;
		spin_unlock_irqrestore(&chan->lock, flags);
		if (chan->span->flags & DAHDI_FLAG_RBS) {
			switch (j) {
			case DAHDI_ONHOOK:
				spin_lock_irqsave(&chan->lock, flags);
				dahdi_hangup(chan);
				spin_unlock_irqrestore(&chan->lock, flags);
				break;
			case DAHDI_OFFHOOK:
				spin_lock_irqsave(&chan->lock, flags);
				if ((chan->txstate == DAHDI_TXSTATE_KEWL) ||
				  (chan->txstate == DAHDI_TXSTATE_AFTERKEWL)) {
					spin_unlock_irqrestore(&chan->lock, flags);
					return -EBUSY;
				}
				dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_DEBOUNCE, chan->debouncetime);
				spin_unlock_irqrestore(&chan->lock, flags);
				break;
			case DAHDI_RING:
			case DAHDI_START:
				spin_lock_irqsave(&chan->lock, flags);
				if (!chan->curzone) {
					spin_unlock_irqrestore(&chan->lock, flags);
					module_printk(KERN_WARNING, "Cannot start tone until a tone zone is loaded.\n");
					return -ENODATA;
				}
				if (chan->txstate != DAHDI_TXSTATE_ONHOOK) {
					spin_unlock_irqrestore(&chan->lock, flags);
					return -EBUSY;
				}
				if (chan->sig & __DAHDI_SIG_FXO) {
					ret = 0;
					chan->cadencepos = 0;
					ret = chan->ringcadence[0];
					dahdi_rbs_sethook(chan, DAHDI_TXSIG_START, DAHDI_TXSTATE_RINGON, ret);
				} else
					dahdi_rbs_sethook(chan, DAHDI_TXSIG_START, DAHDI_TXSTATE_START, chan->starttime);
				spin_unlock_irqrestore(&chan->lock, flags);
				if (file->f_flags & O_NONBLOCK)
					return -EINPROGRESS;
#if 0
				rv = schluffen(&chan->txstateq);
				if (rv) return rv;
#endif				
				rv = 0;
				break;
			case DAHDI_WINK:
				spin_lock_irqsave(&chan->lock, flags);
				if (chan->txstate != DAHDI_TXSTATE_ONHOOK) {
					spin_unlock_irqrestore(&chan->lock, flags);
					return -EBUSY;
				}
				dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_PREWINK, chan->prewinktime);
				spin_unlock_irqrestore(&chan->lock, flags);
				if (file->f_flags & O_NONBLOCK)
					return -EINPROGRESS;
				rv = schluffen(&chan->txstateq);
				if (rv) return rv;
				break;
			case DAHDI_FLASH:
				spin_lock_irqsave(&chan->lock, flags);
				if (chan->txstate != DAHDI_TXSTATE_OFFHOOK) {
					spin_unlock_irqrestore(&chan->lock, flags);
					return -EBUSY;
				}
				dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_PREFLASH, chan->preflashtime);
				spin_unlock_irqrestore(&chan->lock, flags);
				if (file->f_flags & O_NONBLOCK)
					return -EINPROGRESS;
				rv = schluffen(&chan->txstateq);
				if (rv) return rv;
				break;
			case DAHDI_RINGOFF:
				spin_lock_irqsave(&chan->lock, flags);
				dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_ONHOOK, 0);
				spin_unlock_irqrestore(&chan->lock, flags);
				break;
			default:
				return -EINVAL;
			}
		} else if (chan->span->sethook) {
			if (chan->txhooksig != j) {
				chan->txhooksig = j;
				chan->span->sethook(chan, j);
			}
		} else
			return -ENOSYS;
		break;
#ifdef CONFIG_DAHDI_PPP
	case PPPIOCGCHAN:
		if (chan->flags & DAHDI_FLAG_PPP)
			return put_user(ppp_channel_index(chan->ppp), (int *)data) ? -EFAULT : 0;
		else
			return -EINVAL;
		break;
	case PPPIOCGUNIT:
		if (chan->flags & DAHDI_FLAG_PPP)
			return put_user(ppp_unit_number(chan->ppp), (int *)data) ? -EFAULT : 0;
		else
			return -EINVAL;
		break;
#endif
	default:
		return dahdi_chanandpseudo_ioctl(inode, file, cmd, data, unit);
	}
	return 0;
}

static int dahdi_prechan_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data, int unit)
{
	struct dahdi_chan *chan = file->private_data;
	int channo;
	int res;

	if (chan) {
		module_printk(KERN_NOTICE, "Huh?  Prechan already has private data??\n");
	}
	switch(cmd) {
	case DAHDI_SPECIFY:
		get_user(channo,(int *)data);
		if (channo < 1)
			return -EINVAL;
		if (channo > DAHDI_MAX_CHANNELS)
			return -EINVAL;
		res = dahdi_specchan_open(inode, file, channo, 0);
		if (!res) {
			/* Setup the pointer for future stuff */
			chan = chans[channo];
			file->private_data = chan;
			/* Return success */
			return 0;
		}
		return res;
	default:
		return -ENOSYS;
	}
	return 0;
}

static int dahdi_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long data)
{
	int unit = UNIT(file);
	struct dahdi_chan *chan;
	struct dahdi_timer *timer;

	if (!unit)
		return dahdi_ctl_ioctl(inode, file, cmd, data);

	if (unit == 250)
		return dahdi_transcode_fops->ioctl(inode, file, cmd, data);

	if (unit == 253) {
		timer = file->private_data;
		if (timer)
			return dahdi_timer_ioctl(inode, file, cmd, data, timer);
		else
			return -EINVAL;
	}
	if (unit == 254) {
		chan = file->private_data;
		if (chan)
			return dahdi_chan_ioctl(inode, file, cmd, data, chan->channo);
		else
			return dahdi_prechan_ioctl(inode, file, cmd, data, unit);
	}
	if (unit == 255) {
		chan = file->private_data;
		if (!chan) {
			module_printk(KERN_NOTICE, "No pseudo channel structure to read?\n");
			return -EINVAL;
		}
		return dahdi_chanandpseudo_ioctl(inode, file, cmd, data, chan->channo);
	}
	return dahdi_chan_ioctl(inode, file, cmd, data, unit);
}

int dahdi_register(struct dahdi_span *span, int prefmaster)
{
	int x;

#ifdef CONFIG_PROC_FS
	char tempfile[17];
#endif
	if (!span)
		return -EINVAL;
	if (span->flags & DAHDI_FLAG_REGISTERED) {
		module_printk(KERN_ERR, "Span %s already appears to be registered\n", span->name);
		return -EBUSY;
	}
	for (x=1;x<maxspans;x++)
		if (spans[x] == span) {
			module_printk(KERN_ERR, "Span %s already in list\n", span->name);
			return -EBUSY;
		}
	for (x=1;x<DAHDI_MAX_SPANS;x++)
		if (!spans[x])
			break;
	if (x < DAHDI_MAX_SPANS) {
		spans[x] = span;
		if (maxspans < x + 1)
			maxspans = x + 1;
	} else {
		module_printk(KERN_ERR, "Too many DAHDI spans registered\n");
		return -EBUSY;
	}
	span->flags |= DAHDI_FLAG_REGISTERED;
	span->spanno = x;
	spin_lock_init(&span->lock);
	if (!span->deflaw) {
		module_printk(KERN_NOTICE, "Span %s didn't specify default law.  Assuming mulaw, please fix driver!\n", span->name);
		span->deflaw = DAHDI_LAW_MULAW;
	}

	if (span->echocan && span->echocan_with_params) {
		module_printk(KERN_NOTICE, "Span %s implements both echocan and echocan_with_params functions, preserving only echocan_with_params, please fix driver!\n", span->name);
		span->echocan = NULL;
	}

	for (x=0;x<span->channels;x++) {
		span->chans[x].span = span;
		dahdi_chan_reg(&span->chans[x]); 
	}

#ifdef CONFIG_PROC_FS
			sprintf(tempfile, "dahdi/%d", span->spanno);
			proc_entries[span->spanno] = create_proc_read_entry(tempfile, 0444, NULL , dahdi_proc_read, (int *)(long)span->spanno);
#endif

	for (x = 0; x < span->channels; x++) {
		char chan_name[50];
		if (span->chans[x].channo < 250) {
			sprintf(chan_name, "dahdi%d", span->chans[x].channo);
			CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, span->chans[x].channo), NULL, chan_name);
		}
	}

	if (debug)
		module_printk(KERN_NOTICE, "Registered Span %d ('%s') with %d channels\n", span->spanno, span->name, span->channels);
	if (!master || prefmaster) {
		master = span;
		if (debug)
			module_printk(KERN_NOTICE, "Span ('%s') is new master\n", span->name);
	}
	return 0;
}

int dahdi_unregister(struct dahdi_span *span)
{
	int x;
	int new_maxspans;
	static struct dahdi_span *new_master;

#ifdef CONFIG_PROC_FS
	char tempfile[17];
#endif /* CONFIG_PROC_FS */

	if (!(span->flags & DAHDI_FLAG_REGISTERED)) {
		module_printk(KERN_ERR, "Span %s does not appear to be registered\n", span->name);
		return -1;
	}
	/* Shutdown the span if it's running */
	if (span->flags & DAHDI_FLAG_RUNNING)
		if (span->shutdown)
			span->shutdown(span);
			
	if (spans[span->spanno] != span) {
		module_printk(KERN_ERR, "Span %s has spanno %d which is something else\n", span->name, span->spanno);
		return -1;
	}
	if (debug)
		module_printk(KERN_NOTICE, "Unregistering Span '%s' with %d channels\n", span->name, span->channels);
#ifdef CONFIG_PROC_FS
	sprintf(tempfile, "dahdi/%d", span->spanno);
        remove_proc_entry(tempfile, NULL);
#endif /* CONFIG_PROC_FS */

	for (x = 0; x < span->channels; x++) {
		if (span->chans[x].channo < 250)
			class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, span->chans[x].channo));
	}

	spans[span->spanno] = NULL;
	span->spanno = 0;
	span->flags &= ~DAHDI_FLAG_REGISTERED;
	for (x=0;x<span->channels;x++)
		dahdi_chan_unreg(&span->chans[x]);
	new_maxspans = 0;
	new_master = master; /* FIXME: locking */
	if (master == span)
		new_master = NULL;
	for (x=1;x<DAHDI_MAX_SPANS;x++) {
		if (spans[x]) {
			new_maxspans = x+1;
			if (!new_master)
				new_master = spans[x];
		}
	}
	maxspans = new_maxspans;
	if (master != new_master)
		if (debug)
			module_printk(KERN_NOTICE, "%s: Span ('%s') is new master\n", __FUNCTION__, 
				      (new_master)? new_master->name: "no master");
	master = new_master;

	return 0;
}

/*
** This routine converts from linear to ulaw
**
** Craig Reese: IDA/Supercomputing Research Center
** Joe Campbell: Department of Defense
** 29 September 1989
**
** References:
** 1) CCITT Recommendation G.711  (very difficult to follow)
** 2) "A New Digital Technique for Implementation of Any
**     Continuous PCM Companding Law," Villeret, Michel,
**     et al. 1973 IEEE Int. Conf. on Communications, Vol 1,
**     1973, pg. 11.12-11.17
** 3) MIL-STD-188-113,"Interoperability and Performance Standards
**     for Analog-to_Digital Conversion Techniques,"
**     17 February 1987
**
** Input: Signed 16 bit linear sample
** Output: 8 bit ulaw sample
*/

#define ZEROTRAP    /* turn on the trap as per the MIL-STD */
#define BIAS 0x84   /* define the add-in bias for 16 bit samples */
#define CLIP 32635

#ifdef CONFIG_CALC_XLAW
unsigned char
#else
static unsigned char  __init
#endif
__dahdi_lineartoulaw(short sample)
{
  static int exp_lut[256] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
                             4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
  int sign, exponent, mantissa;
  unsigned char ulawbyte;

  /* Get the sample into sign-magnitude. */
  sign = (sample >> 8) & 0x80;          /* set aside the sign */
  if (sign != 0) sample = -sample;              /* get magnitude */
  if (sample > CLIP) sample = CLIP;             /* clip the magnitude */

  /* Convert from 16 bit linear to ulaw. */
  sample = sample + BIAS;
  exponent = exp_lut[(sample >> 7) & 0xFF];
  mantissa = (sample >> (exponent + 3)) & 0x0F;
  ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
  if (ulawbyte == 0) ulawbyte = 0x02;   /* optional CCITT trap */
#endif
  if (ulawbyte == 0xff) ulawbyte = 0x7f;   /* never return 0xff */
  return(ulawbyte);
}

#define AMI_MASK 0x55

#ifdef CONFIG_CALC_XLAW
unsigned char
#else
static inline unsigned char __init
#endif
__dahdi_lineartoalaw (short linear)
{
    int mask;
    int seg;
    int pcm_val;
    static int seg_end[8] =
    {
         0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF
    };
    
    pcm_val = linear;
    if (pcm_val >= 0)
    {
        /* Sign (7th) bit = 1 */
        mask = AMI_MASK | 0x80;
    }
    else
    {
        /* Sign bit = 0 */
        mask = AMI_MASK;
        pcm_val = -pcm_val;
    }

    /* Convert the scaled magnitude to segment number. */
    for (seg = 0;  seg < 8;  seg++)
    {
        if (pcm_val <= seg_end[seg])
	    break;
    }
    /* Combine the sign, segment, and quantization bits. */
    return  ((seg << 4) | ((pcm_val >> ((seg)  ?  (seg + 3)  :  4)) & 0x0F)) ^ mask;
}
/*- End of function --------------------------------------------------------*/

static inline short int __init alaw2linear (uint8_t alaw)
{
    int i;
    int seg;

    alaw ^= AMI_MASK;
    i = ((alaw & 0x0F) << 4);
    seg = (((int) alaw & 0x70) >> 4);
    if (seg)
        i = (i + 0x100) << (seg - 1);
    return (short int) ((alaw & 0x80)  ?  i  :  -i);
}
/*- End of function --------------------------------------------------------*/
static void  __init dahdi_conv_init(void)
{
	int i;

	/* 
	 *  Set up mu-law conversion table
	 */
	for(i = 0;i < 256;i++)
	   {
		short mu,e,f,y;
		static short etab[]={0,132,396,924,1980,4092,8316,16764};

		mu = 255-i;
		e = (mu & 0x70)/16;
		f = mu & 0x0f;
		y = f * (1 << (e + 3));
		y += etab[e];
		if (mu & 0x80) y = -y;
	        __dahdi_mulaw[i] = y;
		__dahdi_alaw[i] = alaw2linear(i);
		/* Default (0.0 db) gain table */
		defgain[i] = i;
	   }
#ifndef CONFIG_CALC_XLAW
	  /* set up the reverse (mu-law) conversion table */
	for(i = -32768; i < 32768; i += 4)
	   {
		__dahdi_lin2mu[((unsigned short)(short)i) >> 2] = __dahdi_lineartoulaw(i);
		__dahdi_lin2a[((unsigned short)(short)i) >> 2] = __dahdi_lineartoalaw(i);
	   }
#endif
}

static inline void __dahdi_process_getaudio_chunk(struct dahdi_chan *ss, unsigned char *txb)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct dahdi_chan *ms = ss->master;
	/* Linear representation */
	short getlin[DAHDI_CHUNKSIZE], k[DAHDI_CHUNKSIZE];
	int x;

	/* Okay, now we've got something to transmit */
	for (x=0;x<DAHDI_CHUNKSIZE;x++)
		getlin[x] = DAHDI_XLAW(txb[x], ms);
#ifndef NO_ECHOCAN_DISABLE
	if (ms->ec_state) {
		for (x=0;x<DAHDI_CHUNKSIZE;x++) {
			/* Check for echo cancel disabling tone */
			if (echo_can_disable_detector_update(&ms->txecdis, getlin[x])) {
				module_printk(KERN_NOTICE, "Disabled echo canceller because of tone (tx) on channel %d\n", ss->channo);
				ms->echocancel = 0;
				ms->echostate = ECHO_STATE_IDLE;
				ms->echolastupdate = 0;
				ms->echotimer = 0;
				ms->ec_current->echo_can_free(ms->ec_state);
				ms->ec_state = NULL;
				release_echocan(ms->ec_current);
				ms->ec_current = NULL;
				__qevent(ss, DAHDI_EVENT_EC_DISABLED);
				break;
			}
		}
	}
#endif
	if ((!ms->confmute && !ms->dialing) || (ms->flags & DAHDI_FLAG_PSEUDO)) {
		/* Handle conferencing on non-clear channel and non-HDLC channels */
		switch(ms->confmode & DAHDI_CONF_MODE_MASK) {
		case DAHDI_CONF_NORMAL:
			/* Do nuffin */
			break;
		case DAHDI_CONF_MONITOR:	/* Monitor a channel's rx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				ACSS(getlin, chans[ms->confna]->getlin);
			} else {
				ACSS(getlin, chans[ms->confna]->putlin);
			}
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);
			break;
		case DAHDI_CONF_MONITORTX: /* Monitor a channel's tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				ACSS(getlin, chans[ms->confna]->putlin);
			} else {
				ACSS(getlin, chans[ms->confna]->getlin);
			}

			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);
			break;
		case DAHDI_CONF_MONITORBOTH: /* monitor a channel's rx and tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO) break;
			ACSS(getlin, chans[ms->confna]->putlin);
			ACSS(getlin, chans[ms->confna]->getlin);
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);
			break;
		case DAHDI_CONF_MONITOR_RX_PREECHO:	/* Monitor a channel's rx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO)
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			/* Add monitored channel */
			ACSS(getlin, chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO ?
			     chans[ms->confna]->readchunkpreec : chans[ms->confna]->putlin);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);

			break;
		case DAHDI_CONF_MONITOR_TX_PREECHO: /* Monitor a channel's tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO)
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			/* Add monitored channel */
			ACSS(getlin, chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO ?
			     chans[ms->confna]->putlin : chans[ms->confna]->readchunkpreec);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);

			break;
		case DAHDI_CONF_MONITORBOTH_PREECHO: /* monitor a channel's rx and tx mode */
			  /* if a pseudo-channel, ignore */
			if (ms->flags & DAHDI_FLAG_PSEUDO)
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			ACSS(getlin, chans[ms->confna]->putlin);
			ACSS(getlin, chans[ms->confna]->readchunkpreec);

			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);

			break;
		case DAHDI_CONF_REALANDPSEUDO:
			/* This strange mode takes the transmit buffer and
				puts it on the conference, minus its last sample,
				then outputs from the conference minus the 
				real channel's last sample. */
			  /* if to talk on conf */
			if (ms->confmode & DAHDI_CONF_PSEUDO_TALKER) {
				/* Store temp value */
				memcpy(k, getlin, DAHDI_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/* save last one */
				memcpy(ms->conflast2, ms->conflast1, DAHDI_CHUNKSIZE * sizeof(short));
				memcpy(ms->conflast1, k, DAHDI_CHUNKSIZE * sizeof(short));
				/*  get amount actually added */
				SCSS(ms->conflast1, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast1);
			} else {
				memset(ms->conflast1, 0, DAHDI_CHUNKSIZE * sizeof(short));
				memset(ms->conflast2, 0, DAHDI_CHUNKSIZE * sizeof(short));
			}
			memset(getlin, 0, DAHDI_CHUNKSIZE * sizeof(short));
			txb[0] = DAHDI_LIN2X(0, ms);
			memset(txb + 1, txb[0], DAHDI_CHUNKSIZE - 1);
			/* fall through to normal conf mode */
		case DAHDI_CONF_CONF:	/* Normal conference mode */
			if (ms->flags & DAHDI_FLAG_PSEUDO) /* if pseudo-channel */
			   {
				  /* if to talk on conf */
				if (ms->confmode & DAHDI_CONF_TALKER) {
					/* Store temp value */
					memcpy(k, getlin, DAHDI_CHUNKSIZE * sizeof(short));
					/* Add conf value */
					ACSS(k, conf_sums[ms->_confn]);
					/*  get amount actually added */
					memcpy(ms->conflast, k, DAHDI_CHUNKSIZE * sizeof(short));
					SCSS(ms->conflast, conf_sums[ms->_confn]);
					/* Really add in new value */
					ACSS(conf_sums[ms->_confn], ms->conflast);
					memcpy(ms->getlin, getlin, DAHDI_CHUNKSIZE * sizeof(short));
				} else {
					memset(ms->conflast, 0, DAHDI_CHUNKSIZE * sizeof(short));
					memcpy(getlin, ms->getlin, DAHDI_CHUNKSIZE * sizeof(short));
				}
				txb[0] = DAHDI_LIN2X(0, ms);
				memset(txb + 1, txb[0], DAHDI_CHUNKSIZE - 1);
				break;
		 	   }
			/* fall through */
		case DAHDI_CONF_CONFMON:	/* Conference monitor mode */
			if (ms->confmode & DAHDI_CONF_LISTENER) {
				/* Subtract out last sample written to conf */
				SCSS(getlin, ms->conflast);
				/* Add in conference */
				ACSS(getlin, conf_sums[ms->_confn]);
			}
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);
			break;
		case DAHDI_CONF_CONFANN:
		case DAHDI_CONF_CONFANNMON:
			/* First, add tx buffer to conf */
			ACSS(conf_sums_next[ms->_confn], getlin);
			/* Start with silence */
			memset(getlin, 0, DAHDI_CHUNKSIZE * sizeof(short));
			/* If a listener on the conf... */
			if (ms->confmode & DAHDI_CONF_LISTENER) {
				/* Subtract last value written */
				SCSS(getlin, ms->conflast);
				/* Add in conf */
				ACSS(getlin, conf_sums[ms->_confn]);
			}
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				txb[x] = DAHDI_LIN2X(getlin[x], ms);
			break;
		case DAHDI_CONF_DIGITALMON:
			/* Real digital monitoring, but still echo cancel if desired */
			if (!chans[ms->confna])
				break;
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				if (ms->ec_state) {
					for (x=0;x<DAHDI_CHUNKSIZE;x++)
						txb[x] = DAHDI_LIN2X(chans[ms->confna]->getlin[x], ms);
				} else {
					memcpy(txb, chans[ms->confna]->getraw, DAHDI_CHUNKSIZE);
				}
			} else {
				if (ms->ec_state) {
					for (x=0;x<DAHDI_CHUNKSIZE;x++)
						txb[x] = DAHDI_LIN2X(chans[ms->confna]->putlin[x], ms);
				} else {
					memcpy(txb, chans[ms->confna]->putraw, DAHDI_CHUNKSIZE);
				}
			}
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				getlin[x] = DAHDI_XLAW(txb[x], ms);
			break;
		}
	}
	if (ms->confmute || (ms->echostate & __ECHO_STATE_MUTE)) {
		txb[0] = DAHDI_LIN2X(0, ms);
		memset(txb + 1, txb[0], DAHDI_CHUNKSIZE - 1);
		if (ms->echostate == ECHO_STATE_STARTTRAINING) {
			/* Transmit impulse now */
			txb[0] = DAHDI_LIN2X(16384, ms);
			ms->echostate = ECHO_STATE_AWAITINGECHO;
		}
	}
	/* save value from last chunk */
	memcpy(ms->getlin_lastchunk, ms->getlin, DAHDI_CHUNKSIZE * sizeof(short));
	/* save value from current */
	memcpy(ms->getlin, getlin, DAHDI_CHUNKSIZE * sizeof(short));
	/* save value from current */
	memcpy(ms->getraw, txb, DAHDI_CHUNKSIZE);
	/* if to make tx tone */
	if (ms->v1_1 || ms->v2_1 || ms->v3_1)
	{
		for (x=0;x<DAHDI_CHUNKSIZE;x++)
		{
			getlin[x] += dahdi_txtone_nextsample(ms);
			txb[x] = DAHDI_LIN2X(getlin[x], ms);
		}
	}
	/* This is what to send (after having applied gain) */
	for (x=0;x<DAHDI_CHUNKSIZE;x++)
		txb[x] = ms->txgain[txb[x]];
}

static inline void __dahdi_getbuf_chunk(struct dahdi_chan *ss, unsigned char *txb)
{
	/* Called with ss->lock held */
	/* We transmit data from our master channel */
	struct dahdi_chan *ms = ss->master;
	/* Buffer we're using */
	unsigned char *buf;
	/* Old buffer number */
	int oldbuf;
	/* Linear representation */
	int getlin;
	/* How many bytes we need to process */
	int bytes = DAHDI_CHUNKSIZE, left;
	int x;

	/* Let's pick something to transmit.  First source to
	   try is our write-out buffer.  Always check it first because
	   its our 'fast path' for whatever that's worth. */
	while(bytes) {
		if ((ms->outwritebuf > -1) && !ms->txdisable) {
			buf= ms->writebuf[ms->outwritebuf];
			left = ms->writen[ms->outwritebuf] - ms->writeidx[ms->outwritebuf];
			if (left > bytes)
				left = bytes;
			if (ms->flags & DAHDI_FLAG_HDLC) {
				/* If this is an HDLC channel we only send a byte of
				   HDLC. */
				for(x=0;x<left;x++) {
					if (ms->txhdlc.bits < 8)
						/* Load a byte of data only if needed */
						fasthdlc_tx_load_nocheck(&ms->txhdlc, buf[ms->writeidx[ms->outwritebuf]++]);
					*(txb++) = fasthdlc_tx_run_nocheck(&ms->txhdlc);
				}
				bytes -= left;
			} else {
				memcpy(txb, buf + ms->writeidx[ms->outwritebuf], left);
				ms->writeidx[ms->outwritebuf]+=left;
				txb += left;
				bytes -= left;
			}
			/* Check buffer status */
			if (ms->writeidx[ms->outwritebuf] >= ms->writen[ms->outwritebuf]) {
				/* We've reached the end of our buffer.  Go to the next. */
				oldbuf = ms->outwritebuf;
				/* Clear out write index and such */
				ms->writeidx[oldbuf] = 0;
				ms->outwritebuf = (ms->outwritebuf + 1) % ms->numbufs;

				if (!(ms->flags & DAHDI_FLAG_MTP2)) {
					ms->writen[oldbuf] = 0;
					if (ms->outwritebuf == ms->inwritebuf) {
						/* Whoopsies, we're run out of buffers.  Mark ours
						as -1 and wait for the filler to notify us that
						there is something to write */
						ms->outwritebuf = -1;
						if (ms->iomask & (DAHDI_IOMUX_WRITE | DAHDI_IOMUX_WRITEEMPTY))
							wake_up_interruptible(&ms->eventbufq);
						/* If we're only supposed to start when full, disable the transmitter */
						if (ms->txbufpolicy == DAHDI_POLICY_WHEN_FULL)
							ms->txdisable = 1;
					}
				} else {
					if (ms->outwritebuf == ms->inwritebuf) {
						ms->outwritebuf = oldbuf;
						if (ms->iomask & (DAHDI_IOMUX_WRITE | DAHDI_IOMUX_WRITEEMPTY))
							wake_up_interruptible(&ms->eventbufq);
						/* If we're only supposed to start when full, disable the transmitter */
						if (ms->txbufpolicy == DAHDI_POLICY_WHEN_FULL)
							ms->txdisable = 1;
					}
				}
				if (ms->inwritebuf < 0) {
					/* The filler doesn't have a place to put data.  Now
					that we're done with this buffer, notify them. */
					ms->inwritebuf = oldbuf;
				}
/* In the very orignal driver, it was quite well known to me (Jim) that there
was a possibility that a channel sleeping on a write block needed to
be potentially woken up EVERY time a buffer was emptied, not just on the first
one, because if only done on the first one there is a slight timing potential
of missing the wakeup (between where it senses the (lack of) active condition
(with interrupts disabled) and where it does the sleep (interrupts enabled)
in the read or iomux call, etc). That is why the write and iomux calls start
with an infinite loop that gets broken out of upon an active condition,
otherwise keeps sleeping and looking. The part in this code got "optimized"
out in the later versions, and is put back now. */
				if (!(ms->flags & (DAHDI_FLAG_NETDEV | DAHDI_FLAG_PPP))) {
					wake_up_interruptible(&ms->writebufq);
					wake_up_interruptible(&ms->sel);
					if (ms->iomask & DAHDI_IOMUX_WRITE)
						wake_up_interruptible(&ms->eventbufq);
				}
				/* Transmit a flag if this is an HDLC channel */
				if (ms->flags & DAHDI_FLAG_HDLC)
					fasthdlc_tx_frame_nocheck(&ms->txhdlc);
#ifdef CONFIG_DAHDI_NET
				if (ms->flags & DAHDI_FLAG_NETDEV)
					netif_wake_queue(ztchan_to_dev(ms));
#endif				
#ifdef CONFIG_DAHDI_PPP
				if (ms->flags & DAHDI_FLAG_PPP) {
					ms->do_ppp_wakeup = 1;
					tasklet_schedule(&ms->ppp_calls);
				}
#endif
			}
		} else if (ms->curtone && !(ms->flags & DAHDI_FLAG_PSEUDO)) {
			left = ms->curtone->tonesamples - ms->tonep;
			if (left > bytes)
				left = bytes;
			for (x=0;x<left;x++) {
				/* Pick our default value from the next sample of the current tone */
				getlin = dahdi_tone_nextsample(&ms->ts, ms->curtone);
				*(txb++) = DAHDI_LIN2X(getlin, ms);
			}
			ms->tonep+=left;
			bytes -= left;
			if (ms->tonep >= ms->curtone->tonesamples) {
				struct dahdi_tone *last;
				/* Go to the next sample of the tone */
				ms->tonep = 0;
				last = ms->curtone;
				ms->curtone = ms->curtone->next;
				if (!ms->curtone) {
					/* No more tones...  Is this dtmf or mf?  If so, go to the next digit */
					if (ms->dialing)
						__do_dtmf(ms);
				} else {
					if (last != ms->curtone)
						dahdi_init_tone_state(&ms->ts, ms->curtone);
				}
			}
		} else if (ms->flags & DAHDI_FLAG_LOOPED) {
			for (x = 0; x < bytes; x++)
				txb[x] = ms->readchunk[x];
			bytes = 0;
		} else if (ms->flags & DAHDI_FLAG_HDLC) {
			for (x=0;x<bytes;x++) {
				/* Okay, if we're HDLC, then transmit a flag by default */
				if (ms->txhdlc.bits < 8) 
					fasthdlc_tx_frame_nocheck(&ms->txhdlc);
				*(txb++) = fasthdlc_tx_run_nocheck(&ms->txhdlc);
			}
			bytes = 0;
		} else if (ms->flags & DAHDI_FLAG_CLEAR) {
			/* Clear channels that are idle in audio mode need
			   to send silence; in non-audio mode, always send 0xff
			   so stupid switches won't consider the channel active
			*/
			if (ms->flags & DAHDI_FLAG_AUDIO) {
				memset(txb, DAHDI_LIN2X(0, ms), bytes);
			} else {
				memset(txb, 0xFF, bytes);
			}
			bytes = 0;
		} else {
			memset(txb, DAHDI_LIN2X(0, ms), bytes);	/* Lastly we use silence on telephony channels */
			bytes = 0;
		}
	}	
}

static inline void rbs_itimer_expire(struct dahdi_chan *chan)
{
	/* the only way this could have gotten here, is if a channel
	    went onf hook longer then the wink or flash detect timeout */
	/* Called with chan->lock held */
	switch(chan->sig)
	{
	    case DAHDI_SIG_FXOLS:  /* if FXO, its definitely on hook */
	    case DAHDI_SIG_FXOGS:
	    case DAHDI_SIG_FXOKS:
		__qevent(chan,DAHDI_EVENT_ONHOOK);
		chan->gotgs = 0; 
		break;
#if defined(EMFLASH) || defined(EMPULSE)
	    case DAHDI_SIG_EM:
	    case DAHDI_SIG_EM_E1:
		if (chan->rxhooksig == DAHDI_RXSIG_ONHOOK) {
			__qevent(chan,DAHDI_EVENT_ONHOOK); 
			break;
		}
		__qevent(chan,DAHDI_EVENT_RINGOFFHOOK); 
		break;
#endif
#ifdef	FXSFLASH
	    case DAHDI_SIG_FXSKS:
		if (chan->rxhooksig == DAHDI_RXSIG_ONHOOK) {
			__qevent(chan, DAHDI_EVENT_ONHOOK); 
			break;
		}
#endif
		/* fall thru intentionally */
	    default:  /* otherwise, its definitely off hook */
		__qevent(chan,DAHDI_EVENT_RINGOFFHOOK); 
		break;
	}
}

static inline void __rbs_otimer_expire(struct dahdi_chan *chan)
{
	int len = 0;
	/* Called with chan->lock held */

	chan->otimer = 0;
	/* Move to the next timer state */	
	switch(chan->txstate) {
	case DAHDI_TXSTATE_RINGOFF:
		/* Turn on the ringer now that the silent time has passed */
		++chan->cadencepos;
		if (chan->cadencepos >= DAHDI_MAX_CADENCE)
			chan->cadencepos = chan->firstcadencepos;
		len = chan->ringcadence[chan->cadencepos];

		if (!len) {
			chan->cadencepos = chan->firstcadencepos;
			len = chan->ringcadence[chan->cadencepos];
		}

		dahdi_rbs_sethook(chan, DAHDI_TXSIG_START, DAHDI_TXSTATE_RINGON, len);
		__qevent(chan, DAHDI_EVENT_RINGERON);
		break;
		
	case DAHDI_TXSTATE_RINGON:
		/* Turn off the ringer now that the loud time has passed */
		++chan->cadencepos;
		if (chan->cadencepos >= DAHDI_MAX_CADENCE)
			chan->cadencepos = 0;
		len = chan->ringcadence[chan->cadencepos];

		if (!len) {
			chan->cadencepos = 0;
			len = chan->curzone->ringcadence[chan->cadencepos];
		}

		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_RINGOFF, len);
		__qevent(chan, DAHDI_EVENT_RINGEROFF);
		break;
		
	case DAHDI_TXSTATE_START:
		/* If we were starting, go off hook now ready to debounce */
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_AFTERSTART, DAHDI_AFTERSTART_TIME);
		wake_up_interruptible(&chan->txstateq);
		break;
		
	case DAHDI_TXSTATE_PREWINK:
		/* Actually wink */
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_WINK, chan->winktime);
		break;
		
	case DAHDI_TXSTATE_WINK:
		/* Wink complete, go on hook and stabalize */
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_ONHOOK, 0);
		if (chan->file && (chan->file->f_flags & O_NONBLOCK))
			__qevent(chan, DAHDI_EVENT_HOOKCOMPLETE);
		wake_up_interruptible(&chan->txstateq);
		break;
		
	case DAHDI_TXSTATE_PREFLASH:
		/* Actually flash */
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_FLASH, chan->flashtime);
		break;

	case DAHDI_TXSTATE_FLASH:
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_OFFHOOK, 0);
		if (chan->file && (chan->file->f_flags & O_NONBLOCK))
			__qevent(chan, DAHDI_EVENT_HOOKCOMPLETE);
		wake_up_interruptible(&chan->txstateq);
		break;
	
	case DAHDI_TXSTATE_DEBOUNCE:
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_OFFHOOK, 0);
		/* See if we've gone back on hook */
		if ((chan->rxhooksig == DAHDI_RXSIG_ONHOOK) && (chan->rxflashtime > 2))
			chan->itimerset = chan->itimer = chan->rxflashtime * DAHDI_CHUNKSIZE;
		wake_up_interruptible(&chan->txstateq);
		break;
		
	case DAHDI_TXSTATE_AFTERSTART:
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_OFFHOOK, 0);
		if (chan->file && (chan->file->f_flags & O_NONBLOCK))
			__qevent(chan, DAHDI_EVENT_HOOKCOMPLETE);
		wake_up_interruptible(&chan->txstateq);
		break;

	case DAHDI_TXSTATE_KEWL:
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, DAHDI_TXSTATE_AFTERKEWL, DAHDI_AFTERKEWLTIME);
		if (chan->file && (chan->file->f_flags & O_NONBLOCK))
			__qevent(chan, DAHDI_EVENT_HOOKCOMPLETE);
		wake_up_interruptible(&chan->txstateq);
		break;

	case DAHDI_TXSTATE_AFTERKEWL:
		if (chan->kewlonhook)  {
			__qevent(chan,DAHDI_EVENT_ONHOOK);
		}
		chan->txstate = DAHDI_TXSTATE_ONHOOK;
		chan->gotgs = 0;
		break;

	case DAHDI_TXSTATE_PULSEBREAK:
		dahdi_rbs_sethook(chan, DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_PULSEMAKE, 
			chan->pulsemaketime);
		wake_up_interruptible(&chan->txstateq);
		break;

	case DAHDI_TXSTATE_PULSEMAKE:
		if (chan->pdialcount)
			chan->pdialcount--;
		if (chan->pdialcount)
		{
			dahdi_rbs_sethook(chan, DAHDI_TXSIG_ONHOOK, 
				DAHDI_TXSTATE_PULSEBREAK, chan->pulsebreaktime);
			break;
		}
		chan->txstate = DAHDI_TXSTATE_PULSEAFTER;
		chan->otimer = chan->pulseaftertime * DAHDI_CHUNKSIZE;
		wake_up_interruptible(&chan->txstateq);
		break;

	case DAHDI_TXSTATE_PULSEAFTER:
		chan->txstate = DAHDI_TXSTATE_OFFHOOK;
		__do_dtmf(chan);
		wake_up_interruptible(&chan->txstateq);
		break;

	default:
		break;
	}
}

static void __dahdi_hooksig_pvt(struct dahdi_chan *chan, dahdi_rxsig_t rxsig)
{

	/* State machines for receive hookstate transitions 
		called with chan->lock held */

	if ((chan->rxhooksig) == rxsig) return;
	
	if ((chan->flags & DAHDI_FLAG_SIGFREEZE)) return;

	chan->rxhooksig = rxsig;
#ifdef	RINGBEGIN
	if ((chan->sig & __DAHDI_SIG_FXS) && (rxsig == DAHDI_RXSIG_RING) &&
	    (!chan->ringdebtimer))
		__qevent(chan,DAHDI_EVENT_RINGBEGIN);  
#endif
	switch(chan->sig) {
	    case DAHDI_SIG_EM:  /* E and M */
	    case DAHDI_SIG_EM_E1:
		switch(rxsig) {
		    case DAHDI_RXSIG_OFFHOOK: /* went off hook */
			/* The interface is going off hook */
#ifdef	EMFLASH
			if (chan->itimer)
			{
				__qevent(chan,DAHDI_EVENT_WINKFLASH); 
				chan->itimerset = chan->itimer = 0;
				break;				
			}
#endif
#ifdef EMPULSE
			if (chan->itimer) /* if timer still running */
			{
			    int plen = chan->itimerset - chan->itimer;
			    if (plen <= DAHDI_MAXPULSETIME)
			    {
					if (plen >= DAHDI_MINPULSETIME)
					{
						chan->pulsecount++;

						chan->pulsetimer = DAHDI_PULSETIMEOUT;
                                                chan->itimerset = chan->itimer = 0;
						if (chan->pulsecount == 1)
							__qevent(chan,DAHDI_EVENT_PULSE_START); 
					} 
			    } 
			    break;
			}
#endif
			/* set wink timer */
			chan->itimerset = chan->itimer = chan->rxwinktime * DAHDI_CHUNKSIZE;
			break;
		    case DAHDI_RXSIG_ONHOOK: /* went on hook */
			/* This interface is now going on hook.
			   Check for WINK, etc */
			if (chan->itimer)
				__qevent(chan,DAHDI_EVENT_WINKFLASH); 
#if defined(EMFLASH) || defined(EMPULSE)
			else {
#ifdef EMFLASH
				chan->itimerset = chan->itimer = chan->rxflashtime * DAHDI_CHUNKSIZE;

#else /* EMFLASH */
				chan->itimerset = chan->itimer = chan->rxwinktime * DAHDI_CHUNKSIZE;

#endif /* EMFLASH */
				chan->gotgs = 0;
				break;				
			}
#else /* EMFLASH || EMPULSE */
			else {
				__qevent(chan,DAHDI_EVENT_ONHOOK); 
				chan->gotgs = 0;
			}
#endif
			chan->itimerset = chan->itimer = 0;
			break;
		    default:
			break;
		}
		break;
	   case DAHDI_SIG_FXSKS:  /* FXS Kewlstart */
		  /* ignore a bit poopy if loop not closed and stable */
		if (chan->txstate != DAHDI_TXSTATE_OFFHOOK) break;
#ifdef	FXSFLASH
		if (rxsig == DAHDI_RXSIG_ONHOOK) {
			chan->itimer = DAHDI_FXSFLASHMAXTIME * DAHDI_CHUNKSIZE;
			break;
		} else 	if (rxsig == DAHDI_RXSIG_OFFHOOK) {
			if (chan->itimer) {
				/* did the offhook occur in the window? if not, ignore both events */
				if (chan->itimer <= ((DAHDI_FXSFLASHMAXTIME - DAHDI_FXSFLASHMINTIME) * DAHDI_CHUNKSIZE))
					__qevent(chan, DAHDI_EVENT_WINKFLASH);
			}
			chan->itimer = 0;
			break;
		}
#endif
		/* fall through intentionally */
	   case DAHDI_SIG_FXSGS:  /* FXS Groundstart */
		if (rxsig == DAHDI_RXSIG_ONHOOK) {
			chan->ringdebtimer = RING_DEBOUNCE_TIME;
			chan->ringtrailer = 0;
			if (chan->txstate != DAHDI_TXSTATE_DEBOUNCE) {
				chan->gotgs = 0;
				__qevent(chan,DAHDI_EVENT_ONHOOK);
			}
		}
		break;
	   case DAHDI_SIG_FXOGS: /* FXO Groundstart */
		if (rxsig == DAHDI_RXSIG_START) {
			  /* if havent got gs, report it */
			if (!chan->gotgs) {
				__qevent(chan,DAHDI_EVENT_RINGOFFHOOK);
				chan->gotgs = 1;
			}
		}
		/* fall through intentionally */
	   case DAHDI_SIG_FXOLS: /* FXO Loopstart */
	   case DAHDI_SIG_FXOKS: /* FXO Kewlstart */
		switch(rxsig) {
		    case DAHDI_RXSIG_OFFHOOK: /* went off hook */
			  /* if asserti ng ring, stop it */
			if (chan->txstate == DAHDI_TXSTATE_START) {
				dahdi_rbs_sethook(chan,DAHDI_TXSIG_OFFHOOK, DAHDI_TXSTATE_AFTERSTART, DAHDI_AFTERSTART_TIME);
			}
			chan->kewlonhook = 0;
#ifdef CONFIG_DAHDI_DEBUG
			module_printk(KERN_NOTICE, "Off hook on channel %d, itimer = %d, gotgs = %d\n", chan->channo, chan->itimer, chan->gotgs);
#endif
			if (chan->itimer) /* if timer still running */
			{
			    int plen = chan->itimerset - chan->itimer;
			    if (plen <= DAHDI_MAXPULSETIME)
			    {
					if (plen >= DAHDI_MINPULSETIME)
					{
						chan->pulsecount++;
						chan->pulsetimer = DAHDI_PULSETIMEOUT;
						chan->itimer = chan->itimerset;
						if (chan->pulsecount == 1)
							__qevent(chan,DAHDI_EVENT_PULSE_START); 
					} 
			    } else 
					__qevent(chan,DAHDI_EVENT_WINKFLASH); 
			} else {
				  /* if havent got GS detect */
				if (!chan->gotgs) {
					__qevent(chan,DAHDI_EVENT_RINGOFFHOOK); 
					chan->gotgs = 1;
					chan->itimerset = chan->itimer = 0;
				}
			}
			chan->itimerset = chan->itimer = 0;
			break;
		    case DAHDI_RXSIG_ONHOOK: /* went on hook */
			  /* if not during offhook debounce time */
			if ((chan->txstate != DAHDI_TXSTATE_DEBOUNCE) &&
			    (chan->txstate != DAHDI_TXSTATE_KEWL) && 
			    (chan->txstate != DAHDI_TXSTATE_AFTERKEWL)) {
				chan->itimerset = chan->itimer = chan->rxflashtime * DAHDI_CHUNKSIZE;
			}
			if (chan->txstate == DAHDI_TXSTATE_KEWL)
				chan->kewlonhook = 1;
			break;
		    default:
			break;
		}
	    default:
		break;
	}
}

void dahdi_hooksig(struct dahdi_chan *chan, dahdi_rxsig_t rxsig)
{
	  /* skip if no change */
	unsigned long flags;
	spin_lock_irqsave(&chan->lock, flags);
	__dahdi_hooksig_pvt(chan,rxsig);
	spin_unlock_irqrestore(&chan->lock, flags);
}

void dahdi_rbsbits(struct dahdi_chan *chan, int cursig)
{
	unsigned long flags;
	if (cursig == chan->rxsig)
		return;

	if ((chan->flags & DAHDI_FLAG_SIGFREEZE)) return;

	spin_lock_irqsave(&chan->lock, flags);
	switch(chan->sig) {
	    case DAHDI_SIG_FXOGS: /* FXO Groundstart */
		/* B-bit only matters for FXO GS */
		if (!(cursig & DAHDI_BBIT)) {
			__dahdi_hooksig_pvt(chan, DAHDI_RXSIG_START);
			break;
		}
		/* Fall through */
	    case DAHDI_SIG_EM:  /* E and M */
	    case DAHDI_SIG_EM_E1:
	    case DAHDI_SIG_FXOLS: /* FXO Loopstart */
	    case DAHDI_SIG_FXOKS: /* FXO Kewlstart */
		if (cursig & DAHDI_ABIT)  /* off hook */
			__dahdi_hooksig_pvt(chan,DAHDI_RXSIG_OFFHOOK);
		else /* on hook */
			__dahdi_hooksig_pvt(chan,DAHDI_RXSIG_ONHOOK);
		break;

	   case DAHDI_SIG_FXSKS:  /* FXS Kewlstart */
	   case DAHDI_SIG_FXSGS:  /* FXS Groundstart */
		/* Fall through */
	   case DAHDI_SIG_FXSLS:
		if (!(cursig & DAHDI_BBIT)) {
			/* Check for ringing first */
			__dahdi_hooksig_pvt(chan, DAHDI_RXSIG_RING);
			break;
		}
		if ((chan->sig != DAHDI_SIG_FXSLS) && (cursig & DAHDI_ABIT)) { 
			    /* if went on hook */
			__dahdi_hooksig_pvt(chan, DAHDI_RXSIG_ONHOOK);
		} else {
			__dahdi_hooksig_pvt(chan, DAHDI_RXSIG_OFFHOOK);
		}
		break;
	   case DAHDI_SIG_CAS:
		/* send event that something changed */
		__qevent(chan, DAHDI_EVENT_BITSCHANGED);
		break;

	   default:
		break;
	}
	/* Keep track of signalling for next time */
	chan->rxsig = cursig;
	spin_unlock_irqrestore(&chan->lock, flags);
}

static inline void __dahdi_ec_chunk(struct dahdi_chan *ss, unsigned char *rxchunk, const unsigned char *txchunk)
{
	short rxlin, txlin;
	int x;
	unsigned long flags;

	spin_lock_irqsave(&ss->lock, flags);

	if (ss->readchunkpreec) {
		/* Save a copy of the audio before the echo can has its way with it */
		for (x = 0; x < DAHDI_CHUNKSIZE; x++)
			/* We only ever really need to deal with signed linear - let's just convert it now */
			ss->readchunkpreec[x] = DAHDI_XLAW(rxchunk[x], ss);
	}

	/* Perform echo cancellation on a chunk if necessary */
	if (ss->ec_state) {
#if defined(CONFIG_DAHDI_MMX) || defined(ECHO_CAN_FP)
		dahdi_kernel_fpu_begin();
#endif		
		if (ss->echostate & __ECHO_STATE_MUTE) {
			/* Special stuff for training the echo can */
			for (x=0;x<DAHDI_CHUNKSIZE;x++) {
				rxlin = DAHDI_XLAW(rxchunk[x], ss);
				txlin = DAHDI_XLAW(txchunk[x], ss);
				if (ss->echostate == ECHO_STATE_PRETRAINING) {
					if (--ss->echotimer <= 0) {
						ss->echotimer = 0;
						ss->echostate = ECHO_STATE_STARTTRAINING;
					}
				}
				if ((ss->echostate == ECHO_STATE_AWAITINGECHO) && (txlin > 8000)) {
					ss->echolastupdate = 0;
					ss->echostate = ECHO_STATE_TRAINING;
				}
				if (ss->echostate == ECHO_STATE_TRAINING) {
					if (ss->ec_current->echo_can_traintap(ss->ec_state, ss->echolastupdate++, rxlin)) {
#if 0
						module_printk(KERN_NOTICE, "Finished training (%d taps trained)!\n", ss->echolastupdate);
#endif						
						ss->echostate = ECHO_STATE_ACTIVE;
					}
				}
				rxlin = 0;
				rxchunk[x] = DAHDI_LIN2X((int)rxlin, ss);
			}
		} else {
			short rxlins[DAHDI_CHUNKSIZE], txlins[DAHDI_CHUNKSIZE];
			for (x = 0; x < DAHDI_CHUNKSIZE; x++) {
				rxlins[x] = DAHDI_XLAW(rxchunk[x], ss);
				txlins[x] = DAHDI_XLAW(txchunk[x], ss);
			}
			ss->ec_current->echo_can_array_update(ss->ec_state, rxlins, txlins);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				rxchunk[x] = DAHDI_LIN2X((int) rxlins[x], ss);
		}
#if defined(CONFIG_DAHDI_MMX) || defined(ECHO_CAN_FP)
		kernel_fpu_end();
#endif		
	}
	spin_unlock_irqrestore(&ss->lock, flags);
}

void dahdi_ec_chunk(struct dahdi_chan *ss, unsigned char *rxchunk, const unsigned char *txchunk)
{
	__dahdi_ec_chunk(ss, rxchunk, txchunk);
}

void dahdi_ec_span(struct dahdi_span *span)
{
	int x;
	for (x = 0; x < span->channels; x++) {
		if (span->chans[x].ec_current)
			__dahdi_ec_chunk(&span->chans[x], span->chans[x].readchunk, span->chans[x].writechunk);
	}
}

/* return 0 if nothing detected, 1 if lack of tone, 2 if presence of tone */
/* modifies buffer pointed to by 'amp' with notched-out values */
static inline int sf_detect (sf_detect_state_t *s,
                 short *amp,
                 int samples,long p1, long p2, long p3)
{
int     i,rv = 0;
long x,y;

#define	SF_DETECT_SAMPLES (DAHDI_CHUNKSIZE * 5)
#define	SF_DETECT_MIN_ENERGY 500
#define	NB 14  /* number of bits to shift left */
         
        /* determine energy level before filtering */
        for(i = 0; i < samples; i++)
        {
                if (amp[i] < 0) s->e1 -= amp[i];
                else s->e1 += amp[i];
        }
	/* do 2nd order IIR notch filter at given freq. and calculate
	    energy */
        for(i = 0; i < samples; i++)
        {
                x = amp[i] << NB;
                y = s->x2 + (p1 * (s->x1 >> NB)) + x;
                y += (p2 * (s->y2 >> NB)) + 
			(p3 * (s->y1 >> NB));
                s->x2 = s->x1;
                s->x1 = x;
                s->y2 = s->y1;
                s->y1 = y;
                amp[i] = y >> NB;
                if (amp[i] < 0) s->e2 -= amp[i];
                else s->e2 += amp[i];
        }
	s->samps += i;
	/* if time to do determination */
	if ((s->samps) >= SF_DETECT_SAMPLES)
	{
		rv = 1; /* default to no tone */
		/* if enough energy, it is determined to be a tone */
		if (((s->e1 - s->e2) / s->samps) > SF_DETECT_MIN_ENERGY) rv = 2;
		/* reset energy processing variables */
		s->samps = 0;
		s->e1 = s->e2 = 0;
	}
	return(rv);		
}

static inline void __dahdi_process_putaudio_chunk(struct dahdi_chan *ss, unsigned char *rxb)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct dahdi_chan *ms = ss->master;
	/* Linear version of received data */
	short putlin[DAHDI_CHUNKSIZE],k[DAHDI_CHUNKSIZE];
	int x,r;

	if (ms->dialing) ms->afterdialingtimer = 50;
	else if (ms->afterdialingtimer) ms->afterdialingtimer--;
	if (ms->afterdialingtimer && (!(ms->flags & DAHDI_FLAG_PSEUDO))) {
		/* Be careful since memset is likely a macro */
		rxb[0] = DAHDI_LIN2X(0, ms);
		memset(&rxb[1], rxb[0], DAHDI_CHUNKSIZE - 1);  /* receive as silence if dialing */
	} 
	for (x=0;x<DAHDI_CHUNKSIZE;x++) {
		rxb[x] = ms->rxgain[rxb[x]];
		putlin[x] = DAHDI_XLAW(rxb[x], ms);
	}

#ifndef NO_ECHOCAN_DISABLE
	if (ms->ec_state) {
		for (x=0;x<DAHDI_CHUNKSIZE;x++) {
			if (echo_can_disable_detector_update(&ms->rxecdis, putlin[x])) {
				module_printk(KERN_NOTICE, "Disabled echo canceller because of tone (rx) on channel %d\n", ss->channo);
				ms->echocancel = 0;
				ms->echostate = ECHO_STATE_IDLE;
				ms->echolastupdate = 0;
				ms->echotimer = 0;
				ms->ec_current->echo_can_free(ms->ec_state);
				ms->ec_state = NULL;
				release_echocan(ms->ec_current);
				ms->ec_current = NULL;
				break;
			}
		}
	}
#endif	
	/* if doing rx tone decoding */
	if (ms->rxp1 && ms->rxp2 && ms->rxp3)
	{
		r = sf_detect(&ms->rd,putlin,DAHDI_CHUNKSIZE,ms->rxp1,
			ms->rxp2,ms->rxp3);
		/* Convert back */
		for(x=0;x<DAHDI_CHUNKSIZE;x++)
			rxb[x] = DAHDI_LIN2X(putlin[x], ms);
		if (r) /* if something happened */
		{
			if (r != ms->rd.lastdetect)
			{
				if (((r == 2) && !(ms->toneflags & DAHDI_REVERSE_RXTONE)) ||
				    ((r == 1) && (ms->toneflags & DAHDI_REVERSE_RXTONE)))
				{
					__qevent(ms,DAHDI_EVENT_RINGOFFHOOK);
				}
				else
				{
					__qevent(ms,DAHDI_EVENT_ONHOOK);
				}
				ms->rd.lastdetect = r;
			}
		}
	}		

	if (!(ms->flags &  DAHDI_FLAG_PSEUDO)) {
		memcpy(ms->putlin, putlin, DAHDI_CHUNKSIZE * sizeof(short));
		memcpy(ms->putraw, rxb, DAHDI_CHUNKSIZE);
	}
	
	/* Take the rxc, twiddle it for conferencing if appropriate and put it
	   back */
	if ((!ms->confmute && !ms->afterdialingtimer) ||
	    (ms->flags & DAHDI_FLAG_PSEUDO)) {
		switch(ms->confmode & DAHDI_CONF_MODE_MASK) {
		case DAHDI_CONF_NORMAL:		/* Normal mode */
			/* Do nothing.  rx goes output */
			break;
		case DAHDI_CONF_MONITOR:		/* Monitor a channel's rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				ACSS(putlin, chans[ms->confna]->getlin);
			} else {
				ACSS(putlin, chans[ms->confna]->putlin);
			}
			/* Convert back */
			for(x=0;x<DAHDI_CHUNKSIZE;x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);
			break;
		case DAHDI_CONF_MONITORTX:	/* Monitor a channel's tx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				ACSS(putlin, chans[ms->confna]->putlin);
			} else {
				ACSS(putlin, chans[ms->confna]->getlin);
			}
			/* Convert back */
			for(x=0;x<DAHDI_CHUNKSIZE;x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);
			break;
		case DAHDI_CONF_MONITORBOTH:	/* Monitor a channel's tx and rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO)) break;
			/* Note: Technically, saturation should be done at 
			   the end of the whole addition, but for performance
			   reasons, we don't do that.  Besides, it only matters
			   when you're so loud you're clipping anyway */
			ACSS(putlin, chans[ms->confna]->getlin);
			ACSS(putlin, chans[ms->confna]->putlin);
			/* Convert back */
			for(x=0;x<DAHDI_CHUNKSIZE;x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);
			break;
		case DAHDI_CONF_MONITOR_RX_PREECHO:		/* Monitor a channel's rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO))
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			/* Add monitored channel */
			ACSS(putlin, chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO ?
			     chans[ms->confna]->getlin : chans[ms->confna]->readchunkpreec);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);

			break;
		case DAHDI_CONF_MONITOR_TX_PREECHO:	/* Monitor a channel's tx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO))
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			/* Add monitored channel */
			ACSS(putlin, chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO ?
			     chans[ms->confna]->readchunkpreec : chans[ms->confna]->getlin);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);

			break;
		case DAHDI_CONF_MONITORBOTH_PREECHO:	/* Monitor a channel's tx and rx mode */
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO))
				break;

			if (!chans[ms->confna]->readchunkpreec)
				break;

			/* Note: Technically, saturation should be done at 
			   the end of the whole addition, but for performance
			   reasons, we don't do that.  Besides, it only matters
			   when you're so loud you're clipping anyway */
			ACSS(putlin, chans[ms->confna]->getlin);
			ACSS(putlin, chans[ms->confna]->readchunkpreec);
			for (x = 0; x < DAHDI_CHUNKSIZE; x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);

			break;
		case DAHDI_CONF_REALANDPSEUDO:
			  /* do normal conf mode processing */
			if (ms->confmode & DAHDI_CONF_TALKER) {
				/* Store temp value */
				memcpy(k, putlin, DAHDI_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/*  get amount actually added */
				memcpy(ms->conflast, k, DAHDI_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast);
			} else memset(ms->conflast, 0, DAHDI_CHUNKSIZE * sizeof(short));
			  /* do the pseudo-channel part processing */
			memset(putlin, 0, DAHDI_CHUNKSIZE * sizeof(short));
			if (ms->confmode & DAHDI_CONF_PSEUDO_LISTENER) {
				/* Subtract out previous last sample written to conf */
				SCSS(putlin, ms->conflast2);
				/* Add in conference */
				ACSS(putlin, conf_sums[ms->_confn]);
			}
			/* Convert back */
			for(x=0;x<DAHDI_CHUNKSIZE;x++)
				rxb[x] = DAHDI_LIN2X(putlin[x], ms);
			break;
		case DAHDI_CONF_CONF:	/* Normal conference mode */
			if (ms->flags & DAHDI_FLAG_PSEUDO) /* if a pseudo-channel */
			   {
				if (ms->confmode & DAHDI_CONF_LISTENER) {
					/* Subtract out last sample written to conf */
					SCSS(putlin, ms->conflast);
					/* Add in conference */
					ACSS(putlin, conf_sums[ms->_confn]);
				}
				/* Convert back */
				for(x=0;x<DAHDI_CHUNKSIZE;x++)
					rxb[x] = DAHDI_LIN2X(putlin[x], ms);
				memcpy(ss->putlin, putlin, DAHDI_CHUNKSIZE * sizeof(short));
				break;
			   }
			/* fall through */
		case DAHDI_CONF_CONFANN:  /* Conference with announce */
			if (ms->confmode & DAHDI_CONF_TALKER) {
				/* Store temp value */
				memcpy(k, putlin, DAHDI_CHUNKSIZE * sizeof(short));
				/* Add conf value */
				ACSS(k, conf_sums_next[ms->_confn]);
				/*  get amount actually added */
				memcpy(ms->conflast, k, DAHDI_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums_next[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums_next[ms->_confn], ms->conflast);
			} else 
				memset(ms->conflast, 0, DAHDI_CHUNKSIZE * sizeof(short));
			  /* rxc unmodified */
			break;
		case DAHDI_CONF_CONFMON:
		case DAHDI_CONF_CONFANNMON:
			if (ms->confmode & DAHDI_CONF_TALKER) {
				/* Store temp value */
				memcpy(k, putlin, DAHDI_CHUNKSIZE * sizeof(short));
				/* Subtract last value */
				SCSS(conf_sums[ms->_confn], ms->conflast);
				/* Add conf value */
				ACSS(k, conf_sums[ms->_confn]);
				/*  get amount actually added */
				memcpy(ms->conflast, k, DAHDI_CHUNKSIZE * sizeof(short));
				SCSS(ms->conflast, conf_sums[ms->_confn]);
				/* Really add in new value */
				ACSS(conf_sums[ms->_confn], ms->conflast);
			} else 
				memset(ms->conflast, 0, DAHDI_CHUNKSIZE * sizeof(short));
			for (x=0;x<DAHDI_CHUNKSIZE;x++)
				rxb[x] = DAHDI_LIN2X((int)conf_sums_prev[ms->_confn][x], ms);
			break;
		case DAHDI_CONF_DIGITALMON:
			  /* if not a pseudo-channel, ignore */
			if (!(ms->flags & DAHDI_FLAG_PSEUDO)) break;
			/* Add monitored channel */
			if (chans[ms->confna]->flags & DAHDI_FLAG_PSEUDO) {
				memcpy(rxb, chans[ms->confna]->getraw, DAHDI_CHUNKSIZE);
			} else {
				memcpy(rxb, chans[ms->confna]->putraw, DAHDI_CHUNKSIZE);
			}
			break;			
		}
	}
}

/* HDLC (or other) receiver buffer functions for read side */
static inline void __putbuf_chunk(struct dahdi_chan *ss, unsigned char *rxb, int bytes)
{
	/* We transmit data from our master channel */
	/* Called with ss->lock held */
	struct dahdi_chan *ms = ss->master;
	/* Our receive buffer */
	unsigned char *buf;
#if defined(CONFIG_DAHDI_NET)  || defined(CONFIG_DAHDI_PPP)
	/* SKB for receiving network stuff */
	struct sk_buff *skb=NULL;
#endif	
	int oldbuf;
	int eof=0;
	int abort=0;
	int res;
	int left, x;

	while(bytes) {
#if defined(CONFIG_DAHDI_NET)  || defined(CONFIG_DAHDI_PPP)
		skb = NULL;
#endif	
		abort = 0;
		eof = 0;
		/* Next, figure out if we've got a buffer to receive into */
		if (ms->inreadbuf > -1) {
			/* Read into the current buffer */
			buf = ms->readbuf[ms->inreadbuf];
			left = ms->blocksize - ms->readidx[ms->inreadbuf];
			if (left > bytes)
				left = bytes;
			if (ms->flags & DAHDI_FLAG_HDLC) {
				for (x=0;x<left;x++) {
					/* Handle HDLC deframing */
					fasthdlc_rx_load_nocheck(&ms->rxhdlc, *(rxb++));
					bytes--;
					res = fasthdlc_rx_run(&ms->rxhdlc);
					/* If there is nothing there, continue */
					if (res & RETURN_EMPTY_FLAG)
						continue;
					else if (res & RETURN_COMPLETE_FLAG) {
						/* Only count this if it's a non-empty frame */
						if (ms->readidx[ms->inreadbuf]) {
							if ((ms->flags & DAHDI_FLAG_FCS) && (ms->infcs != PPP_GOODFCS)) {
								abort = DAHDI_EVENT_BADFCS;
							} else
								eof=1;
							break;
						}
						continue;
					} else if (res & RETURN_DISCARD_FLAG) {
						/* This could be someone idling with 
						  "idle" instead of "flag" */
						if (!ms->readidx[ms->inreadbuf])
							continue;
						abort = DAHDI_EVENT_ABORT;
						break;
					} else {
						unsigned char rxc;
						rxc = res;
						ms->infcs = PPP_FCS(ms->infcs, rxc);
						buf[ms->readidx[ms->inreadbuf]++] = rxc;
						/* Pay attention to the possibility of an overrun */
						if (ms->readidx[ms->inreadbuf] >= ms->blocksize) {
							if (!ss->span->alarms) 
								module_printk(KERN_WARNING, "HDLC Receiver overrun on channel %s (master=%s)\n", ss->name, ss->master->name);
							abort=DAHDI_EVENT_OVERRUN;
							/* Force the HDLC state back to frame-search mode */
							ms->rxhdlc.state = 0;
							ms->rxhdlc.bits = 0;
							ms->readidx[ms->inreadbuf]=0;
							break;
						}
					}
				}
			} else {
				/* Not HDLC */
				memcpy(buf + ms->readidx[ms->inreadbuf], rxb, left);
				rxb += left;
				ms->readidx[ms->inreadbuf] += left;
				bytes -= left;
				/* End of frame is decided by block size of 'N' */
				eof = (ms->readidx[ms->inreadbuf] >= ms->blocksize);
				if (eof && (ss->flags & DAHDI_FLAG_NOSTDTXRX)) {
					eof = 0;
					abort = DAHDI_EVENT_OVERRUN;
				}
			}
			if (eof)  {
				/* Finished with this buffer, try another. */
				oldbuf = ms->inreadbuf;
				ms->infcs = PPP_INITFCS;
				ms->readn[ms->inreadbuf] = ms->readidx[ms->inreadbuf];
#ifdef CONFIG_DAHDI_DEBUG
				module_printk(KERN_NOTICE, "EOF, len is %d\n", ms->readn[ms->inreadbuf]);
#endif
#if defined(CONFIG_DAHDI_NET) || defined(CONFIG_DAHDI_PPP)
				if (ms->flags & (DAHDI_FLAG_NETDEV | DAHDI_FLAG_PPP)) {
#ifdef CONFIG_DAHDI_NET
#endif /* CONFIG_DAHDI_NET */
					/* Our network receiver logic is MUCH
					  different.  We actually only use a single
					  buffer */
					if (ms->readn[ms->inreadbuf] > 1) {
						/* Drop the FCS */
						ms->readn[ms->inreadbuf] -= 2;
						/* Allocate an SKB */
#ifdef CONFIG_DAHDI_PPP
						if (!ms->do_ppp_error)
#endif
							skb = dev_alloc_skb(ms->readn[ms->inreadbuf]);
						if (skb) {
							/* XXX Get rid of this memcpy XXX */
							memcpy(skb->data, ms->readbuf[ms->inreadbuf], ms->readn[ms->inreadbuf]);
							skb_put(skb, ms->readn[ms->inreadbuf]);
#ifdef CONFIG_DAHDI_NET
							if (ms->flags & DAHDI_FLAG_NETDEV) {
								struct net_device_stats *stats = hdlc_stats(ms->hdlcnetdev->netdev);
								stats->rx_packets++;
								stats->rx_bytes += ms->readn[ms->inreadbuf];
							}
#endif

						} else {
#ifdef CONFIG_DAHDI_NET
							if (ms->flags & DAHDI_FLAG_NETDEV) {
								struct net_device_stats *stats = hdlc_stats(ms->hdlcnetdev->netdev);
								stats->rx_dropped++;
							}
#endif
#ifdef CONFIG_DAHDI_PPP
							if (ms->flags & DAHDI_FLAG_PPP) {
								abort = DAHDI_EVENT_OVERRUN;
							}
#endif
#if 1
#ifdef CONFIG_DAHDI_PPP
							if (!ms->do_ppp_error)
#endif
								module_printk(KERN_NOTICE, "Memory squeeze, dropped one\n");
#endif
						}
					}
					/* We don't cycle through buffers, just
					reuse the same one */
					ms->readn[ms->inreadbuf] = 0;
					ms->readidx[ms->inreadbuf] = 0;
				} else 
#endif
				{
					/* This logic might confuse and astound.  Basically we need to find
					 * the previous buffer index.  It should be safe because, regardless
					 * of whether or not it has been copied to user space, nothing should
					 * have messed around with it since then */

					int comparemessage;

					if (ms->flags & DAHDI_FLAG_MTP2) {
						comparemessage = (ms->inreadbuf - 1) & (ms->numbufs - 1);
						if (!memcmp(ms->readbuf[comparemessage], ms->readbuf[ms->inreadbuf], ms->readn[ms->inreadbuf])) {
							/* Our messages are the same, so discard -
							 * 	Don't advance buffers, reset indexes and buffer sizes. */
							ms->readn[ms->inreadbuf] = 0;
							ms->readidx[ms->inreadbuf] = 0;
						}
					} else {
						ms->inreadbuf = (ms->inreadbuf + 1) % ms->numbufs;
						if (ms->inreadbuf == ms->outreadbuf) {
							/* Whoops, we're full, and have no where else
							   to store into at the moment.  We'll drop it
							   until there's a buffer available */
#ifdef CONFIG_DAHDI_DEBUG
							module_printk(KERN_NOTICE, "Out of storage space\n");
#endif
							ms->inreadbuf = -1;
							/* Enable the receiver in case they've got POLICY_WHEN_FULL */
							ms->rxdisable = 0;
						}
						if (ms->outreadbuf < 0) { /* start out buffer if not already */
							ms->outreadbuf = oldbuf;
						}
/* In the very orignal driver, it was quite well known to me (Jim) that there
was a possibility that a channel sleeping on a receive block needed to
be potentially woken up EVERY time a buffer was filled, not just on the first
one, because if only done on the first one there is a slight timing potential
of missing the wakeup (between where it senses the (lack of) active condition
(with interrupts disabled) and where it does the sleep (interrupts enabled)
in the read or iomux call, etc). That is why the read and iomux calls start
with an infinite loop that gets broken out of upon an active condition,
otherwise keeps sleeping and looking. The part in this code got "optimized"
out in the later versions, and is put back now. */
						if (!ms->rxdisable) { /* if receiver enabled */
							/* Notify a blocked reader that there is data available
							to be read, unless we're waiting for it to be full */
#ifdef CONFIG_DAHDI_DEBUG
							module_printk(KERN_NOTICE, "Notifying reader data in block %d\n", oldbuf);
#endif
							wake_up_interruptible(&ms->readbufq);
							wake_up_interruptible(&ms->sel);
							if (ms->iomask & DAHDI_IOMUX_READ)
								wake_up_interruptible(&ms->eventbufq);
						}
					}
				}
			}
			if (abort) {
				/* Start over reading frame */
				ms->readidx[ms->inreadbuf] = 0;
				ms->infcs = PPP_INITFCS;

#ifdef CONFIG_DAHDI_NET
				if (ms->flags & DAHDI_FLAG_NETDEV) {
					struct net_device_stats *stats = hdlc_stats(ms->hdlcnetdev->netdev);
					stats->rx_errors++;
					if (abort == DAHDI_EVENT_OVERRUN)
						stats->rx_over_errors++;
					if (abort == DAHDI_EVENT_BADFCS)
						stats->rx_crc_errors++;
					if (abort == DAHDI_EVENT_ABORT)
						stats->rx_frame_errors++;
				} else 
#endif			
#ifdef CONFIG_DAHDI_PPP
				if (ms->flags & DAHDI_FLAG_PPP) {
					ms->do_ppp_error = 1;
					tasklet_schedule(&ms->ppp_calls);
				} else
#endif
					if (test_bit(DAHDI_FLAGBIT_OPEN, &ms->flags) && !ss->span->alarms) {
						/* Notify the receiver... */
						__qevent(ss->master, abort);
					}
#if 0
				module_printk(KERN_NOTICE, "torintr_receive: Aborted %d bytes of frame on %d\n", amt, ss->master);
#endif

			}
		} else /* No place to receive -- drop on the floor */
			break;
#ifdef CONFIG_DAHDI_NET
		if (skb && (ms->flags & DAHDI_FLAG_NETDEV))
#ifdef NEW_HDLC_INTERFACE
		{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22)
			skb->mac.raw = skb->data;
#else
			skb_reset_mac_header(skb);
#endif
			skb->dev = ztchan_to_dev(ms);
#ifdef DAHDI_HDLC_TYPE_TRANS
			skb->protocol = hdlc_type_trans(skb, ztchan_to_dev(ms));
#else
			skb->protocol = htons (ETH_P_HDLC);
#endif
			netif_rx(skb);
		}
#else
			hdlc_netif_rx(&ms->hdlcnetdev->netdev, skb);
#endif
#endif
#ifdef CONFIG_DAHDI_PPP
		if (skb && (ms->flags & DAHDI_FLAG_PPP)) {
			unsigned char *tmp;
			tmp = skb->data;
			skb_pull(skb, 2);
			/* Make sure that it's addressed to ALL STATIONS and UNNUMBERED */
			if (!tmp || (tmp[0] != 0xff) || (tmp[1] != 0x03)) {
				/* Invalid SKB -- drop */
				if (tmp)
					module_printk(KERN_NOTICE, "Received invalid SKB (%02x, %02x)\n", tmp[0], tmp[1]);
				dev_kfree_skb_irq(skb);
			} else {
				skb_queue_tail(&ms->ppp_rq, skb);
				tasklet_schedule(&ms->ppp_calls);
			}
		}
#endif
	}
}

static inline void __dahdi_putbuf_chunk(struct dahdi_chan *ss, unsigned char *rxb)
{
	__putbuf_chunk(ss, rxb, DAHDI_CHUNKSIZE);
}

static void __dahdi_hdlc_abort(struct dahdi_chan *ss, int event)
{
	if (ss->inreadbuf >= 0)
		ss->readidx[ss->inreadbuf] = 0;
	if (test_bit(DAHDI_FLAGBIT_OPEN, &ss->flags) && !ss->span->alarms)
		__qevent(ss->master, event);
}

extern void dahdi_hdlc_abort(struct dahdi_chan *ss, int event)
{
	unsigned long flags;
	spin_lock_irqsave(&ss->lock, flags);
	__dahdi_hdlc_abort(ss, event);
	spin_unlock_irqrestore(&ss->lock, flags);
}

extern void dahdi_hdlc_putbuf(struct dahdi_chan *ss, unsigned char *rxb, int bytes)
{
	unsigned long flags;
	int res;
	int left;

	spin_lock_irqsave(&ss->lock, flags);
	if (ss->inreadbuf < 0) {
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "No place to receive HDLC frame\n");
#endif
		spin_unlock_irqrestore(&ss->lock, flags);
		return;
	}
	/* Read into the current buffer */
	left = ss->blocksize - ss->readidx[ss->inreadbuf];
	if (left > bytes)
		left = bytes;
	if (left > 0) {
		memcpy(ss->readbuf[ss->inreadbuf] + ss->readidx[ss->inreadbuf], rxb, left);
		rxb += left;
		ss->readidx[ss->inreadbuf] += left;
		bytes -= left;
	}
	/* Something isn't fit into buffer */
	if (bytes) {
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "HDLC frame isn't fit into buffer space\n");
#endif
		__dahdi_hdlc_abort(ss, DAHDI_EVENT_OVERRUN);
	}
	res = left;
	spin_unlock_irqrestore(&ss->lock, flags);
}

extern void dahdi_hdlc_finish(struct dahdi_chan *ss)
{
	int oldreadbuf;
	unsigned long flags;

	spin_lock_irqsave(&ss->lock, flags);

	if ((oldreadbuf = ss->inreadbuf) < 0) {
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "No buffers to finish\n");
#endif
		spin_unlock_irqrestore(&ss->lock, flags);
		return;
	}

	if (!ss->readidx[ss->inreadbuf]) {
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "Empty HDLC frame received\n");
#endif
		spin_unlock_irqrestore(&ss->lock, flags);
		return;
	}

	ss->readn[ss->inreadbuf] = ss->readidx[ss->inreadbuf];
	ss->inreadbuf = (ss->inreadbuf + 1) % ss->numbufs;
	if (ss->inreadbuf == ss->outreadbuf) {
		ss->inreadbuf = -1;
#ifdef CONFIG_DAHDI_DEBUG
		module_printk(KERN_NOTICE, "Notifying reader data in block %d\n", oldreadbuf);
#endif
		ss->rxdisable = 0;
	}
	if (ss->outreadbuf < 0) {
		ss->outreadbuf = oldreadbuf;
	}

	if (!ss->rxdisable) {
		wake_up_interruptible(&ss->readbufq);
		wake_up_interruptible(&ss->sel);
		if (ss->iomask & DAHDI_IOMUX_READ)
			wake_up_interruptible(&ss->eventbufq);
	}
	spin_unlock_irqrestore(&ss->lock, flags);
}

/* Returns 1 if EOF, 0 if data is still in frame, -1 if EOF and no buffers left */
extern int dahdi_hdlc_getbuf(struct dahdi_chan *ss, unsigned char *bufptr, unsigned int *size)
{
	unsigned char *buf;
	unsigned long flags;
	int left = 0;
	int res;
	int oldbuf;

	spin_lock_irqsave(&ss->lock, flags);
	if (ss->outwritebuf > -1) {
		buf = ss->writebuf[ss->outwritebuf];
		left = ss->writen[ss->outwritebuf] - ss->writeidx[ss->outwritebuf];
		/* Strip off the empty HDLC CRC end */
		left -= 2;
		if (left <= *size) {
			*size = left;
			res = 1;
		} else
			res = 0;

		memcpy(bufptr, &buf[ss->writeidx[ss->outwritebuf]], *size);
		ss->writeidx[ss->outwritebuf] += *size;

		if (res) {
			/* Rotate buffers */
			oldbuf = ss->outwritebuf;
			ss->writeidx[oldbuf] = 0;
			ss->writen[oldbuf] = 0;
			ss->outwritebuf = (ss->outwritebuf + 1) % ss->numbufs;
			if (ss->outwritebuf == ss->inwritebuf) {
				ss->outwritebuf = -1;
				if (ss->iomask & (DAHDI_IOMUX_WRITE | DAHDI_IOMUX_WRITEEMPTY))
					wake_up_interruptible(&ss->eventbufq);
				/* If we're only supposed to start when full, disable the transmitter */
				if (ss->txbufpolicy == DAHDI_POLICY_WHEN_FULL)
					ss->txdisable = 1;
				res = -1;
			}

			if (ss->inwritebuf < 0)
				ss->inwritebuf = oldbuf;

			if (!(ss->flags & (DAHDI_FLAG_NETDEV | DAHDI_FLAG_PPP))) {
				wake_up_interruptible(&ss->writebufq);
				wake_up_interruptible(&ss->sel);
				if ((ss->iomask & DAHDI_IOMUX_WRITE) && (res >= 0))
					wake_up_interruptible(&ss->eventbufq);
			}
		}
	} else {
		res = -1;
		*size = 0;
	}
	spin_unlock_irqrestore(&ss->lock, flags);

	return res;
}


static void process_timers(void)
{
	unsigned long flags;
	struct dahdi_timer *cur;
	spin_lock_irqsave(&zaptimerlock, flags);
	cur = zaptimers;
	while(cur) {
		if (cur->ms) {
			cur->pos -= DAHDI_CHUNKSIZE;
			if (cur->pos <= 0) {
				cur->tripped++;
				cur->pos = cur->ms;
				wake_up_interruptible(&cur->sel);
			}
		}
		cur = cur->next;
	}
	spin_unlock_irqrestore(&zaptimerlock, flags);
}

static unsigned int dahdi_timer_poll(struct file *file, struct poll_table_struct *wait_table)
{
	struct dahdi_timer *timer = file->private_data;
	unsigned long flags;
	int ret = 0;
	if (timer) {
		poll_wait(file, &timer->sel, wait_table);
		spin_lock_irqsave(&zaptimerlock, flags);
		if (timer->tripped || timer->ping) 
			ret |= POLLPRI;
		spin_unlock_irqrestore(&zaptimerlock, flags);
	} else
		ret = -EINVAL;
	return ret;
}

/* device poll routine */
static unsigned int
dahdi_chan_poll(struct file *file, struct poll_table_struct *wait_table, int unit)
{   
	
	struct dahdi_chan *chan = chans[unit];
	int	ret;
	unsigned long flags;

	  /* do the poll wait */
	if (chan) {
		poll_wait(file, &chan->sel, wait_table);
		ret = 0; /* start with nothing to return */
		spin_lock_irqsave(&chan->lock, flags);
		   /* if at least 1 write buffer avail */
		if (chan->inwritebuf > -1) {
			ret |= POLLOUT | POLLWRNORM;
		}
		if ((chan->outreadbuf > -1) && !chan->rxdisable) {
			ret |= POLLIN | POLLRDNORM;
		}
		if (chan->eventoutidx != chan->eventinidx)
		   {
			/* Indicate an exception */
			ret |= POLLPRI;
		   }
		spin_unlock_irqrestore(&chan->lock, flags);
	} else
		ret = -EINVAL;
	return(ret);  /* return what we found */
}

static int dahdi_mmap(struct file *file, struct vm_area_struct *vm)
{
	int unit = UNIT(file);
	if (unit == 250)
		return dahdi_transcode_fops->mmap(file, vm);
	return -ENOSYS;
}

static unsigned int dahdi_poll(struct file *file, struct poll_table_struct *wait_table)
{
	int unit = UNIT(file);
	struct dahdi_chan *chan;

	if (!unit)
		return -EINVAL;

	if (unit == 250)
		return dahdi_transcode_fops->poll(file, wait_table);

	if (unit == 253)
		return dahdi_timer_poll(file, wait_table);
		
	if (unit == 254) {
		chan = file->private_data;
		if (!chan)
			return -EINVAL;
		return dahdi_chan_poll(file, wait_table,chan->channo);
	}
	if (unit == 255) {
		chan = file->private_data;
		if (!chan) {
			module_printk(KERN_NOTICE, "No pseudo channel structure to read?\n");
			return -EINVAL;
		}
		return dahdi_chan_poll(file, wait_table, chan->channo);
	}
	return dahdi_chan_poll(file, wait_table, unit);
}

static void __dahdi_transmit_chunk(struct dahdi_chan *chan, unsigned char *buf)
{
	unsigned char silly[DAHDI_CHUNKSIZE];
	/* Called with chan->lock locked */
#ifdef	OPTIMIZE_CHANMUTE
	if(likely(chan->chanmute))
		return;
#endif
	if (!buf)
		buf = silly;
	__dahdi_getbuf_chunk(chan, buf);

	if ((chan->flags & DAHDI_FLAG_AUDIO) || (chan->confmode)) {
#ifdef CONFIG_DAHDI_MMX
		dahdi_kernel_fpu_begin();
#endif
		__dahdi_process_getaudio_chunk(chan, buf);
#ifdef CONFIG_DAHDI_MMX
		kernel_fpu_end();
#endif
	}
}

static inline void __dahdi_real_transmit(struct dahdi_chan *chan)
{
	/* Called with chan->lock held */
#ifdef	OPTIMIZE_CHANMUTE
	if(likely(chan->chanmute))
		return;
#endif
	if (chan->confmode) {
		/* Pull queued data off the conference */
		__buf_pull(&chan->confout, chan->writechunk, chan, "dahdi_real_transmit");
	} else {
		__dahdi_transmit_chunk(chan, chan->writechunk);
	}
}

static void __dahdi_getempty(struct dahdi_chan *ms, unsigned char *buf)
{
	int bytes = DAHDI_CHUNKSIZE;
	int left;
	unsigned char *txb = buf;
	int x;
	short getlin;
	/* Called with ms->lock held */

	while(bytes) {
		/* Receive silence, or tone */
		if (ms->curtone) {
			left = ms->curtone->tonesamples - ms->tonep;
			if (left > bytes)
				left = bytes;
			for (x=0;x<left;x++) {
				/* Pick our default value from the next sample of the current tone */
				getlin = dahdi_tone_nextsample(&ms->ts, ms->curtone);
				*(txb++) = DAHDI_LIN2X(getlin, ms);
			}
			ms->tonep+=left;
			bytes -= left;
			if (ms->tonep >= ms->curtone->tonesamples) {
				struct dahdi_tone *last;
				/* Go to the next sample of the tone */
				ms->tonep = 0;
				last = ms->curtone;
				ms->curtone = ms->curtone->next;
				if (!ms->curtone) {
					/* No more tones...  Is this dtmf or mf?  If so, go to the next digit */
					if (ms->dialing)
						__do_dtmf(ms);
				} else {
					if (last != ms->curtone)
						dahdi_init_tone_state(&ms->ts, ms->curtone);
				}
			}
		} else {
			/* Use silence */
			memset(txb, DAHDI_LIN2X(0, ms), bytes);
			bytes = 0;
		}
	}
		
}

static void __dahdi_receive_chunk(struct dahdi_chan *chan, unsigned char *buf)
{
	/* Receive chunk of audio -- called with chan->lock held */
	unsigned char waste[DAHDI_CHUNKSIZE];

#ifdef	OPTIMIZE_CHANMUTE
	if(likely(chan->chanmute))
		return;
#endif
	if (!buf) {
		memset(waste, DAHDI_LIN2X(0, chan), sizeof(waste));
		buf = waste;
	}
	if ((chan->flags & DAHDI_FLAG_AUDIO) || (chan->confmode)) {
#ifdef CONFIG_DAHDI_MMX                         
		dahdi_kernel_fpu_begin();
#endif
		__dahdi_process_putaudio_chunk(chan, buf);
#ifdef CONFIG_DAHDI_MMX
		kernel_fpu_end();
#endif
	}
	__dahdi_putbuf_chunk(chan, buf);
}

static inline void __dahdi_real_receive(struct dahdi_chan *chan)
{
	/* Called with chan->lock held */
#ifdef	OPTIMIZE_CHANMUTE
	if(likely(chan->chanmute))
		return;
#endif
	if (chan->confmode) {
		/* Load into queue if we have space */
		__buf_push(&chan->confin, chan->readchunk, "dahdi_real_receive");
	} else {
		__dahdi_receive_chunk(chan, chan->readchunk);
	}
}

int dahdi_transmit(struct dahdi_span *span)
{
	int x,y,z;
	unsigned long flags;

#if 1
	for (x=0;x<span->channels;x++) {
		spin_lock_irqsave(&span->chans[x].lock, flags);
		if (span->chans[x].flags & DAHDI_FLAG_NOSTDTXRX) {
			spin_unlock_irqrestore(&span->chans[x].lock, flags);
			continue;
		}
		if (&span->chans[x] == span->chans[x].master) {
			if (span->chans[x].otimer) {
				span->chans[x].otimer -= DAHDI_CHUNKSIZE;
				if (span->chans[x].otimer <= 0) {
					__rbs_otimer_expire(&span->chans[x]);
				}
			}
			if (span->chans[x].flags & DAHDI_FLAG_AUDIO) {
				__dahdi_real_transmit(&span->chans[x]);
			} else {
				if (span->chans[x].nextslave) {
					u_char data[DAHDI_CHUNKSIZE];
					int pos=DAHDI_CHUNKSIZE;
					/* Process master/slaves one way */
					for (y=0;y<DAHDI_CHUNKSIZE;y++) {
						/* Process slaves for this byte too */
						z = x;
						do {
							if (pos==DAHDI_CHUNKSIZE) {
								/* Get next chunk */
								__dahdi_transmit_chunk(&span->chans[x], data);
								pos = 0;
							}
							span->chans[z].writechunk[y] = data[pos++]; 
							z = span->chans[z].nextslave;
						} while(z);
					}
				} else {
					/* Process independents elsewise */
					__dahdi_real_transmit(&span->chans[x]);
				}
			}
			if (span->chans[x].sig == DAHDI_SIG_DACS_RBS) {
				if (chans[span->chans[x].confna]) {
				    	/* Just set bits for our destination */
					if (span->chans[x].txsig != chans[span->chans[x].confna]->rxsig) {
						span->chans[x].txsig = chans[span->chans[x].confna]->rxsig;
						span->rbsbits(&span->chans[x], chans[span->chans[x].confna]->rxsig);
					}
				}
			}

		}
		spin_unlock_irqrestore(&span->chans[x].lock, flags);
	}
	if (span->mainttimer) {
		span->mainttimer -= DAHDI_CHUNKSIZE;
		if (span->mainttimer <= 0) {
			span->mainttimer = 0;
			if (span->maint)
				span->maint(span, DAHDI_MAINT_LOOPSTOP);
			span->maintstat = 0;
			wake_up_interruptible(&span->maintq);
		}
	}
#endif
	return 0;
}

int dahdi_receive(struct dahdi_span *span)
{
	int x,y,z;
	unsigned long flags, flagso;

#if 1
#ifdef CONFIG_DAHDI_WATCHDOG
	span->watchcounter--;
#endif	
	for (x=0;x<span->channels;x++) {
		if (span->chans[x].master == &span->chans[x]) {
			spin_lock_irqsave(&span->chans[x].lock, flags);
			if (span->chans[x].nextslave) {
				/* Must process each slave at the same time */
				u_char data[DAHDI_CHUNKSIZE];
				int pos = 0;
				for (y=0;y<DAHDI_CHUNKSIZE;y++) {
					/* Put all its slaves, too */
					z = x;
					do {
						data[pos++] = span->chans[z].readchunk[y];
						if (pos == DAHDI_CHUNKSIZE) {
							if(!(span->chans[x].flags & DAHDI_FLAG_NOSTDTXRX))
								__dahdi_receive_chunk(&span->chans[x], data);
							pos = 0;
						}
						z=span->chans[z].nextslave;
					} while(z);
				}
			} else {
				/* Process a normal channel */
				if (!(span->chans[x].flags & DAHDI_FLAG_NOSTDTXRX))
					__dahdi_real_receive(&span->chans[x]);
			}
			if (span->chans[x].itimer) {
				span->chans[x].itimer -= DAHDI_CHUNKSIZE;
				if (span->chans[x].itimer <= 0) {
					rbs_itimer_expire(&span->chans[x]);
				}
			}
			if (span->chans[x].ringdebtimer)
				span->chans[x].ringdebtimer--;
			if (span->chans[x].sig & __DAHDI_SIG_FXS) {
				if (span->chans[x].rxhooksig == DAHDI_RXSIG_RING)
					span->chans[x].ringtrailer = DAHDI_RINGTRAILER;
				else if (span->chans[x].ringtrailer) {
					span->chans[x].ringtrailer-= DAHDI_CHUNKSIZE;
					/* See if RING trailer is expired */
					if (!span->chans[x].ringtrailer && !span->chans[x].ringdebtimer) 
						__qevent(&span->chans[x],DAHDI_EVENT_RINGOFFHOOK);
				}
			}
			if (span->chans[x].pulsetimer)
			{
				span->chans[x].pulsetimer--;
				if (span->chans[x].pulsetimer <= 0)
				{
					if (span->chans[x].pulsecount)
					{
						if (span->chans[x].pulsecount > 12) {
						
							module_printk(KERN_NOTICE, "Got pulse digit %d on %s???\n",
						    span->chans[x].pulsecount,
							span->chans[x].name);
						} else if (span->chans[x].pulsecount > 11) {
							__qevent(&span->chans[x], DAHDI_EVENT_PULSEDIGIT | '#');
						} else if (span->chans[x].pulsecount > 10) {
							__qevent(&span->chans[x], DAHDI_EVENT_PULSEDIGIT | '*');
						} else if (span->chans[x].pulsecount > 9) {
							__qevent(&span->chans[x], DAHDI_EVENT_PULSEDIGIT | '0');
						} else {
							__qevent(&span->chans[x], DAHDI_EVENT_PULSEDIGIT | ('0' + 
								span->chans[x].pulsecount));
						}
						span->chans[x].pulsecount = 0;
					}
				}
			}
			spin_unlock_irqrestore(&span->chans[x].lock, flags);
		}
	}

	if (span == master) {
		/* Hold the big zap lock for the duration of major
		   activities which touch all sorts of channels */
		spin_lock_irqsave(&bigzaplock, flagso);			
		/* Process any timers */
		process_timers();
		/* If we have dynamic stuff, call the ioctl with 0,0 parameters to
		   make it run */
		if (dahdi_dynamic_ioctl)
			dahdi_dynamic_ioctl(0,0);
		for (x=1;x<maxchans;x++) {
			if (chans[x] && chans[x]->confmode && !(chans[x]->flags & DAHDI_FLAG_PSEUDO)) {
				u_char *data;
				spin_lock_irqsave(&chans[x]->lock, flags);
				data = __buf_peek(&chans[x]->confin);
				__dahdi_receive_chunk(chans[x], data);
				if (data)
					__buf_pull(&chans[x]->confin, NULL,chans[x], "confreceive");
				spin_unlock_irqrestore(&chans[x]->lock, flags);
			}
		}
		/* This is the master channel, so make things switch over */
		rotate_sums();
		/* do all the pseudo and/or conferenced channel receives (getbuf's) */
		for (x=1;x<maxchans;x++) {
			if (chans[x] && (chans[x]->flags & DAHDI_FLAG_PSEUDO)) {
				spin_lock_irqsave(&chans[x]->lock, flags);
				__dahdi_transmit_chunk(chans[x], NULL);
				spin_unlock_irqrestore(&chans[x]->lock, flags);
			}
		}
		if (maxlinks) {
#ifdef CONFIG_DAHDI_MMX
			dahdi_kernel_fpu_begin();
#endif			
			  /* process all the conf links */
			for(x = 1; x <= maxlinks; x++) {
				  /* if we have a destination conf */
				if (((z = confalias[conf_links[x].dst]) > 0) &&
				    ((y = confalias[conf_links[x].src]) > 0)) {
					ACSS(conf_sums[z], conf_sums[y]);
				}
			}
#ifdef CONFIG_DAHDI_MMX
			kernel_fpu_end();
#endif			
		}
		/* do all the pseudo/conferenced channel transmits (putbuf's) */
		for (x=1;x<maxchans;x++) {
			if (chans[x] && (chans[x]->flags & DAHDI_FLAG_PSEUDO)) {
				unsigned char tmp[DAHDI_CHUNKSIZE];
				spin_lock_irqsave(&chans[x]->lock, flags);
				__dahdi_getempty(chans[x], tmp);
				__dahdi_receive_chunk(chans[x], tmp);
				spin_unlock_irqrestore(&chans[x]->lock, flags);
			}
		}
		for (x=1;x<maxchans;x++) {
			if (chans[x] && chans[x]->confmode && !(chans[x]->flags & DAHDI_FLAG_PSEUDO)) {
				u_char *data;
				spin_lock_irqsave(&chans[x]->lock, flags);
				data = __buf_pushpeek(&chans[x]->confout);
				__dahdi_transmit_chunk(chans[x], data);
				if (data)
					__buf_push(&chans[x]->confout, NULL, "conftransmit");
				spin_unlock_irqrestore(&chans[x]->lock, flags);
			}
		}
#ifdef	DAHDI_SYNC_TICK
		for (x=0;x<maxspans;x++) {
			struct dahdi_span	*s = spans[x];

			if (s && s->sync_tick)
				s->sync_tick(s, s == master);
		}
#endif
		spin_unlock_irqrestore(&bigzaplock, flagso);			
	}
#endif
	return 0;
}

MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
MODULE_DESCRIPTION("DAHDI Telephony Interface");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DAHDI_VERSION);

module_param(debug, int, 0644);
module_param(deftaps, int, 0644);

static struct file_operations dahdi_fops = {
	owner: THIS_MODULE,
	llseek: NULL,
	open: dahdi_open,
	release: dahdi_release,
	ioctl: dahdi_ioctl,
	read: dahdi_read,
	write: dahdi_write,
	poll: dahdi_poll,
	mmap: dahdi_mmap,
	flush: NULL,
	fsync: NULL,
	fasync: NULL,
};

#ifdef CONFIG_DAHDI_WATCHDOG
static struct timer_list watchdogtimer;

static void watchdog_check(unsigned long ignored)
{
	int x;
	unsigned long flags;
	static int wdcheck=0;
	
	local_irq_save(flags);
	for (x=0;x<maxspans;x++) {
		if (spans[x] && (spans[x]->flags & DAHDI_FLAG_RUNNING)) {
			if (spans[x]->watchcounter == DAHDI_WATCHDOG_INIT) {
				/* Whoops, dead card */
				if ((spans[x]->watchstate == DAHDI_WATCHSTATE_OK) || 
					(spans[x]->watchstate == DAHDI_WATCHSTATE_UNKNOWN)) {
					spans[x]->watchstate = DAHDI_WATCHSTATE_RECOVERING;
					if (spans[x]->watchdog) {
						module_printk(KERN_NOTICE, "Kicking span %s\n", spans[x]->name);
						spans[x]->watchdog(spans[x], DAHDI_WATCHDOG_NOINTS);
					} else {
						module_printk(KERN_NOTICE, "Span %s is dead with no revival\n", spans[x]->name);
						spans[x]->watchstate = DAHDI_WATCHSTATE_FAILED;
					}
				}
			} else {
				if ((spans[x]->watchstate != DAHDI_WATCHSTATE_OK) &&
					(spans[x]->watchstate != DAHDI_WATCHSTATE_UNKNOWN))
						module_printk(KERN_NOTICE, "Span %s is alive!\n", spans[x]->name);
				spans[x]->watchstate = DAHDI_WATCHSTATE_OK;
			}
			spans[x]->watchcounter = DAHDI_WATCHDOG_INIT;
		}
	}
	local_irq_restore(flags);
	if (!wdcheck) {
		module_printk(KERN_NOTICE, "watchdog on duty!\n");
		wdcheck=1;
	}
	mod_timer(&watchdogtimer, jiffies + 2);
}

static int __init watchdog_init(void)
{
	init_timer(&watchdogtimer);
	watchdogtimer.expires = 0;
	watchdogtimer.data =0;
	watchdogtimer.function = watchdog_check;
	/* Run every couple of jiffy or so */
	mod_timer(&watchdogtimer, jiffies + 2);
	return 0;
}

static void __exit watchdog_cleanup(void)
{
	del_timer(&watchdogtimer);
}

#endif

int dahdi_register_chardev(struct dahdi_chardev *dev)
{
	char udevname[strlen(dev->name) + 3];

	strcpy(udevname, "dahdi");
	strcat(udevname, dev->name);
	CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, dev->minor), NULL, udevname);

	return 0;
}

int dahdi_unregister_chardev(struct dahdi_chardev *dev)
{
	class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, dev->minor));

	return 0;
}

static int __init dahdi_init(void) {
	int res = 0;

#ifdef CONFIG_PROC_FS
	proc_entries[0] = proc_mkdir("dahdi", NULL);
#endif

	dahdi_class = class_create(THIS_MODULE, "dahdi");
	CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, 253), NULL, "dahditimer");
	CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, 254), NULL, "dahdichannel");
	CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, 255), NULL, "dahdipseudo");
	CLASS_DEV_CREATE(dahdi_class, MKDEV(DAHDI_MAJOR, 0), NULL, "dahdictl");

	if ((res = register_chrdev(DAHDI_MAJOR, "dahdi", &dahdi_fops))) {
		module_printk(KERN_ERR, "Unable to register DAHDI character device handler on %d\n", DAHDI_MAJOR);
		return res;
	}

	module_printk(KERN_INFO, "Telephony Interface Registered on major %d\n", DAHDI_MAJOR);
	module_printk(KERN_INFO, "Version: %s\n", DAHDI_VERSION);
	dahdi_conv_init();
	fasthdlc_precalc();
	rotate_sums();
#ifdef CONFIG_DAHDI_WATCHDOG
	watchdog_init();
#endif	
	return res;
}

static void __exit dahdi_cleanup(void) {
	int x;

	unregister_chrdev(DAHDI_MAJOR, "dahdi");

#ifdef CONFIG_PROC_FS
	remove_proc_entry("dahdi", NULL);
#endif

	module_printk(KERN_INFO, "Telephony Interface Unloaded\n");
	for (x = 0; x < DAHDI_TONE_ZONE_MAX; x++) {
		if (tone_zones[x])
			kfree(tone_zones[x]);
	}

	class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, 253)); /* timer */
	class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, 254)); /* channel */
	class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, 255)); /* pseudo */
	class_device_destroy(dahdi_class, MKDEV(DAHDI_MAJOR, 0)); /* ctl */
	class_destroy(dahdi_class);

#ifdef CONFIG_DAHDI_WATCHDOG
	watchdog_cleanup();
#endif
}

module_init(dahdi_init);
module_exit(dahdi_cleanup);
