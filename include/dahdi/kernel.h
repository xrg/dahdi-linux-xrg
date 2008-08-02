/*
 * DAHDI Telephony Interface
 *
 * Written by Mark Spencer <markster@linux-support.net>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001 - 2008 Digium, Inc.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
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

/*!
 * \file
 * \brief DAHDI kernel interface definitions
 */

#ifndef _DAHDI_KERNEL_H
#define _DAHDI_KERNEL_H

#include <dahdi/user.h>
#include <dahdi/fasthdlc.h>

#include "dahdi_config.h"
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/config.h>
#endif
#include <linux/fs.h>
#include <linux/ioctl.h>

#ifdef CONFIG_DAHDI_NET	
#include <linux/hdlc.h>
#endif

#ifdef CONFIG_DAHDI_PPP
#include <linux/ppp_channel.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#endif

#include <linux/poll.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
#define dahdi_pci_module pci_register_driver
#else
#define dahdi_pci_module pci_module_init
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
#define DAHDI_IRQ_HANDLER(a) static irqreturn_t a(int irq, void *dev_id)
#else
#define DAHDI_IRQ_HANDLER(a) static irqreturn_t a(int irq, void *dev_id, struct pt_regs *regs)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#define DAHDI_IRQ_SHARED IRQF_SHARED
#define DAHDI_IRQ_DISABLED IRQF_DISABLED
#define DAHDI_IRQ_SHARED_DISABLED IRQF_SHARED | IRQF_DISABLED
#else
#define DAHDI_IRQ_SHARED SA_SHIRQ
#define DAHDI_IRQ_DISABLED SA_INTERRUPT
#define DAHDI_IRQ_SHARED_DISABLED SA_SHIRQ | SA_INTERRUPT
#endif

/*! Default chunk size for conferences and such -- static right now, might make
   variable sometime.  8 samples = 1 ms = most frequent service interval possible
   for a USB device */
#define DAHDI_CHUNKSIZE		 8
#define DAHDI_MIN_CHUNKSIZE	 DAHDI_CHUNKSIZE
#define DAHDI_DEFAULT_CHUNKSIZE	 DAHDI_CHUNKSIZE
#define DAHDI_MAX_CHUNKSIZE 	 DAHDI_CHUNKSIZE
#define DAHDI_CB_SIZE		 2

#define RING_DEBOUNCE_TIME	2000	/*!< 2000 ms ring debounce time */

#include "ecdis.h"

typedef struct
{
	long	x1;
	long	x2;
	long	y1;
	long	y2;
	long	e1;
	long	e2;
	int	samps;
	int	lastdetect;
} sf_detect_state_t;

struct dahdi_tone_state {
	int v1_1;
	int v2_1;
	int v3_1;
	int v1_2;
	int v2_2;
	int v3_2;
	int modulate;
};

/*! \brief Conference queue structure */
struct confq {
	u_char buffer[DAHDI_CHUNKSIZE * DAHDI_CB_SIZE];
	u_char *buf[DAHDI_CB_SIZE];
	int inbuf;
	int outbuf;
};

struct dahdi_chan {
#ifdef CONFIG_DAHDI_NET
	/*! \note Must be first */
	struct dahdi_hdlc *hdlcnetdev;
#endif
#ifdef CONFIG_DAHDI_PPP
	struct ppp_channel *ppp;
	struct tasklet_struct ppp_calls;
	int do_ppp_wakeup;
	int do_ppp_error;
	struct sk_buff_head ppp_rq;
#endif
	spinlock_t lock;
	char name[40];
	/* Specified by DAHDI */
	/*! \brief DAHDI channel number */
	int channo;
	int chanpos;
	unsigned long flags;
	long rxp1;
	long rxp2;
	long rxp3;
	int txtone;
	int tx_v2;
	int tx_v3;
	int v1_1;
	int v2_1;
	int v3_1;
	int toneflags;
	sf_detect_state_t rd;

	struct dahdi_chan *master;	/*!< Our Master channel (could be us) */
	/*! \brief Next slave (if appropriate) */
	int nextslave;

	u_char *writechunk;						/*!< Actual place to write to */
	u_char swritechunk[DAHDI_MAX_CHUNKSIZE];	/*!< Buffer to be written */
	u_char *readchunk;						/*!< Actual place to read from */
	u_char sreadchunk[DAHDI_MAX_CHUNKSIZE];	/*!< Preallocated static area */
	short *readchunkpreec;

	/*! Pointer to tx and rx gain tables */
	u_char *rxgain;
	u_char *txgain;
	
	/*! Whether or not we have allocated gains or are using the default */
	int gainalloc;

	/* Specified by driver, readable by DAHDI */
	void *pvt;			/*!< Private channel data */
	struct file *file;	/*!< File structure */
	
	
	struct dahdi_span	*span;			/*!< Span we're a member of */
	int		sig;			/*!< Signalling */
	int		sigcap;			/*!< Capability for signalling */
	__u32		chan_alarms;		/*!< alarms status */

	/* Used only by DAHDI -- NO DRIVER SERVICEABLE PARTS BELOW */
	/* Buffer declarations */
	u_char		*readbuf[DAHDI_MAX_NUM_BUFS];	/*!< read buffer */
	int		inreadbuf;
	int		outreadbuf;
	wait_queue_head_t readbufq; /*!< read wait queue */

	u_char		*writebuf[DAHDI_MAX_NUM_BUFS]; /*!< write buffers */
	int		inwritebuf;
	int		outwritebuf;
	wait_queue_head_t writebufq; /*!< write wait queue */
	
	int		blocksize;	/*!< Block size */

	int		eventinidx;  /*!< out index in event buf (circular) */
	int		eventoutidx;  /*!< in index in event buf (circular) */
	unsigned int	eventbuf[DAHDI_MAX_EVENTSIZE];  /*!< event circ. buffer */
	wait_queue_head_t eventbufq; /*!< event wait queue */
	
	wait_queue_head_t txstateq;	/*!< waiting on the tx state to change */
	
	int		readn[DAHDI_MAX_NUM_BUFS];  /*!< # of bytes ready in read buf */
	int		readidx[DAHDI_MAX_NUM_BUFS];  /*!< current read pointer */
	int		writen[DAHDI_MAX_NUM_BUFS];  /*!< # of bytes ready in write buf */
	int		writeidx[DAHDI_MAX_NUM_BUFS];  /*!< current write pointer */
	
	int		numbufs;			/*!< How many buffers in channel */
	int		txbufpolicy;			/*!< Buffer policy */
	int		rxbufpolicy;			/*!< Buffer policy */
	int		txdisable;				/*!< Disable transmitter */
	int 	rxdisable;				/*!< Disable receiver */
	
	
	/* Tone zone stuff */
	struct dahdi_zone *curzone;		/*!< Zone for selecting tones */
	int 	tonezone;				/*!< Tone zone for this channel */
	struct dahdi_tone *curtone;		/*!< Current tone we're playing (if any) */
	int		tonep;					/*!< Current position in tone */
	struct dahdi_tone_state ts;		/*!< Tone state */

	/* Pulse dial stuff */
	int	pdialcount;			/*!< pulse dial count */

	/*! Ring cadence */
	int ringcadence[DAHDI_MAX_CADENCE];
	int firstcadencepos;				/*!< Where to restart ring cadence */

	/* Digit string dialing stuff */
	int		digitmode;			/*!< What kind of tones are we sending? */
	char	txdialbuf[DAHDI_MAX_DTMF_BUF];
	int 	dialing;
	int	afterdialingtimer;
	int		cadencepos;				/*!< Where in the cadence we are */

	/* I/O Mask */	
	int		iomask;  /*! I/O Mux signal mask */
	wait_queue_head_t sel;	/*! thingy for select stuff */
	
	/* HDLC state machines */
	struct fasthdlc_state txhdlc;
	struct fasthdlc_state rxhdlc;
	int infcs;

	/* Conferencing stuff */
	int		confna;	/*! conference number (alias) */
	int		_confn;	/*! Actual conference number */
	int		confmode;  /*! conference mode */
	int		confmute; /*! conference mute mode */

	/* Incoming and outgoing conference chunk queues for
	   communicating between DAHDI master time and
	   other boards */
	struct confq confin;
	struct confq confout;

	short	getlin[DAHDI_MAX_CHUNKSIZE];			/*!< Last transmitted samples */
	unsigned char getraw[DAHDI_MAX_CHUNKSIZE];		/*!< Last received raw data */
	short	getlin_lastchunk[DAHDI_MAX_CHUNKSIZE];	/*!< Last transmitted samples from last chunk */
	short	putlin[DAHDI_MAX_CHUNKSIZE];			/*!< Last received samples */
	unsigned char putraw[DAHDI_MAX_CHUNKSIZE];		/*!< Last received raw data */
	short	conflast[DAHDI_MAX_CHUNKSIZE];			/*!< Last conference sample -- base part of channel */
	short	conflast1[DAHDI_MAX_CHUNKSIZE];		/*!< Last conference sample  -- pseudo part of channel */
	short	conflast2[DAHDI_MAX_CHUNKSIZE];		/*!< Previous last conference sample -- pseudo part of channel */
	

	/*! Is echo cancellation enabled or disabled */
	int		echocancel;
	/*! The echo canceler module that should be used to create an
	   instance when this channel needs one */
	const struct dahdi_echocan *ec_factory;
	/*! The echo canceler module that owns the instance currently
	   on this channel, if one is present */
	const struct dahdi_echocan *ec_current;
	/*! The private state data of the echo canceler instance in use */
	struct echo_can_state *ec_state;
	echo_can_disable_detector_state_t txecdis;
	echo_can_disable_detector_state_t rxecdis;
	
	int		echostate;		/*!< State of echo canceller */
	int		echolastupdate;		/*!< Last echo can update pos */
	int		echotimer;		/*!< Timer for echo update */

	/* RBS timings  */
	int		prewinktime;  /*!< pre-wink time (ms) */
	int		preflashtime;	/*!< pre-flash time (ms) */
	int		winktime;  /*!< wink time (ms) */
	int		flashtime;  /*!< flash time (ms) */
	int		starttime;  /*!< start time (ms) */
	int		rxwinktime;  /*!< rx wink time (ms) */
	int		rxflashtime; /*!< rx flash time (ms) */
	int		debouncetime;  /*!< FXS GS sig debounce time (ms) */
	int		pulsebreaktime; /*!< pulse line open time (ms) */
	int		pulsemaketime;  /*!< pulse line closed time (ms) */
	int		pulseaftertime; /*!< pulse time between digits (ms) */

	/*! RING debounce timer */
	int	ringdebtimer;
	
	/*! RING trailing detector to make sure a RING is really over */
	int ringtrailer;

	/* PULSE digit receiver stuff */
	int	pulsecount;
	int	pulsetimer;

	/* RBS timers */
	int 	itimerset;		/*!< what the itimer was set to last */
	int 	itimer;
	int 	otimer;
	
	/* RBS state */
	int gotgs;
	int txstate;
	int rxsig;
	int txsig;
	int rxsigstate;

	/* non-RBS rx state */
	int rxhooksig;
	int txhooksig;
	int kewlonhook;

	/*! Idle signalling if CAS signalling */
	int idlebits;

	int deflaw;		/*! 1 = mulaw, 2=alaw, 0=undefined */
	short *xlaw;
#ifdef	OPTIMIZE_CHANMUTE
	int chanmute;		/*!< no need for PCM data */
#endif
#ifdef CONFIG_CALC_XLAW
	unsigned char (*lineartoxlaw)(short a);
#else
	unsigned char *lin2x;
#endif
};

#ifdef CONFIG_DAHDI_NET
struct dahdi_hdlc {
	struct net_device *netdev;
	struct dahdi_chan *chan;
};
#endif

/* Echo cancellation */
struct echo_can_state;

struct dahdi_echocan {
	const char *name;
	struct module *owner;
	int (*echo_can_create)(struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p, struct echo_can_state **ec);
	void (*echo_can_free)(struct echo_can_state *ec);
	void (*echo_can_array_update)(struct echo_can_state *ec, short *iref, short *isig);
	int (*echo_can_traintap)(struct echo_can_state *ec, int pos, short val);
};

int dahdi_register_echocan(const struct dahdi_echocan *ec);
void dahdi_unregister_echocan(const struct dahdi_echocan *ec);

/*! Define the maximum block size */
#define DAHDI_MAX_BLOCKSIZE	8192

/*! Define the default network block size */
#define DAHDI_DEFAULT_MTU_MRU	2048


#define DAHDI_DEFAULT_WINKTIME	150	/*!< 150 ms default wink time */
#define DAHDI_DEFAULT_FLASHTIME	750	/*!< 750 ms default flash time */

#define DAHDI_DEFAULT_PREWINKTIME	50	/*!< 50 ms before wink */
#define DAHDI_DEFAULT_PREFLASHTIME 50	/*!< 50 ms before flash */
#define DAHDI_DEFAULT_STARTTIME 1500	/*!< 1500 ms of start */
#define DAHDI_DEFAULT_RINGTIME 2000	/*!< 2000 ms of ring on (start, FXO) */
#if 0
#define DAHDI_DEFAULT_RXWINKTIME 250	/*!< 250ms longest rx wink */
#endif
#define DAHDI_DEFAULT_RXWINKTIME 300	/*!< 300ms longest rx wink (to work with the Atlas) */
#define DAHDI_DEFAULT_RXFLASHTIME 1250	/*!< 1250ms longest rx flash */
#define DAHDI_DEFAULT_DEBOUNCETIME 600	/*!< 600ms of FXS GS signalling debounce */
#define DAHDI_DEFAULT_PULSEMAKETIME 50	/*!< 50 ms of line closed when dial pulsing */
#define DAHDI_DEFAULT_PULSEBREAKTIME 50	/*!< 50 ms of line open when dial pulsing */
#define DAHDI_DEFAULT_PULSEAFTERTIME 750	/*!< 750ms between dial pulse digits */

#define DAHDI_MINPULSETIME (15 * 8)	/*!< 15 ms minimum */

#ifdef SHORT_FLASH_TIME
#define DAHDI_MAXPULSETIME (80 * 8)	/*!< we need 80 ms, not 200ms, as we have a short flash */
#else
#define DAHDI_MAXPULSETIME (200 * 8)	/*!< 200 ms maximum */
#endif

#define DAHDI_PULSETIMEOUT ((DAHDI_MAXPULSETIME / 8) + 50)

#define DAHDI_RINGTRAILER (50 * 8)	/*!< Don't consider a ring "over" until it's been gone at least this
									   much time */

#define DAHDI_LOOPCODE_TIME 10000		/*!< send loop codes for 10 secs */
#define DAHDI_ALARMSETTLE_TIME	5000	/*!< allow alarms to settle for 5 secs */
#define DAHDI_AFTERSTART_TIME 500		/*!< 500ms after start */

#define DAHDI_RINGOFFTIME 4000		/*!< Turn off ringer for 4000 ms */
#define DAHDI_KEWLTIME 500		/*!< 500ms for kewl pulse */
#define DAHDI_AFTERKEWLTIME 300    /*!< 300ms after kewl pulse */

#define DAHDI_MAX_PRETRAINING   1000	/*!< 1000ms max pretraining time */

#ifdef	FXSFLASH
#define DAHDI_FXSFLASHMINTIME	450	/*!< min 450ms */
#define DAHDI_FXSFLASHMAXTIME	550	/*!< max 550ms */
#endif


struct dahdi_chardev {
	const char *name;
	__u8 minor;
};

int dahdi_register_chardev(struct dahdi_chardev *dev);
int dahdi_unregister_chardev(struct dahdi_chardev *dev);

/*! \brief defines for transmit signalling */
typedef enum {
	DAHDI_TXSIG_ONHOOK,  /*!< On hook */
	DAHDI_TXSIG_OFFHOOK, /*!< Off hook */
	DAHDI_TXSIG_START,   /*!< Start / Ring */
	DAHDI_TXSIG_KEWL     /*!< Drop battery if possible */
} dahdi_txsig_t;

typedef enum {
	DAHDI_RXSIG_ONHOOK,
	DAHDI_RXSIG_OFFHOOK,
	DAHDI_RXSIG_START,
	DAHDI_RXSIG_RING,
	DAHDI_RXSIG_INITIAL
} dahdi_rxsig_t;
	
/* Span flags */
#define DAHDI_FLAG_REGISTERED		(1 << 0)
#define DAHDI_FLAG_RUNNING			(1 << 1)
#define DAHDI_FLAG_RBS			(1 << 12)	/*!< Span uses RBS signalling */

/* Channel flags */
#define DAHDI_FLAG_DTMFDECODE		(1 << 2)	/*!< Channel supports native DTMF decode */
#define DAHDI_FLAG_MFDECODE		(1 << 3)	/*!< Channel supports native MFr2 decode */
#define DAHDI_FLAG_ECHOCANCEL		(1 << 4)	/*!< Channel supports native echo cancellation */

#define DAHDI_FLAG_HDLC			(1 << 5)	/*!< Perform HDLC */
#define DAHDI_FLAG_NETDEV			(1 << 6)	/*!< Send to network */
#define DAHDI_FLAG_PSEUDO			(1 << 7)	/*!< Pseudo channel */
#define DAHDI_FLAG_CLEAR			(1 << 8)	/*!< Clear channel */
#define DAHDI_FLAG_AUDIO			(1 << 9)	/*!< Audio mode channel */

#define DAHDI_FLAG_OPEN			(1 << 10)	/*!< Channel is open */
#define DAHDI_FLAG_FCS			(1 << 11)	/*!< Calculate FCS */
/* Reserve 12 for uniqueness with span flags */
#define DAHDI_FLAG_LINEAR			(1 << 13)	/*!< Talk to user space in linear */
#define DAHDI_FLAG_PPP			(1 << 14)	/*!< PPP is available */
#define DAHDI_FLAG_T1PPP			(1 << 15)
#define DAHDI_FLAG_SIGFREEZE		(1 << 16)	/*!< Freeze signalling */
#define DAHDI_FLAG_NOSTDTXRX		(1 << 17)	/*!< Do NOT do standard transmit and receive on every interrupt */
#define DAHDI_FLAG_LOOPED			(1 << 18)	/*!< Loopback the receive data from the channel to the transmit */
#define DAHDI_FLAG_MTP2			(1 << 19)	/*!< Repeats last message in buffer and also discards repeating messages sent to us */

/*! This is a redefinition of the flags from above to allow use of the kernel atomic bit testing and changing routines.
 * See the above descriptions for DAHDI_FLAG_....  for documentation about function. */
enum {
	DAHDI_FLAGBIT_REGISTERED = 0,
	DAHDI_FLAGBIT_RUNNING    = 1,
	DAHDI_FLAGBIT_RBS	      = 12,
	DAHDI_FLAGBIT_DTMFDECODE = 2,
	DAHDI_FLAGBIT_MFDECODE   = 3,
	DAHDI_FLAGBIT_ECHOCANCEL = 4,
	DAHDI_FLAGBIT_HDLC	      = 5,
	DAHDI_FLAGBIT_NETDEV     = 6,
	DAHDI_FLAGBIT_PSEUDO     = 7,
	DAHDI_FLAGBIT_CLEAR      = 8,
	DAHDI_FLAGBIT_AUDIO      = 9,
	DAHDI_FLAGBIT_OPEN	      = 10,
	DAHDI_FLAGBIT_FCS	      = 11,
	DAHDI_FLAGBIT_LINEAR     = 13,
	DAHDI_FLAGBIT_PPP	      = 14,
	DAHDI_FLAGBIT_T1PPP      = 15,
	DAHDI_FLAGBIT_SIGFREEZE  = 16,
	DAHDI_FLAGBIT_NOSTDTXRX  = 17,
	DAHDI_FLAGBIT_LOOPED     = 18,
	DAHDI_FLAGBIT_MTP2       = 19,
};

struct dahdi_span {
	spinlock_t lock;
	void *pvt;			/*!< Private stuff */
	char name[40];			/*!< Span name */
	char desc[80];			/*!< Span description */
	const char *spantype;		/*!< span type in text form */
	const char *manufacturer;	/*!< span's device manufacturer */
	char devicetype[80];		/*!< span's device type */
	char location[40];		/*!< span device's location in system */
	int deflaw;			/*!< Default law (DAHDI_MULAW or DAHDI_ALAW) */
	int alarms;			/*!< Pending alarms on span */
	int flags;
	int irq;			/*!< IRQ for this span's hardware */
	int lbo;			/*!< Span Line-Buildout */
	int lineconfig;			/*!< Span line configuration */
	int linecompat;			/*!< Span line compatibility */
	int channels;			/*!< Number of channels in span */
	int txlevel;			/*!< Tx level */
	int rxlevel;			/*!< Rx level */
	int syncsrc;			/*!< current sync src (gets copied here) */
	unsigned int bpvcount;		/*!< BPV counter */
	unsigned int crc4count;	        /*!< CRC4 error counter */
	unsigned int ebitcount;		/*!< current E-bit error count */
	unsigned int fascount;		/*!< current FAS error count */

	int maintstat;			/*!< Maintenance state */
	wait_queue_head_t maintq;	/*!< Maintenance queue */
	int mainttimer;			/*!< Maintenance timer */
	
	int irqmisses;			/*!< Interrupt misses */

	int timingslips;			/*!< Clock slips */

	struct dahdi_chan **chans;		/*!< Member channel structures */

	/*   ==== Span Callback Operations ====   */
	/*! Req: Set the requested chunk size.  This is the unit in which you must
	   report results for conferencing, etc */
	int (*setchunksize)(struct dahdi_span *span, int chunksize);

	/*! Opt: Configure the span (if appropriate) */
	int (*spanconfig)(struct dahdi_span *span, struct dahdi_lineconfig *lc);
	
	/*! Opt: Start the span */
	int (*startup)(struct dahdi_span *span);
	
	/*! Opt: Shutdown the span */
	int (*shutdown)(struct dahdi_span *span);
	
	/*! Opt: Enable maintenance modes */
	int (*maint)(struct dahdi_span *span, int mode);

#ifdef	DAHDI_SYNC_TICK
	/*! Opt: send sync to spans */
	int (*sync_tick)(struct dahdi_span *span, int is_master);
#endif

	/* ====  Channel Callback Operations ==== */
	/*! Opt: Set signalling type (if appropriate) */
	int (*chanconfig)(struct dahdi_chan *chan, int sigtype);

	/*! Opt: Prepare a channel for I/O */
	int (*open)(struct dahdi_chan *chan);

	/*! Opt: Close channel for I/O */
	int (*close)(struct dahdi_chan *chan);
	
	/*! Opt: IOCTL */
	int (*ioctl)(struct dahdi_chan *chan, unsigned int cmd, unsigned long data);
	
	/*! Opt: Native echo cancellation (simple) */
	int (*echocan)(struct dahdi_chan *chan, int ecval);

	int (*echocan_with_params)(struct dahdi_chan *chan, struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p);

	/* Okay, now we get to the signalling.  You have several options: */

	/* Option 1: If you're a T1 like interface, you can just provide a
	   rbsbits function and we'll assert robbed bits for you.  Be sure to 
	   set the DAHDI_FLAG_RBS in this case.  */

	/*! Opt: If the span uses A/B bits, set them here */
	int (*rbsbits)(struct dahdi_chan *chan, int bits);
	
	/*! Option 2: If you don't know about sig bits, but do have their
	   equivalents (i.e. you can disconnect battery, detect off hook,
	   generate ring, etc directly) then you can just specify a
	   sethook function, and we'll call you with appropriate hook states
	   to set.  Still set the DAHDI_FLAG_RBS in this case as well */
	int (*hooksig)(struct dahdi_chan *chan, dahdi_txsig_t hookstate);
	
	/*! Option 3: If you can't use sig bits, you can write a function
	   which handles the individual hook states  */
	int (*sethook)(struct dahdi_chan *chan, int hookstate);
	
	/*! Opt: Dacs the contents of chan2 into chan1 if possible */
	int (*dacs)(struct dahdi_chan *chan1, struct dahdi_chan *chan2);

	/*! Opt: Used to tell an onboard HDLC controller that there is data ready to transmit */
	void (*hdlc_hard_xmit)(struct dahdi_chan *chan);

	/* Used by DAHDI only -- no user servicable parts inside */
	int spanno;			/*!< Span number for DAHDI */
	int offset;			/*!< Offset within a given card */
	int lastalarms;		/*!< Previous alarms */
	/*! If the watchdog detects no received data, it will call the
	   watchdog routine */
	int (*watchdog)(struct dahdi_span *span, int cause);
#ifdef CONFIG_DAHDI_WATCHDOG
	int watchcounter;
	int watchstate;
#endif	
};

struct dahdi_transcoder_channel {
	void *pvt;
	struct dahdi_transcoder *parent;
	wait_queue_head_t ready;
	int errorstatus;
	int offset;
	unsigned int chan_built;
	unsigned int built_fmts;
	unsigned int flags;
	unsigned int srcfmt;
	unsigned int dstfmt;
	struct dahdi_transcode_header *tch;
};

#define DAHDI_TC_FLAG_BUSY       (1 << 0)
#define DAHDI_TC_FLAG_TRANSIENT  (1 << 1)


struct dahdi_transcoder {
	struct dahdi_transcoder *next;
	char name[80];
	int numchannels;
	unsigned int srcfmts;
	unsigned int dstfmts;
	int (*operation)(struct dahdi_transcoder_channel *channel, int op);
	/*! Transcoder channels */
	struct dahdi_transcoder_channel channels[0];
};

#define DAHDI_WATCHDOG_NOINTS		(1 << 0)

#define DAHDI_WATCHDOG_INIT			1000

#define DAHDI_WATCHSTATE_UNKNOWN		0
#define DAHDI_WATCHSTATE_OK			1
#define DAHDI_WATCHSTATE_RECOVERING	2
#define DAHDI_WATCHSTATE_FAILED		3


struct dahdi_dynamic_driver {
	/*! Driver name (e.g. Eth) */
	char name[20];

	/*! Driver description */
	char desc[80];

	/*! Create a new transmission pipe */
	void *(*create)(struct dahdi_span *span, char *address);

	/*! Destroy a created transmission pipe */
	void (*destroy)(void *tpipe);

	/*! Transmit a given message */
	int (*transmit)(void *tpipe, unsigned char *msg, int msglen);

	/*! Flush any pending messages */
	int (*flush)(void);

	struct dahdi_dynamic_driver *next;
};

/*! \brief Receive a dynamic span message */
void dahdi_dynamic_receive(struct dahdi_span *span, unsigned char *msg, int msglen);

/*! \brief Register a dynamic driver */
int dahdi_dynamic_register(struct dahdi_dynamic_driver *driver);

/*! \brief Unregister a dynamic driver */
void dahdi_dynamic_unregister(struct dahdi_dynamic_driver *driver);

/*! Receive on a span.  The DAHDI interface will handle all the calculations for
   all member channels of the span, pulling the data from the readchunk buffer */
int dahdi_receive(struct dahdi_span *span);

/*! Prepare writechunk buffers on all channels for this span */
int dahdi_transmit(struct dahdi_span *span);

/*! Abort the buffer currently being receive with event "event" */
void dahdi_hdlc_abort(struct dahdi_chan *ss, int event);

/*! Indicate to DAHDI that the end of frame was received and rotate buffers */
void dahdi_hdlc_finish(struct dahdi_chan *ss);

/*! Put a chunk of data into the current receive buffer */
void dahdi_hdlc_putbuf(struct dahdi_chan *ss, unsigned char *rxb, int bytes);

/*! Get a chunk of data from the current transmit buffer.  Returns -1 if no data
 * is left to send, 0 if there is data remaining in the current message to be sent
 * and 1 if the currently transmitted message is now done */
int dahdi_hdlc_getbuf(struct dahdi_chan *ss, unsigned char *bufptr, unsigned int *size);


/*! Register a span.  Returns 0 on success, -1 on failure.  Pref-master is non-zero if
   we should have preference in being the master device */
int dahdi_register(struct dahdi_span *span, int prefmaster);

/*! Allocate / free memory for a transcoder */
struct dahdi_transcoder *dahdi_transcoder_alloc(int numchans);
void dahdi_transcoder_free(struct dahdi_transcoder *ztc);

/*! \brief Register a transcoder */
int dahdi_transcoder_register(struct dahdi_transcoder *tc);

/*! \brief Unregister a transcoder */
int dahdi_transcoder_unregister(struct dahdi_transcoder *tc);

/*! \brief Alert a transcoder */
int dahdi_transcoder_alert(struct dahdi_transcoder_channel *ztc);

/*! \brief Unregister a span */
int dahdi_unregister(struct dahdi_span *span);

/*! \brief Gives a name to an LBO */
char *dahdi_lboname(int lbo);

/*! \brief Tell DAHDI about changes in received rbs bits */
void dahdi_rbsbits(struct dahdi_chan *chan, int bits);

/*! \brief Tell DAHDI abou changes in received signalling */
void dahdi_hooksig(struct dahdi_chan *chan, dahdi_rxsig_t rxsig);

/*! \brief Queue an event on a channel */
void dahdi_qevent_nolock(struct dahdi_chan *chan, int event);

/*! \brief Queue an event on a channel, locking it first */
void dahdi_qevent_lock(struct dahdi_chan *chan, int event);

/*! \brief Notify a change possible change in alarm status on a channel */
void dahdi_alarm_channel(struct dahdi_chan *chan, int alarms);

/*! \brief Notify a change possible change in alarm status on a span */
void dahdi_alarm_notify(struct dahdi_span *span);

/*! \brief Initialize a tone state */
void dahdi_init_tone_state(struct dahdi_tone_state *ts, struct dahdi_tone *zt);

/*! \brief Get a given MF tone struct, suitable for dahdi_tone_nextsample. */
struct dahdi_tone *dahdi_mf_tone(const struct dahdi_chan *chan, char digit, int digitmode);

/* Echo cancel a receive and transmit chunk for a given channel.  This
   should be called by the low-level driver as close to the interface
   as possible.  ECHO CANCELLATION IS NO LONGER AUTOMATICALLY DONE
   AT THE DAHDI LEVEL.  dahdi_ec_chunk will not echo cancel if it should
   not be doing so.  rxchunk is modified in-place */

void dahdi_ec_chunk(struct dahdi_chan *chan, unsigned char *rxchunk, const unsigned char *txchunk);
void dahdi_ec_span(struct dahdi_span *span);

extern struct file_operations *dahdi_transcode_fops;

/* Don't use these directly -- they're not guaranteed to
   be there. */
extern short __dahdi_mulaw[256];
extern short __dahdi_alaw[256];
#ifdef CONFIG_CALC_XLAW
u_char __dahdi_lineartoulaw(short a);
u_char __dahdi_lineartoalaw(short a);
#else
extern u_char __dahdi_lin2mu[16384];
extern u_char __dahdi_lin2a[16384];
#endif

/*! \brief Used by dynamic DAHDI -- don't use directly */
void dahdi_set_dynamic_ioctl(int (*func)(unsigned int cmd, unsigned long data));

/*! \brief Used by DAHDI HPEC module -- don't use directly */
void dahdi_set_hpec_ioctl(int (*func)(unsigned int cmd, unsigned long data));

/*! \brief Used privately by DAHDI.  Avoid touching directly */
struct dahdi_tone {
	int fac1;
	int init_v2_1;
	int init_v3_1;

	int fac2;
	int init_v2_2;
	int init_v3_2;

	int tonesamples;		/*!< How long to play this tone before 
					   going to the next (in samples) */
	struct dahdi_tone *next;		/* Next tone in this sequence */

	int modulate;
};

static inline short dahdi_tone_nextsample(struct dahdi_tone_state *ts, struct dahdi_tone *zt)
{
	/* follow the curves, return the sum */

	int p;

	ts->v1_1 = ts->v2_1;
	ts->v2_1 = ts->v3_1;
	ts->v3_1 = (zt->fac1 * ts->v2_1 >> 15) - ts->v1_1;

	ts->v1_2 = ts->v2_2;
	ts->v2_2 = ts->v3_2;
	ts->v3_2 = (zt->fac2 * ts->v2_2 >> 15) - ts->v1_2;

	/* Return top 16 bits */
	if (!ts->modulate) return ts->v3_1 + ts->v3_2;
	/* we are modulating */
	p = ts->v3_2 - 32768;
	if (p < 0) p = -p;
	p = ((p * 9) / 10) + 1;
	return (ts->v3_1 * p) >> 15;

}

static inline short dahdi_txtone_nextsample(struct dahdi_chan *ss)
{
	/* follow the curves, return the sum */

	ss->v1_1 = ss->v2_1;
	ss->v2_1 = ss->v3_1;
	ss->v3_1 = (ss->txtone * ss->v2_1 >> 15) - ss->v1_1;
	return ss->v3_1;
}

/* These are the right functions to use.  */

#define DAHDI_MULAW(a) (__dahdi_mulaw[(a)])
#define DAHDI_ALAW(a) (__dahdi_alaw[(a)])
#define DAHDI_XLAW(a,c) (c->xlaw[(a)])

#ifdef CONFIG_CALC_XLAW
#define DAHDI_LIN2MU(a) (__dahdi_lineartoulaw((a)))
#define DAHDI_LIN2A(a) (__dahdi_lineartoalaw((a)))

#define DAHDI_LIN2X(a,c) ((c)->lineartoxlaw((a)))

#else
/* Use tables */
#define DAHDI_LIN2MU(a) (__dahdi_lin2mu[((unsigned short)(a)) >> 2])
#define DAHDI_LIN2A(a) (__dahdi_lin2a[((unsigned short)(a)) >> 2])

/* Manipulate as appropriate for x-law */
#define DAHDI_LIN2X(a,c) ((c)->lin2x[((unsigned short)(a)) >> 2])

#endif /* CONFIG_CALC_XLAW */

/* Data formats for capabilities and frames alike (from Asterisk) */
/*! G.723.1 compression */
#define DAHDI_FORMAT_G723_1	(1 << 0)
/*! GSM compression */
#define DAHDI_FORMAT_GSM		(1 << 1)
/*! Raw mu-law data (G.711) */
#define DAHDI_FORMAT_ULAW		(1 << 2)
/*! Raw A-law data (G.711) */
#define DAHDI_FORMAT_ALAW		(1 << 3)
/*! ADPCM (G.726, 32kbps) */
#define DAHDI_FORMAT_G726		(1 << 4)
/*! ADPCM (IMA) */
#define DAHDI_FORMAT_ADPCM		(1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define DAHDI_FORMAT_SLINEAR	(1 << 6)
/*! LPC10, 180 samples/frame */
#define DAHDI_FORMAT_LPC10		(1 << 7)
/*! G.729A audio */
#define DAHDI_FORMAT_G729A		(1 << 8)
/*! SpeeX Free Compression */
#define DAHDI_FORMAT_SPEEX		(1 << 9)
/*! iLBC Free Compression */
#define DAHDI_FORMAT_ILBC		(1 << 10)
/*! Maximum audio format */
#define DAHDI_FORMAT_MAX_AUDIO	(1 << 15)
/*! Maximum audio mask */
#define DAHDI_FORMAT_AUDIO_MASK	((1 << 16) - 1)

#endif /* _DAHDI_KERNEL_H */
