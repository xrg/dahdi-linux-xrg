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

#ifndef _LINUX_DAHDI_H
#define _LINUX_DAHDI_H

#ifdef __KERNEL__
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

#include "ecdis.h"
#include "fasthdlc.h"

#endif /* __KERNEL__ */

#include <linux/types.h>

#ifndef ELAST
#define ELAST 500
#endif

/* Per-span configuration values */
#define	DAHDI_CONFIG_TXLEVEL	7	/* bits 0-2 are tx level */

/* Line configuration */
/* These apply to T1 */
#define DAHDI_CONFIG_D4	 (1 << 4)	
#define DAHDI_CONFIG_ESF	 (1 << 5)
#define DAHDI_CONFIG_AMI	 (1 << 6)
#define DAHDI_CONFIG_B8ZS 	 (1 << 7)
/* These apply to E1 */
#define	DAHDI_CONFIG_CCS	 (1 << 8)	/* CCS (ISDN) instead of CAS (Robbed Bit) */
#define	DAHDI_CONFIG_HDB3	 (1 << 9)	/* HDB3 instead of AMI (line coding) */
#define	DAHDI_CONFIG_CRC4   (1 << 10)	/* CRC4 framing */
#define DAHDI_CONFIG_NOTOPEN (1 << 16)

/* Signalling types */
#define DAHDI_SIG_BROKEN	(1 << 31)	/* The port is broken and/or failed initialization */

#define __DAHDI_SIG_FXO	(1 << 12)	/* Never use directly */
#define __DAHDI_SIG_FXS	(1 << 13)	/* Never use directly */

#define DAHDI_SIG_NONE		(0)			/* Channel not configured */
#define DAHDI_SIG_FXSLS	((1 << 0) | __DAHDI_SIG_FXS)	/* FXS, Loopstart */
#define DAHDI_SIG_FXSGS	((1 << 1) | __DAHDI_SIG_FXS)	/* FXS, Groundstart */
#define DAHDI_SIG_FXSKS	((1 << 2) | __DAHDI_SIG_FXS)	/* FXS, Kewlstart */

#define DAHDI_SIG_FXOLS	((1 << 3) | __DAHDI_SIG_FXO)	/* FXO, Loopstart */
#define DAHDI_SIG_FXOGS	((1 << 4) | __DAHDI_SIG_FXO)	/* FXO, Groupstart */
#define DAHDI_SIG_FXOKS	((1 << 5) | __DAHDI_SIG_FXO)	/* FXO, Kewlstart */

#define DAHDI_SIG_EM	(1 << 6)		/* Ear & Mouth (E&M) */

/* The following are all variations on clear channel */

#define __DAHDI_SIG_DACS	(1 << 16)

#define DAHDI_SIG_CLEAR	(1 << 7)					/* Clear channel */
#define DAHDI_SIG_HDLCRAW	((1 << 8)  | DAHDI_SIG_CLEAR)	/* Raw unchecked HDLC */
#define DAHDI_SIG_HDLCFCS	((1 << 9)  | DAHDI_SIG_HDLCRAW)	/* HDLC with FCS calculation */
#define DAHDI_SIG_HDLCNET	((1 << 10) | DAHDI_SIG_HDLCFCS)	/* HDLC Network */
#define DAHDI_SIG_SLAVE	(1 << 11) 					/* Slave to another channel */
#define	DAHDI_SIG_SF	(1 << 14)			/* Single Freq. tone only, no sig bits */
#define DAHDI_SIG_CAS	(1 << 15)			/* Just get bits */
#define DAHDI_SIG_DACS	(__DAHDI_SIG_DACS | DAHDI_SIG_CLEAR)	/* Cross connect */
#define DAHDI_SIG_EM_E1	(1 << 17)			/* E1 E&M Variation */
#define DAHDI_SIG_DACS_RBS	((1 << 18) | __DAHDI_SIG_DACS)	/* Cross connect w/ RBS */
#define DAHDI_SIG_HARDHDLC	((1 << 19) | DAHDI_SIG_CLEAR)
#define DAHDI_SIG_MTP2	((1 << 20) | DAHDI_SIG_HDLCFCS)	/* MTP2 support  Need HDLC bitstuff and FCS calcuation too */

/* tone flag values */
#define	DAHDI_REVERSE_RXTONE 1  /* reverse polarity rx tone logic */
#define	DAHDI_REVERSE_TXTONE 2  /* reverse polarity tx tone logic */

#define DAHDI_ABIT			8
#define DAHDI_BBIT			4
#define	DAHDI_CBIT			2
#define	DAHDI_DBIT			1

#define DAHDI_MAJOR	196

/* Default chunk size for conferences and such -- static right now, might make
   variable sometime.  8 samples = 1 ms = most frequent service interval possible
   for a USB device */
#define DAHDI_CHUNKSIZE		 8
#define DAHDI_MIN_CHUNKSIZE	 DAHDI_CHUNKSIZE
#define DAHDI_DEFAULT_CHUNKSIZE	 DAHDI_CHUNKSIZE
#define DAHDI_MAX_CHUNKSIZE 	 DAHDI_CHUNKSIZE
#define DAHDI_CB_SIZE		 2

#define DAHDI_MAX_BLOCKSIZE 	 8192
#define DAHDI_DEFAULT_NUM_BUFS	 2
#define DAHDI_MAX_NUM_BUFS		 32
#define DAHDI_MAX_BUF_SPACE         32768

#define DAHDI_DEFAULT_BLOCKSIZE 1024
#define DAHDI_DEFAULT_MTR_MRU	 2048

#define DAHDI_POLICY_IMMEDIATE	 0		/* Start play/record immediately */
#define DAHDI_POLICY_WHEN_FULL  1		/* Start play/record when buffer is full */

#define	RING_DEBOUNCE_TIME	2000	/* 2000 ms ring debounce time */

#define DAHDI_GET_PARAMS_RETURN_MASTER 0x40000000

typedef struct dahdi_params
{
	int channo;		/* Channel number */
	int spanno;		/* Span itself */
	int chanpos;		/* Channel number in span */
	int sigtype;		/* read-only */
	int sigcap;		/* read-only */
	int rxisoffhook;	/* read-only */
	int rxbits;		/* read-only */
	int txbits;		/* read-only */
	int txhooksig;		/* read-only */
	int rxhooksig;		/* read-only */
	int curlaw;		/* read-only  -- one of DAHDI_LAW_MULAW or DAHDI_LAW_ALAW */
	int idlebits;		/* read-only  -- What is considered the idle state */
	char name[40];		/* Name of channel */
	int prewinktime;
	int preflashtime;
	int winktime;
	int flashtime;
	int starttime;
	int rxwinktime;
	int rxflashtime;
	int debouncetime;
	int pulsebreaktime;
	int pulsemaketime;
	int pulseaftertime;
	__u32 chan_alarms;	/* alarms on this channel */
} DAHDI_PARAMS;

typedef struct dahdi_spaninfo {
	int	spanno;		/* span number */
	char	name[20];	/* Name */
	char	desc[40];	/* Description */
	int	alarms;		/* alarms status */
	int	txlevel;	/* what TX level is set to */
	int	rxlevel;	/* current RX level */
	int	bpvcount;	/* current BPV count */
	int	crc4count;	/* current CRC4 error count */
	int	ebitcount;	/* current E-bit error count */
	int	fascount;	/* current FAS error count */
	int	irqmisses;	/* current IRQ misses */
	int	syncsrc;	/* span # of current sync source, or 0 for free run  */
	int	numchans;	/* number of configured channels on this span */
	int	totalchans;	/* total number of channels on the span */
	int	totalspans;	/* total number of spans in entire system */
	int	lbo;		/* line build out */
	int	lineconfig;	/* framing/coding */
	char 	lboname[40];	/* line build out in text form */
	char	location[40];	/* span's device location in system */
	char	manufacturer[40]; /* manufacturer of span's device */
	char	devicetype[40];	/* span's device type */
	int	irq;		/* span's device IRQ */
	int	linecompat;	/* signaling modes possible on this span */
	char	spantype[6];	/* type of span in text form */
} DAHDI_SPANINFO;

typedef struct dahdi_maintinfo
{
int	spanno;		/* span number 1-2 */
int	command;	/* command */
} DAHDI_MAINTINFO;

typedef struct dahdi_confinfo
{
int	chan;		/* channel number, 0 for current */
int	confno;		/* conference number */
int	confmode;	/* conferencing mode */
} DAHDI_CONFINFO;

typedef struct dahdi_gains
{
int	chan;		/* channel number, 0 for current */
unsigned char rxgain[256];	/* Receive gain table */
unsigned char txgain[256];	/* Transmit gain table */
} DAHDI_GAINS;

typedef struct dahdi_lineconfig
{
int span;		/* Which span number (0 to use name) */
char name[20];	/* Name of span to use */
int	lbo;		/* line build-outs */
int	lineconfig;	/* line config parameters (framing, coding) */
int	sync;		/* what level of sync source we are */
} DAHDI_LINECONFIG;

typedef struct dahdi_chanconfig
{
int	chan;		/* Channel we're applying this to (0 to use name) */
char	name[40];	/* Name of channel to use */
int	sigtype;	/* Signal type */
int	deflaw;		/* Default law (DAHDI_LAW_DEFAULT, DAHDI_LAW_MULAW, or DAHDI_LAW_ALAW) */
int	master;		/* Master channel if sigtype is DAHDI_SLAVE */
int	idlebits;	/* Idle bits (if this is a CAS channel) or
			   channel to monitor (if this is DACS channel) */
char	netdev_name[16];/* name for the hdlc network device*/
} DAHDI_CHANCONFIG;

typedef struct dahdi_sfconfig
{
int	chan;		/* Channel we're applying this to (0 to use name) */
char	name[40];	/* Name of channel to use */
long	rxp1;		/* receive tone det. p1 */
long	rxp2;		/* receive tone det. p2 */
long	rxp3;		/* receive tone det. p3 */
int	txtone;		/* Tx tone factor */
int	tx_v2;		/* initial v2 value */
int	tx_v3;		/* initial v3 value */
int	toneflag;	/* Tone flags */
} DAHDI_SFCONFIG;

typedef struct dahdi_bufferinfo
{
int txbufpolicy;	/* Policy for handling receive buffers */
int rxbufpolicy;	/* Policy for handling receive buffers */
int numbufs;		/* How many buffers to use */
int bufsize;		/* How big each buffer is */
int readbufs;		/* How many read buffers are full (read-only) */
int writebufs;		/* How many write buffers are full (read-only) */
} DAHDI_BUFFERINFO;

typedef struct dahdi_dialparams {
	int mfv1_tonelen;	/* MF R1 tone length for digits */
	int dtmf_tonelen;	/* DTMF tone length */
	int mfr2_tonelen;	/* MF R2 tone length */
	int reserved[3];	/* Reserved for future expansion -- always set to 0 */
} DAHDI_DIAL_PARAMS;

typedef struct dahdi_dynamic_span {
	char driver[20];	/* Which low-level driver to use */
	char addr[40];		/* Destination address */
	int numchans;		/* Number of channels */
	int timing;		/* Timing source preference */
	int spanno;		/* Span number (filled in by DAHDI) */
} DAHDI_DYNAMIC_SPAN;

/* Define the max # of outgoing DTMF, MFR1 or MFR2 digits to queue in-kernel */
#define DAHDI_MAX_DTMF_BUF 256

#define DAHDI_DIAL_OP_APPEND	1
#define DAHDI_DIAL_OP_REPLACE	2
#define DAHDI_DIAL_OP_CANCEL	3

#define DAHDI_LAW_DEFAULT	0	/* Default law for span */
#define DAHDI_LAW_MULAW	1	/* Mu-law */
#define DAHDI_LAW_ALAW	2	/* A-law */

typedef struct dahdi_dialoperation {
	int op;
	char dialstr[DAHDI_MAX_DTMF_BUF];
} DAHDI_DIAL_OPERATION;


typedef struct dahdi_indirect_data
{
int	chan;
int	op;
void	*data;
} DAHDI_INDIRECT_DATA;	

struct dahdi_versioninfo {
	char version[80];
	char echo_canceller[80];
};

struct dahdi_hwgain {
	__s32 newgain;	/* desired gain in dB but x10.  -3.5dB would be -35 */
	__u32 tx:1;	/* 0=rx; 1=tx */
};

struct dahdi_attach_echocan {
	int	chan;		/* Channel we're applying this to */
	char	echocan[16];	/* Name of echo canceler to attach to this channel
				   (leave empty to have no echocan attached */
};

/* ioctl definitions */
#define DAHDI_CODE	0xDA

/*
 * Get Transfer Block Size.
 */
#define DAHDI_GET_BLOCKSIZE	_IOR (DAHDI_CODE, 1, int)

/*
 * Set Transfer Block Size.
 */
#define DAHDI_SET_BLOCKSIZE	_IOW (DAHDI_CODE, 2, int)

/*
 * Flush Buffer(s) and stop I/O
 */
#define	DAHDI_FLUSH		_IOW (DAHDI_CODE, 3, int)

/*
 * Wait for Write to Finish
 */
#define	DAHDI_SYNC		_IOW (DAHDI_CODE, 4, int)

/*
 * Get channel parameters
 */
#define DAHDI_GET_PARAMS		_IOR (DAHDI_CODE, 5, struct dahdi_params)

/*
 * Get channel parameters
 */
#define DAHDI_SET_PARAMS		_IOW (DAHDI_CODE, 6, struct dahdi_params)

/*
 * Set Hookswitch Status
 */
#define DAHDI_HOOK		_IOW (DAHDI_CODE, 7, int)

/*
 * Get Signalling Event
 */
#define DAHDI_GETEVENT		_IOR (DAHDI_CODE, 8, int)

/*
 * Wait for something to happen (IO Mux)
 */
#define DAHDI_IOMUX		_IOWR (DAHDI_CODE, 9, int)

/*
 * Get Span Status
 */
#define DAHDI_SPANSTAT		_IOWR (DAHDI_CODE, 10, struct dahdi_spaninfo)

/*
 * Set Maintenance Mode
 */
#define DAHDI_MAINT		_IOW (DAHDI_CODE, 11, struct dahdi_maintinfo)

/*
 * Get Conference Mode
 */
#define DAHDI_GETCONF		_IOWR (DAHDI_CODE, 12, struct dahdi_confinfo)

/*
 * Set Conference Mode
 */
#define DAHDI_SETCONF		_IOWR (DAHDI_CODE, 13, struct dahdi_confinfo)

/*
 * Setup or Remove Conference Link
 */
#define DAHDI_CONFLINK		_IOW (DAHDI_CODE, 14, struct dahdi_confinfo)

/*
 * Display Conference Diagnostic Information on Console
 */
#define DAHDI_CONFDIAG		_IOR (DAHDI_CODE, 15, int)

/*
 * Get Channel audio gains
 */
#define DAHDI_GETGAINS		_IOWR (DAHDI_CODE, 16, struct dahdi_gains)

/*
 * Set Channel audio gains
 */
#define DAHDI_SETGAINS		_IOWR (DAHDI_CODE, 17, struct dahdi_gains)

/*
 * Set Line (T1) Configurations and start system
 */
#define	DAHDI_SPANCONFIG		_IOW (DAHDI_CODE, 18, struct dahdi_lineconfig)

/*
 * Set Channel Configuration
 */
#define	DAHDI_CHANCONFIG		_IOW (DAHDI_CODE, 19, struct dahdi_chanconfig)

/*
 * Set Conference to mute mode
 */
#define	DAHDI_CONFMUTE		_IOW (DAHDI_CODE, 20, int)

/*
 * Send a particular tone (see DAHDI_TONE_*)
 */
#define	DAHDI_SENDTONE		_IOW (DAHDI_CODE, 21, int)

/*
 * Set your region for tones (see DAHDI_TONE_ZONE_*)
 */
#define	DAHDI_SETTONEZONE		_IOW (DAHDI_CODE, 22, int)

/*
 * Retrieve current region for tones (see DAHDI_TONE_ZONE_*)
 */
#define	DAHDI_GETTONEZONE		_IOR (DAHDI_CODE, 23, int)

/*
 * Master unit only -- set default zone (see DAHDI_TONE_ZONE_*)
 */
#define	DAHDI_DEFAULTZONE		_IOW (DAHDI_CODE, 24, int)

/*
 * Load a tone zone from a dahdi_tone_def_header, see
 * below...
 */
#define DAHDI_LOADZONE		_IOW (DAHDI_CODE, 25, struct dahdi_tone_def_header)

/*
 * Free a tone zone 
 */
#define DAHDI_FREEZONE		_IOW (DAHDI_CODE, 26, int)

/*
 * Set buffer policy 
 */
#define DAHDI_SET_BUFINFO		_IOW (DAHDI_CODE, 27, struct dahdi_bufferinfo)

/*
 * Get current buffer info
 */
#define DAHDI_GET_BUFINFO		_IOR (DAHDI_CODE, 28, struct dahdi_bufferinfo)

/*
 * Get dialing parameters
 */
#define DAHDI_GET_DIALPARAMS	_IOR (DAHDI_CODE, 29, struct dahdi_dialparams)

/*
 * Set dialing parameters
 */
#define DAHDI_SET_DIALPARAMS	_IOW (DAHDI_CODE, 30, struct dahdi_dialparams)

/*
 * Append, replace, or cancel a dial string
 */
#define DAHDI_DIAL			_IOW (DAHDI_CODE, 31, struct dahdi_dialoperation)

/*
 * Set a clear channel into audio mode
 */
#define DAHDI_AUDIOMODE		_IOW (DAHDI_CODE, 32, int)

/*
 * Enable or disable echo cancellation on a channel 
 *
 * For ECHOCANCEL:
 * The number is zero to disable echo cancellation and non-zero
 * to enable echo cancellation.  If the number is between 32
 * and 1024, it will also set the number of taps in the echo canceller
 *
 * For ECHOCANCEL_PARAMS:
 * The structure contains parameters that should be passed to the
 * echo canceler instance for the selected channel.
 */
#define DAHDI_ECHOCANCEL		_IOW (DAHDI_CODE, 33, int)
#define DAHDI_ECHOCANCEL_PARAMS	_IOW (DAHDI_CODE, 33, struct dahdi_echocanparams)

/*
 * Return a channel's channel number (useful for the /dev/zap/pseudo type interfaces 
 */
#define DAHDI_CHANNO		_IOR (DAHDI_CODE, 34, int)

/*
 * Return a flag indicating whether channel is currently dialing
 */
#define DAHDI_DIALING		_IOR (DAHDI_CODE, 35, int)

/* Numbers 60 to 90 are reserved for private use of low level hardware
   drivers */

/*
 * Set a clear channel into HDLC w/out FCS checking/calculation mode
 */
#define DAHDI_HDLCRAWMODE		_IOW (DAHDI_CODE, 36, int)

/*
 * Set a clear channel into HDLC w/ FCS mode
 */
#define DAHDI_HDLCFCSMODE		_IOW (DAHDI_CODE, 37, int)

/* 
 * Specify a channel on /dev/zap/chan -- must be done before any other ioctl's and is only
 * valid on /dev/zap/chan
 */
#define DAHDI_SPECIFY		_IOW (DAHDI_CODE, 38, int)

/*
 * Temporarily set the law on a channel to 
 * DAHDI_LAW_DEFAULT, DAHDI_LAW_ALAW, or DAHDI_LAW_MULAW.  Is reset on close.  
 */
#define DAHDI_SETLAW		_IOW (DAHDI_CODE, 39, int)

/*
 * Temporarily set the channel to operate in linear mode when non-zero
 * or default law if 0
 */
#define DAHDI_SETLINEAR		_IOW (DAHDI_CODE, 40, int)

/*
 * Set a clear channel into HDLC w/ PPP interface mode
 */
#define DAHDI_HDLCPPP		_IOW (DAHDI_CODE, 41, int)

/*
 * Set the ring cadence for FXS interfaces
 */
#define DAHDI_SETCADENCE		_IOW (DAHDI_CODE, 42, struct dahdi_ring_cadence)

/*
 * Set the bits going out for CAS interface
 */
#define DAHDI_SETTXBITS			_IOW (DAHDI_CODE, 43, int)


/*
 * Display Channel Diagnostic Information on Console
 */
#define DAHDI_CHANDIAG		_IOR (DAHDI_CODE, 44, int) 

/* 
 * Obtain received signalling
 */
#define DAHDI_GETRXBITS _IOR (DAHDI_CODE, 45, int)

/*
 * Set Channel's SF Tone Configuration
 */
#define	DAHDI_SFCONFIG		_IOW (DAHDI_CODE, 46, struct dahdi_sfconfig)

/*
 * Set timer expiration (in samples)
 */
#define DAHDI_TIMERCONFIG	_IOW (DAHDI_CODE, 47, int)

/*
 * Acknowledge timer expiration (number to acknowledge, or -1 for all)
 */
#define DAHDI_TIMERACK _IOW (DAHDI_CODE, 48, int)

/*
 * Get Conference to mute mode
 */
#define	DAHDI_GETCONFMUTE		_IOR (DAHDI_CODE, 49, int)

/*
 * Request echo training in some number of ms (with muting in the mean time)
 */
#define	DAHDI_ECHOTRAIN		_IOW (DAHDI_CODE, 50, int)

/*
 * Set on hook transfer for n number of ms -- implemnted by low level driver
 */
#define	DAHDI_ONHOOKTRANSFER		_IOW (DAHDI_CODE, 51, int)

/*
 * Queue Ping
 */
#define DAHDI_TIMERPING _IOW (DAHDI_CODE, 42, int) /* Should be 52, but works */

/*
 * Acknowledge ping
 */
#define DAHDI_TIMERPONG _IOW (DAHDI_CODE, 53, int)

/*
 * Set/get signalling freeze
 */
#define DAHDI_SIGFREEZE _IOW (DAHDI_CODE, 54, int)
#define DAHDI_GETSIGFREEZE _IOR (DAHDI_CODE, 55, int)

/*
 * Do a channel IOCTL from the /dev/zap/ctl interface
 */
#define DAHDI_INDIRECT _IOWR (DAHDI_CODE, 56, struct dahdi_indirect_data)


/*
 * Get the version of DAHDI that is running, and a description
 * of the compiled-in echo canceller (if any)
 */
#define DAHDI_GETVERSION _IOR(DAHDI_CODE, 57, struct dahdi_versioninfo)

/*
 * Put the channel in loopback mode (receive from the channel is
 * transmitted back on the interface)
 */
#define DAHDI_LOOPBACK _IOW(DAHDI_CODE, 58, int)

/*
  Attach the desired echo canceler module (or none) to a channel in an
  audio-supporting mode, so that when the channel needs an echo canceler
  that module will be used to supply one.
 */
#define DAHDI_ATTACH_ECHOCAN _IOW(DAHDI_CODE, 59, struct dahdi_attach_echocan)


/*
 *  60-80 are reserved for private drivers
 *  80-85 are reserved for dynamic span stuff
 */

/*
 * Create a dynamic span
 */
#define DAHDI_DYNAMIC_CREATE	_IOWR (DAHDI_CODE, 80, struct dahdi_dynamic_span)

/* 
 * Destroy a dynamic span 
 */
#define DAHDI_DYNAMIC_DESTROY	_IOW (DAHDI_CODE, 81, struct dahdi_dynamic_span)

/*
 * Set the HW gain for a device
 */
#define DAHDI_SET_HWGAIN		_IOW (DAHDI_CODE, 86, struct dahdi_hwgain)

/*
 * Enable tone detection -- implemented by low level driver
 */
#define DAHDI_TONEDETECT		_IOW (DAHDI_CODE, 91, int)

/*
 * Set polarity -- implemented by individual driver.  0 = forward, 1 = reverse
 */
#define	DAHDI_SETPOLARITY		_IOW (DAHDI_CODE, 92, int)

/*
 * Transcoder operations
 */
#define DAHDI_TRANSCODE_OP		_IOWR(DAHDI_CODE, 93, int)

/*
 * VoiceMail Waiting Indication (WMWI) -- implemented by low-level driver.
 * Value: number of waiting messages (hence 0: switch messages off).
 */
#define DAHDI_VMWI			_IOWR(DAHDI_CODE, 94, int)

/* 
 * Startup or Shutdown a span
 */
#define DAHDI_STARTUP		_IOW (DAHDI_CODE, 99, int)
#define DAHDI_SHUTDOWN		_IOW (DAHDI_CODE, 100, int)

#define DAHDI_TONE_ZONE_MAX		128

#define DAHDI_TONE_ZONE_DEFAULT 	-1	/* To restore default */

#define DAHDI_TONE_STOP		-1
#define DAHDI_TONE_DIALTONE	0
#define DAHDI_TONE_BUSY		1
#define DAHDI_TONE_RINGTONE	2
#define DAHDI_TONE_CONGESTION	3
#define DAHDI_TONE_CALLWAIT	4
#define DAHDI_TONE_DIALRECALL	5
#define DAHDI_TONE_RECORDTONE	6
#define DAHDI_TONE_INFO		7
#define DAHDI_TONE_CUST1		8
#define DAHDI_TONE_CUST2		9
#define DAHDI_TONE_STUTTER		10
#define DAHDI_TONE_MAX		16

#define DAHDI_TONE_DTMF_BASE	64
#define DAHDI_TONE_MFR1_BASE	80
#define DAHDI_TONE_MFR2_FWD_BASE	96
#define DAHDI_TONE_MFR2_REV_BASE	112

enum {
	DAHDI_TONE_DTMF_0 = DAHDI_TONE_DTMF_BASE,
	DAHDI_TONE_DTMF_1,
	DAHDI_TONE_DTMF_2,
	DAHDI_TONE_DTMF_3,
	DAHDI_TONE_DTMF_4,
	DAHDI_TONE_DTMF_5,
	DAHDI_TONE_DTMF_6,
	DAHDI_TONE_DTMF_7,
	DAHDI_TONE_DTMF_8,
	DAHDI_TONE_DTMF_9,
	DAHDI_TONE_DTMF_s,
	DAHDI_TONE_DTMF_p,
	DAHDI_TONE_DTMF_A,
	DAHDI_TONE_DTMF_B,
	DAHDI_TONE_DTMF_C,
	DAHDI_TONE_DTMF_D
};

#define DAHDI_TONE_DTMF_MAX DAHDI_TONE_DTMF_D

enum {
	DAHDI_TONE_MFR1_0 = DAHDI_TONE_MFR1_BASE,
	DAHDI_TONE_MFR1_1,
	DAHDI_TONE_MFR1_2,
	DAHDI_TONE_MFR1_3,
	DAHDI_TONE_MFR1_4,
	DAHDI_TONE_MFR1_5,
	DAHDI_TONE_MFR1_6,
	DAHDI_TONE_MFR1_7,
	DAHDI_TONE_MFR1_8,
	DAHDI_TONE_MFR1_9,
	DAHDI_TONE_MFR1_KP,
	DAHDI_TONE_MFR1_ST,
	DAHDI_TONE_MFR1_STP,
	DAHDI_TONE_MFR1_ST2P,
	DAHDI_TONE_MFR1_ST3P,
};

#define DAHDI_TONE_MFR1_MAX DAHDI_TONE_MFR1_ST3P

enum {
	DAHDI_TONE_MFR2_FWD_1 = DAHDI_TONE_MFR2_FWD_BASE,
	DAHDI_TONE_MFR2_FWD_2,
	DAHDI_TONE_MFR2_FWD_3,
	DAHDI_TONE_MFR2_FWD_4,
	DAHDI_TONE_MFR2_FWD_5,
	DAHDI_TONE_MFR2_FWD_6,
	DAHDI_TONE_MFR2_FWD_7,
	DAHDI_TONE_MFR2_FWD_8,
	DAHDI_TONE_MFR2_FWD_9,
	DAHDI_TONE_MFR2_FWD_10,
	DAHDI_TONE_MFR2_FWD_11,
	DAHDI_TONE_MFR2_FWD_12,
	DAHDI_TONE_MFR2_FWD_13,
	DAHDI_TONE_MFR2_FWD_14,
	DAHDI_TONE_MFR2_FWD_15,
};

#define DAHDI_TONE_MFR2_FWD_MAX DAHDI_TONE_MFR2_FWD_15

enum {
	DAHDI_TONE_MFR2_REV_1 = DAHDI_TONE_MFR2_REV_BASE,
	DAHDI_TONE_MFR2_REV_2,
	DAHDI_TONE_MFR2_REV_3,
	DAHDI_TONE_MFR2_REV_4,
	DAHDI_TONE_MFR2_REV_5,
	DAHDI_TONE_MFR2_REV_6,
	DAHDI_TONE_MFR2_REV_7,
	DAHDI_TONE_MFR2_REV_8,
	DAHDI_TONE_MFR2_REV_9,
	DAHDI_TONE_MFR2_REV_10,
	DAHDI_TONE_MFR2_REV_11,
	DAHDI_TONE_MFR2_REV_12,
	DAHDI_TONE_MFR2_REV_13,
	DAHDI_TONE_MFR2_REV_14,
	DAHDI_TONE_MFR2_REV_15,
};

#define DAHDI_TONE_MFR2_REV_MAX DAHDI_TONE_MFR2_REV_15

#define DAHDI_MAX_CADENCE		16

#define DAHDI_TONEDETECT_ON	(1 << 0)		/* Detect tones */
#define DAHDI_TONEDETECT_MUTE	(1 << 1)		/* Mute audio in received channel */

#define DAHDI_TRANSCODE_MAGIC 0x74a9c0de

/* Operations */
#define DAHDI_TCOP_ALLOCATE	1			/* Allocate/reset DTE channel */
#define DAHDI_TCOP_TRANSCODE	2			/* Begin transcoding a block */
#define DAHDI_TCOP_GETINFO		3			/* Get information (use dahdi_transcode_info) */
#define DAHDI_TCOP_RELEASE         4                       /* Release DTE channel */
#define DAHDI_TCOP_TEST            5                       /* test DTE device */
typedef struct dahdi_transcode_info {
	unsigned int op;
	unsigned int tcnum;
	char name[80];
	int numchannels;
	unsigned int srcfmts;
	unsigned int dstfmts;
} DAHDI_TRANSCODE_INFO;

#define DAHDI_TCCONF_USETS		(1 << 0)		/* Use/update timestamp field */
#define DAHDI_TCCONF_USESEQ	(1 << 1)		/* Use/update seqno field */

#define DAHDI_TCSTAT_DSTRDY	(1 << 0)		/* Destination data is ready */
#define DAHDI_TCSTAT_DSTBUSY	(1 << 1)		/* Destination data is outstanding */

#define __DAHDI_TRANSCODE_BUFSIZ	16384
#define DAHDI_TRANSCODE_HDRLEN	256
#define DAHDI_TRANSCODE_BUFSIZ	((__DAHDI_TRANSCODE_BUFSIZ) - (DAHDI_TRANSCODE_HDRLEN))
#define DAHDI_TRANSCODE_DSTOFFSET	(((DAHDI_TRANSCODE_BUFSIZ) / 2) + DAHDI_TRANSCODE_HDRLEN)
#define DAHDI_TRANSCODE_SRCOFFSET	(((DAHDI_TRANSCODE_BUFSIZ) / 2) + DAHDI_TRANSCODE_HDRLEN)

typedef struct dahdi_transcode_header {
	unsigned int srcfmt;		/* See formats.h -- use TCOP_RESET when you change */
	unsigned int srcoffset; 	/* In bytes -- written by user */
	unsigned int srclen;		/* In bytes -- written by user */
	unsigned int srctimestamp;	/* In samples -- written by user (only used if DAHDI_TCCONF_USETS is set) */
	unsigned int srcseqno;		/* In units -- written by user (only used if DAHDI_TCCONF_USESEQ is set) */

	unsigned int dstfmt;		/* See formats.h -- use TCOP_RESET when you change */
	unsigned int dstoffset;  	/* In bytes -- written by user */
	unsigned int dsttimestamp;	/* In samples -- read by user */
	unsigned int dstseqno;		/* In units -- read by user (only used if DAHDI_TCCONF_USESEQ is set) */
	unsigned int dstlen;  		/* In bytes -- read by user */
	unsigned int dstsamples;	/* In timestamp units -- read by user */

	unsigned int magic;		/* Magic value -- DAHDI_TRANSCODE_MAGIC, read by user */
	unsigned int config;		/* Read/write by user */
	unsigned int status;		/* Read/write by user */
	unsigned char userhdr[DAHDI_TRANSCODE_HDRLEN - (sizeof(unsigned int) * 14)];	/* Storage for user parameters */
	unsigned char srcdata[DAHDI_TRANSCODE_BUFSIZ / 2];	/* Storage of source data */
	unsigned char dstdata[DAHDI_TRANSCODE_BUFSIZ / 2];	/* Storage of destination data */
} DAHDI_TRANSCODE_HEADER;

struct dahdi_ring_cadence {
	int ringcadence[DAHDI_MAX_CADENCE];
};

#define DAHDI_MAX_ECHOCANPARAMS 8

struct dahdi_echocanparam {
	char name[16];
        __s32 value;
};

struct dahdi_echocanparams {
	__u32 tap_length;		/* 8 taps per millisecond */
	__u32 param_count;		/* number of parameters supplied */
	/* immediately follow this structure with dahdi_echocanparam structures */
	struct dahdi_echocanparam params[0];
};

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

struct dahdi_tone_def_header {
	int count;		/* How many samples follow */
	int zone;		/* Which zone we are loading */
	int ringcadence[DAHDI_MAX_CADENCE];	/* Ring cadence in ms (0=on, 1=off, ends with 0 value) */
	char name[40];		/* Informational name of zone */
	/* Immediately follow the dahdi_tone_def_header by dahdi_tone_def's */
};

struct dahdi_tone_def {		/* Structure for zone programming */
	int tone;		/* See DAHDI_TONE_* */
	int next;		/* What the next position in the cadence is
				   (They're numbered by the order the appear here) */
	int samples;		/* How many samples to play for this cadence */
				/* Now come the constants we need to make tones */
	int shift;		/* How much to scale down the volume (2 is nice) */

	/* 
		Calculate the next 6 factors using the following equations:
		l = <level in dbm>, f1 = <freq1>, f2 = <freq2>
		gain = pow(10.0, (l - 3.14) / 20.0) * 65536.0 / 2.0;

		// Frequency factor 1 
		fac_1 = 2.0 * cos(2.0 * M_PI * (f1/8000.0)) * 32768.0;
		// Last previous two samples 
		init_v2_1 = sin(-4.0 * M_PI * (f1/8000.0)) * gain;
		init_v3_1 = sin(-2.0 * M_PI * (f1/8000.0)) * gain;

		// Frequency factor 2 
		fac_2 = 2.0 * cos(2.0 * M_PI * (f2/8000.0)) * 32768.0;
		// Last previous two samples 
		init_v2_2 = sin(-4.0 * M_PI * (f2/8000.0)) * gain;
		init_v3_2 = sin(-2.0 * M_PI * (f2/8000.0)) * gain;
	*/
	int fac1;		
	int init_v2_1;		
	int init_v3_1;		
	int fac2;		
	int init_v2_2;		
	int init_v3_2;
	int modulate;

};

#ifdef __KERNEL__
#endif /* KERNEL */

/* Define the maximum block size */
#define	DAHDI_MAX_BLOCKSIZE	8192

/* Define the default network block size */
#define DAHDI_DEFAULT_MTU_MRU	2048

/* Flush and stop the read (input) process */
#define	DAHDI_FLUSH_READ		1

/* Flush and stop the write (output) process */
#define	DAHDI_FLUSH_WRITE		2

/* Flush and stop both (input and output) processes */
#define	DAHDI_FLUSH_BOTH		(DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE)

/* Flush the event queue */
#define	DAHDI_FLUSH_EVENT		4

/* Flush everything */
#define	DAHDI_FLUSH_ALL		(DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE | DAHDI_FLUSH_EVENT)


/* Value for DAHDI_HOOK, set to ON hook */
#define	DAHDI_ONHOOK	0

/* Value for DAHDI_HOOK, set to OFF hook */
#define	DAHDI_OFFHOOK	1

/* Value for DAHDI_HOOK, wink (off hook momentarily) */
#define	DAHDI_WINK		2

/* Value for DAHDI_HOOK, flash (on hook momentarily) */
#define	DAHDI_FLASH	3

/* Value for DAHDI_HOOK, start line */
#define	DAHDI_START	4

/* Value for DAHDI_HOOK, ring line (same as start line) */
#define	DAHDI_RING		5

/* Value for DAHDI_HOOK, turn ringer off */
#define DAHDI_RINGOFF  6

/* Ret. Value for GET/WAIT Event, no event */
#define	DAHDI_EVENT_NONE	0

/* Ret. Value for GET/WAIT Event, Went Onhook */
#define	DAHDI_EVENT_ONHOOK 1

/* Ret. Value for GET/WAIT Event, Went Offhook or got Ring */
#define	DAHDI_EVENT_RINGOFFHOOK 2

/* Ret. Value for GET/WAIT Event, Got Wink or Flash */
#define	DAHDI_EVENT_WINKFLASH 3

/* Ret. Value for GET/WAIT Event, Got Alarm */
#define	DAHDI_EVENT_ALARM	4

/* Ret. Value for GET/WAIT Event, Got No Alarm (after alarm) */
#define	DAHDI_EVENT_NOALARM 5

/* Ret. Value for GET/WAIT Event, HDLC Abort frame */
#define DAHDI_EVENT_ABORT 6

/* Ret. Value for GET/WAIT Event, HDLC Frame overrun */
#define DAHDI_EVENT_OVERRUN 7

/* Ret. Value for GET/WAIT Event, Bad FCS */
#define DAHDI_EVENT_BADFCS 8

/* Ret. Value for dial complete */
#define DAHDI_EVENT_DIALCOMPLETE	9

/* Ret Value for ringer going on */
#define DAHDI_EVENT_RINGERON 10

/* Ret Value for ringer going off */
#define DAHDI_EVENT_RINGEROFF 11

/* Ret Value for hook change complete */
#define DAHDI_EVENT_HOOKCOMPLETE 12

/* Ret Value for bits changing on a CAS / User channel */
#define DAHDI_EVENT_BITSCHANGED 13

/* Ret value for the beginning of a pulse coming on its way */
#define DAHDI_EVENT_PULSE_START 14

/* Timer event -- timer expired */
#define DAHDI_EVENT_TIMER_EXPIRED	15

/* Timer event -- ping ready */
#define DAHDI_EVENT_TIMER_PING		16

/* Polarity reversal event */
#define DAHDI_EVENT_POLARITY  17

/* Ring Begin event */
#define DAHDI_EVENT_RINGBEGIN  18

/* Echo can disabled event */
#define DAHDI_EVENT_EC_DISABLED 19

/* Channel was disconnected. Hint user to close channel */
#define DAHDI_EVENT_REMOVED   20

/* A neon MWI pulse was detected */
#define DAHDI_EVENT_NEONMWI_ACTIVE   21

/* No neon MWI pulses were detected over some period of time */
#define DAHDI_EVENT_NEONMWI_INACTIVE   22

#define DAHDI_EVENT_PULSEDIGIT (1 << 16)	/* This is OR'd with the digit received */
#define DAHDI_EVENT_DTMFDOWN  (1 << 17)	/* Ditto for DTMF key down event */
#define DAHDI_EVENT_DTMFUP (1 << 18)	/* Ditto for DTMF key up event */

/* Flag Value for IOMUX, read avail */
#define	DAHDI_IOMUX_READ	1

/* Flag Value for IOMUX, write avail */
#define	DAHDI_IOMUX_WRITE	2

/* Flag Value for IOMUX, write done */
#define	DAHDI_IOMUX_WRITEEMPTY	4

/* Flag Value for IOMUX, signalling event avail */
#define	DAHDI_IOMUX_SIGEVENT	8

/* Flag Value for IOMUX, Do Not Wait if nothing to report */
#define	DAHDI_IOMUX_NOWAIT	0x100

/* Alarm Condition bits */
#define	DAHDI_ALARM_NONE		0	/* No alarms */
#define	DAHDI_ALARM_RECOVER	1	/* Recovering from alarm */
#define	DAHDI_ALARM_LOOPBACK	2	/* In loopback */
#define	DAHDI_ALARM_YELLOW		4	/* Yellow Alarm */
#define	DAHDI_ALARM_RED		8	/* Red Alarm */
#define	DAHDI_ALARM_BLUE		16	/* Blue Alarm */
#define DAHDI_ALARM_NOTOPEN	32
/* Maintenance modes */
#define	DAHDI_MAINT_NONE		0	/* Normal Mode */
#define	DAHDI_MAINT_LOCALLOOP	1	/* Local Loopback */
#define	DAHDI_MAINT_REMOTELOOP	2	/* Remote Loopback */
#define	DAHDI_MAINT_LOOPUP	3	/* send loopup code */
#define	DAHDI_MAINT_LOOPDOWN	4	/* send loopdown code */
#define	DAHDI_MAINT_LOOPSTOP	5	/* stop sending loop codes */


/* Conference modes */
#define	DAHDI_CONF_MODE_MASK 0xff		/* mask for modes */
#define	DAHDI_CONF_NORMAL	0		/* normal mode */
#define	DAHDI_CONF_MONITOR 1		/* monitor mode (rx of other chan) */
#define	DAHDI_CONF_MONITORTX 2		/* monitor mode (tx of other chan) */
#define	DAHDI_CONF_MONITORBOTH 3		/* monitor mode (rx & tx of other chan) */
#define	DAHDI_CONF_CONF 4			/* conference mode */
#define	DAHDI_CONF_CONFANN 5		/* conference announce mode */
#define	DAHDI_CONF_CONFMON 6		/* conference monitor mode */
#define	DAHDI_CONF_CONFANNMON 7		/* conference announce/monitor mode */
#define	DAHDI_CONF_REALANDPSEUDO 8	/* real and pseudo port both on conf */
#define DAHDI_CONF_DIGITALMON 9	/* Do not decode or interpret */
#define	DAHDI_CONF_MONITOR_RX_PREECHO 10	/* monitor mode (rx of other chan) - before echo can is done */
#define	DAHDI_CONF_MONITOR_TX_PREECHO 11	/* monitor mode (tx of other chan) - before echo can is done */
#define	DAHDI_CONF_MONITORBOTH_PREECHO 12	/* monitor mode (rx & tx of other chan) - before echo can is done */
#define	DAHDI_CONF_FLAG_MASK 0xff00	/* mask for flags */
#define	DAHDI_CONF_LISTENER 0x100		/* is a listener on the conference */
#define	DAHDI_CONF_TALKER 0x200		/* is a talker on the conference */
#define	DAHDI_CONF_PSEUDO_LISTENER 0x400	/* pseudo is a listener on the conference */
#define	DAHDI_CONF_PSEUDO_TALKER 0x800	/* pseudo is a talker on the conference */


#define	DAHDI_DEFAULT_WINKTIME	150	/* 150 ms default wink time */
#define	DAHDI_DEFAULT_FLASHTIME	750	/* 750 ms default flash time */

#define	DAHDI_DEFAULT_PREWINKTIME	50	/* 50 ms before wink */
#define	DAHDI_DEFAULT_PREFLASHTIME 50	/* 50 ms before flash */
#define	DAHDI_DEFAULT_STARTTIME 1500	/* 1500 ms of start */
#define	DAHDI_DEFAULT_RINGTIME 2000	/* 2000 ms of ring on (start, FXO) */
#if 0
#define	DAHDI_DEFAULT_RXWINKTIME 250	/* 250ms longest rx wink */
#endif
#define	DAHDI_DEFAULT_RXWINKTIME 300	/* 300ms longest rx wink (to work with the Atlas) */
#define	DAHDI_DEFAULT_RXFLASHTIME 1250	/* 1250ms longest rx flash */
#define	DAHDI_DEFAULT_DEBOUNCETIME 600	/* 600ms of FXS GS signalling debounce */
#define	DAHDI_DEFAULT_PULSEMAKETIME 50	/* 50 ms of line closed when dial pulsing */
#define	DAHDI_DEFAULT_PULSEBREAKTIME 50	/* 50 ms of line open when dial pulsing */
#define	DAHDI_DEFAULT_PULSEAFTERTIME 750	/* 750ms between dial pulse digits */

#define	DAHDI_MINPULSETIME (15 * 8)	/* 15 ms minimum */

#ifdef SHORT_FLASH_TIME
#define	DAHDI_MAXPULSETIME (80 * 8)	/* we need 80 ms, not 200ms, as we have a short flash */
#else
#define	DAHDI_MAXPULSETIME (200 * 8)	/* 200 ms maximum */
#endif

#define	DAHDI_PULSETIMEOUT ((DAHDI_MAXPULSETIME / 8) + 50)

#define DAHDI_RINGTRAILER (50 * 8)	/* Don't consider a ring "over" until it's been gone at least this
									   much time */

#define	DAHDI_LOOPCODE_TIME 10000		/* send loop codes for 10 secs */
#define	DAHDI_ALARMSETTLE_TIME	5000	/* allow alarms to settle for 5 secs */
#define	DAHDI_AFTERSTART_TIME 500		/* 500ms after start */

#define DAHDI_RINGOFFTIME 4000		/* Turn off ringer for 4000 ms */
#define	DAHDI_KEWLTIME 500		/* 500ms for kewl pulse */
#define	DAHDI_AFTERKEWLTIME 300    /* 300ms after kewl pulse */

#define DAHDI_MAX_PRETRAINING   1000	/* 1000ms max pretraining time */

#define DAHDI_MAX_SPANS		128		/* Max, 128 spans */
#define DAHDI_MAX_CHANNELS		1024	/* Max, 1024 channels */
#define DAHDI_MAX_CONF			1024	/* Max, 1024 conferences */

#ifdef	FXSFLASH
#define DAHDI_FXSFLASHMINTIME	450	/* min 450ms */
#define DAHDI_FXSFLASHMAXTIME	550	/* max 550ms */
#endif

#ifdef __KERNEL__

#include <linux/poll.h>

#define	DAHDI_MAX_EVENTSIZE	64	/* 64 events max in buffer */

struct dahdi_span;
struct dahdi_chan;

struct dahdi_tone_state {
	int v1_1;
	int v2_1;
	int v3_1;
	int v1_2;
	int v2_2;
	int v3_2;
	int modulate;
};

struct dahdi_chardev {
	const char *name;
	__u8 minor;
};

int dahdi_register_chardev(struct dahdi_chardev *dev);
int dahdi_unregister_chardev(struct dahdi_chardev *dev);

#ifdef CONFIG_DAHDI_NET
struct dahdi_hdlc {
	struct net_device *netdev;
	struct dahdi_chan *chan;
};
#endif

/* Conference queue stucture */
struct confq {
	u_char buffer[DAHDI_CHUNKSIZE * DAHDI_CB_SIZE];
	u_char *buf[DAHDI_CB_SIZE];
	int inbuf;
	int outbuf;
};

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

struct dahdi_chan {
#ifdef CONFIG_DAHDI_NET
	/* Must be first */
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
	char name[40];		/* Name */
	/* Specified by DAHDI */
	int channo;			/* DAHDI Channel number */
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

	struct dahdi_chan *master;	/* Our Master channel (could be us) */
	/* Next slave (if appropriate) */
	int nextslave;

	u_char *writechunk;						/* Actual place to write to */
	u_char swritechunk[DAHDI_MAX_CHUNKSIZE];	/* Buffer to be written */
	u_char *readchunk;						/* Actual place to read from */
	u_char sreadchunk[DAHDI_MAX_CHUNKSIZE];	/* Preallocated static area */
	short *readchunkpreec;

	/* Pointer to tx and rx gain tables */
	u_char *rxgain;
	u_char *txgain;
	
	/* Whether or not we have allocated gains or are using the default */
	int gainalloc;

	/* Specified by driver, readable by DAHDI */
	void *pvt;			/* Private channel data */
	struct file *file;	/* File structure */
	
	
	struct dahdi_span	*span;			/* Span we're a member of */
	int		sig;			/* Signalling */
	int		sigcap;			/* Capability for signalling */
	__u32		chan_alarms;		/* alarms status */

	/* Used only by DAHDI -- NO DRIVER SERVICEABLE PARTS BELOW */
	/* Buffer declarations */
	u_char		*readbuf[DAHDI_MAX_NUM_BUFS];	/* read buffer */
	int		inreadbuf;
	int		outreadbuf;
	wait_queue_head_t readbufq; /* read wait queue */

	u_char		*writebuf[DAHDI_MAX_NUM_BUFS]; /* write buffers */
	int		inwritebuf;
	int		outwritebuf;
	wait_queue_head_t writebufq; /* write wait queue */
	
	int		blocksize;	/* Block size */

	int		eventinidx;  /* out index in event buf (circular) */
	int		eventoutidx;  /* in index in event buf (circular) */
	unsigned int	eventbuf[DAHDI_MAX_EVENTSIZE];  /* event circ. buffer */
	wait_queue_head_t eventbufq; /* event wait queue */
	
	wait_queue_head_t txstateq;	/* waiting on the tx state to change */
	
	int		readn[DAHDI_MAX_NUM_BUFS];  /* # of bytes ready in read buf */
	int		readidx[DAHDI_MAX_NUM_BUFS];  /* current read pointer */
	int		writen[DAHDI_MAX_NUM_BUFS];  /* # of bytes ready in write buf */
	int		writeidx[DAHDI_MAX_NUM_BUFS];  /* current write pointer */
	
	int		numbufs;			/* How many buffers in channel */
	int		txbufpolicy;			/* Buffer policy */
	int		rxbufpolicy;			/* Buffer policy */
	int		txdisable;				/* Disable transmitter */
	int 	rxdisable;				/* Disable receiver */
	
	
	/* Tone zone stuff */
	struct dahdi_zone *curzone;		/* Zone for selecting tones */
	int 	tonezone;				/* Tone zone for this channel */
	struct dahdi_tone *curtone;		/* Current tone we're playing (if any) */
	int		tonep;					/* Current position in tone */
	struct dahdi_tone_state ts;		/* Tone state */

	/* Pulse dial stuff */
	int	pdialcount;			/* pulse dial count */

	/* Ring cadence */
	int ringcadence[DAHDI_MAX_CADENCE];
	int firstcadencepos;				/* Where to restart ring cadence */

	/* Digit string dialing stuff */
	int		digitmode;			/* What kind of tones are we sending? */
	char	txdialbuf[DAHDI_MAX_DTMF_BUF];
	int 	dialing;
	int	afterdialingtimer;
	int		cadencepos;				/* Where in the cadence we are */

	/* I/O Mask */	
	int		iomask;  /* I/O Mux signal mask */
	wait_queue_head_t sel;	/* thingy for select stuff */
	
	/* HDLC state machines */
	struct fasthdlc_state txhdlc;
	struct fasthdlc_state rxhdlc;
	int infcs;

	/* Conferencing stuff */
	int		confna;	/* conference number (alias) */
	int		_confn;	/* Actual conference number */
	int		confmode;  /* conference mode */
	int		confmute; /* conference mute mode */

	/* Incoming and outgoing conference chunk queues for
	   communicating between DAHDI master time and
	   other boards */
	struct confq confin;
	struct confq confout;

	short	getlin[DAHDI_MAX_CHUNKSIZE];			/* Last transmitted samples */
	unsigned char getraw[DAHDI_MAX_CHUNKSIZE];		/* Last received raw data */
	short	getlin_lastchunk[DAHDI_MAX_CHUNKSIZE];	/* Last transmitted samples from last chunk */
	short	putlin[DAHDI_MAX_CHUNKSIZE];			/* Last received samples */
	unsigned char putraw[DAHDI_MAX_CHUNKSIZE];		/* Last received raw data */
	short	conflast[DAHDI_MAX_CHUNKSIZE];			/* Last conference sample -- base part of channel */
	short	conflast1[DAHDI_MAX_CHUNKSIZE];		/* Last conference sample  -- pseudo part of channel */
	short	conflast2[DAHDI_MAX_CHUNKSIZE];		/* Previous last conference sample -- pseudo part of channel */
	

	/* Is echo cancellation enabled or disabled */
	int		echocancel;
	/* The echo canceler module that should be used to create an
	   instance when this channel needs one */
	const struct dahdi_echocan *ec_factory;
	/* The echo canceler module that owns the instance currently
	   on this channel, if one is present */
	const struct dahdi_echocan *ec_current;
	/* The private state data of the echo canceler instance in use */
	struct echo_can_state *ec_state;
	echo_can_disable_detector_state_t txecdis;
	echo_can_disable_detector_state_t rxecdis;
	
	int		echostate;		/* State of echo canceller */
	int		echolastupdate;		/* Last echo can update pos */
	int		echotimer;		/* Timer for echo update */

	/* RBS timings  */
	int		prewinktime;  /* pre-wink time (ms) */
	int		preflashtime;	/* pre-flash time (ms) */
	int		winktime;  /* wink time (ms) */
	int		flashtime;  /* flash time (ms) */
	int		starttime;  /* start time (ms) */
	int		rxwinktime;  /* rx wink time (ms) */
	int		rxflashtime; /* rx flash time (ms) */
	int		debouncetime;  /* FXS GS sig debounce time (ms) */
	int		pulsebreaktime; /* pulse line open time (ms) */
	int		pulsemaketime;  /* pulse line closed time (ms) */
	int		pulseaftertime; /* pulse time between digits (ms) */

	/* RING debounce timer */
	int	ringdebtimer;
	
	/* RING trailing detector to make sure a RING is really over */
	int ringtrailer;

	/* PULSE digit receiver stuff */
	int	pulsecount;
	int	pulsetimer;

	/* RBS timers */
	int 	itimerset;		/* what the itimer was set to last */
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

	/* Idle signalling if CAS signalling */
	int idlebits;

	int deflaw;		/* 1 = mulaw, 2=alaw, 0=undefined */
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

/* defines for transmit signalling */
typedef enum {
	DAHDI_TXSIG_ONHOOK,			/* On hook */
	DAHDI_TXSIG_OFFHOOK,			/* Off hook */
	DAHDI_TXSIG_START,				/* Start / Ring */
	DAHDI_TXSIG_KEWL				/* Drop battery if possible */
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
#define DAHDI_FLAG_RBS			(1 << 12)	/* Span uses RBS signalling */

/* Channel flags */
#define DAHDI_FLAG_DTMFDECODE		(1 << 2)	/* Channel supports native DTMF decode */
#define DAHDI_FLAG_MFDECODE		(1 << 3)	/* Channel supports native MFr2 decode */
#define DAHDI_FLAG_ECHOCANCEL		(1 << 4)	/* Channel supports native echo cancellation */

#define DAHDI_FLAG_HDLC			(1 << 5)	/* Perform HDLC */
#define DAHDI_FLAG_NETDEV			(1 << 6)	/* Send to network */
#define DAHDI_FLAG_PSEUDO			(1 << 7)	/* Pseudo channel */
#define DAHDI_FLAG_CLEAR			(1 << 8)	/* Clear channel */
#define DAHDI_FLAG_AUDIO			(1 << 9)	/* Audio mode channel */

#define DAHDI_FLAG_OPEN			(1 << 10)	/* Channel is open */
#define DAHDI_FLAG_FCS			(1 << 11)	/* Calculate FCS */
/* Reserve 12 for uniqueness with span flags */
#define DAHDI_FLAG_LINEAR			(1 << 13)	/* Talk to user space in linear */
#define DAHDI_FLAG_PPP			(1 << 14)	/* PPP is available */
#define DAHDI_FLAG_T1PPP			(1 << 15)
#define DAHDI_FLAG_SIGFREEZE		(1 << 16)	/* Freeze signalling */
#define DAHDI_FLAG_NOSTDTXRX		(1 << 17)	/* Do NOT do standard transmit and receive on every interrupt */
#define DAHDI_FLAG_LOOPED			(1 << 18)	/* Loopback the receive data from the channel to the transmit */
#define DAHDI_FLAG_MTP2			(1 << 19)	/* Repeats last message in buffer and also discards repeating messages sent to us */

/* This is a redefinition of the flags from above to allow use of the kernel atomic bit testing and changing routines.
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
	void *pvt;			/* Private stuff */
	char name[40];			/* Span name */
	char desc[80];			/* Span description */
	const char *spantype;		/* span type in text form */
	const char *manufacturer;	/* span's device manufacturer */
	char devicetype[80];		/* span's device type */
	char location[40];		/* span device's location in system */
	int deflaw;			/* Default law (DAHDI_MULAW or DAHDI_ALAW) */
	int alarms;			/* Pending alarms on span */
	int flags;
	int irq;			/* IRQ for this span's hardware */
	int lbo;			/* Span Line-Buildout */
	int lineconfig;			/* Span line configuration */
	int linecompat;			/* Span line compatibility */
	int channels;			/* Number of channels in span */
	int txlevel;			/* Tx level */
	int rxlevel;			/* Rx level */
	int syncsrc;			/* current sync src (gets copied here) */
	unsigned int bpvcount;		/* BPV counter */
	unsigned int crc4count;	        /* CRC4 error counter */
	unsigned int ebitcount;		/* current E-bit error count */
	unsigned int fascount;		/* current FAS error count */

	int maintstat;			/* Maintenance state */
	wait_queue_head_t maintq;	/* Maintenance queue */
	int mainttimer;			/* Maintenance timer */
	
	int irqmisses;			/* Interrupt misses */

	int timingslips;			/* Clock slips */

	struct dahdi_chan **chans;		/* Member channel structures */

	/*   ==== Span Callback Operations ====   */
	/* Req: Set the requested chunk size.  This is the unit in which you must
	   report results for conferencing, etc */
	int (*setchunksize)(struct dahdi_span *span, int chunksize);

	/* Opt: Configure the span (if appropriate) */
	int (*spanconfig)(struct dahdi_span *span, struct dahdi_lineconfig *lc);
	
	/* Opt: Start the span */
	int (*startup)(struct dahdi_span *span);
	
	/* Opt: Shutdown the span */
	int (*shutdown)(struct dahdi_span *span);
	
	/* Opt: Enable maintenance modes */
	int (*maint)(struct dahdi_span *span, int mode);

#ifdef	DAHDI_SYNC_TICK
	/* Opt: send sync to spans */
	int (*sync_tick)(struct dahdi_span *span, int is_master);
#endif

	/* ====  Channel Callback Operations ==== */
	/* Opt: Set signalling type (if appropriate) */
	int (*chanconfig)(struct dahdi_chan *chan, int sigtype);

	/* Opt: Prepare a channel for I/O */
	int (*open)(struct dahdi_chan *chan);

	/* Opt: Close channel for I/O */
	int (*close)(struct dahdi_chan *chan);
	
	/* Opt: IOCTL */
	int (*ioctl)(struct dahdi_chan *chan, unsigned int cmd, unsigned long data);
	
	/* Opt: Native echo cancellation (simple) */
	int (*echocan)(struct dahdi_chan *chan, int ecval);

	int (*echocan_with_params)(struct dahdi_chan *chan, struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p);

	/* Okay, now we get to the signalling.  You have several options: */

	/* Option 1: If you're a T1 like interface, you can just provide a
	   rbsbits function and we'll assert robbed bits for you.  Be sure to 
	   set the DAHDI_FLAG_RBS in this case.  */

	/* Opt: If the span uses A/B bits, set them here */
	int (*rbsbits)(struct dahdi_chan *chan, int bits);
	
	/* Option 2: If you don't know about sig bits, but do have their
	   equivalents (i.e. you can disconnect battery, detect off hook,
	   generate ring, etc directly) then you can just specify a
	   sethook function, and we'll call you with appropriate hook states
	   to set.  Still set the DAHDI_FLAG_RBS in this case as well */
	int (*hooksig)(struct dahdi_chan *chan, dahdi_txsig_t hookstate);
	
	/* Option 3: If you can't use sig bits, you can write a function
	   which handles the individual hook states  */
	int (*sethook)(struct dahdi_chan *chan, int hookstate);
	
	/* Opt: Dacs the contents of chan2 into chan1 if possible */
	int (*dacs)(struct dahdi_chan *chan1, struct dahdi_chan *chan2);

	/* Opt: Used to tell an onboard HDLC controller that there is data ready to transmit */
	void (*hdlc_hard_xmit)(struct dahdi_chan *chan);

	/* Used by DAHDI only -- no user servicable parts inside */
	int spanno;			/* Span number for DAHDI */
	int offset;			/* Offset within a given card */
	int lastalarms;		/* Previous alarms */
	/* If the watchdog detects no received data, it will call the
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
	/* Transcoder channels */
	struct dahdi_transcoder_channel channels[0];
};

#define DAHDI_WATCHDOG_NOINTS		(1 << 0)

#define DAHDI_WATCHDOG_INIT			1000

#define DAHDI_WATCHSTATE_UNKNOWN		0
#define DAHDI_WATCHSTATE_OK			1
#define DAHDI_WATCHSTATE_RECOVERING	2
#define DAHDI_WATCHSTATE_FAILED		3


struct dahdi_dynamic_driver {
	/* Driver name (e.g. Eth) */
	char name[20];

	/* Driver description */
	char desc[80];

	/* Create a new transmission pipe */
	void *(*create)(struct dahdi_span *span, char *address);

	/* Destroy a created transmission pipe */
	void (*destroy)(void *tpipe);

	/* Transmit a given message */
	int (*transmit)(void *tpipe, unsigned char *msg, int msglen);

	/* Flush any pending messages */
	int (*flush)(void);

	struct dahdi_dynamic_driver *next;
};

/* Receive a dynamic span message */
void dahdi_dynamic_receive(struct dahdi_span *span, unsigned char *msg, int msglen);

/* Register a dynamic driver */
int dahdi_dynamic_register(struct dahdi_dynamic_driver *driver);

/* Unregister a dynamic driver */
void dahdi_dynamic_unregister(struct dahdi_dynamic_driver *driver);

/* Receive on a span.  The DAHDI interface will handle all the calculations for
   all member channels of the span, pulling the data from the readchunk buffer */
int dahdi_receive(struct dahdi_span *span);

/* Prepare writechunk buffers on all channels for this span */
int dahdi_transmit(struct dahdi_span *span);

/* Abort the buffer currently being receive with event "event" */
void dahdi_hdlc_abort(struct dahdi_chan *ss, int event);

/* Indicate to DAHDI that the end of frame was received and rotate buffers */
void dahdi_hdlc_finish(struct dahdi_chan *ss);

/* Put a chunk of data into the current receive buffer */
void dahdi_hdlc_putbuf(struct dahdi_chan *ss, unsigned char *rxb, int bytes);

/* Get a chunk of data from the current transmit buffer.  Returns -1 if no data
 * is left to send, 0 if there is data remaining in the current message to be sent
 * and 1 if the currently transmitted message is now done */
int dahdi_hdlc_getbuf(struct dahdi_chan *ss, unsigned char *bufptr, unsigned int *size);


/* Register a span.  Returns 0 on success, -1 on failure.  Pref-master is non-zero if
   we should have preference in being the master device */
int dahdi_register(struct dahdi_span *span, int prefmaster);

/* Allocate / free memory for a transcoder */
struct dahdi_transcoder *dahdi_transcoder_alloc(int numchans);
void dahdi_transcoder_free(struct dahdi_transcoder *ztc);

/* Register a transcoder */
int dahdi_transcoder_register(struct dahdi_transcoder *tc);

/* Unregister a transcoder */
int dahdi_transcoder_unregister(struct dahdi_transcoder *tc);

/* Alert a transcoder */
int dahdi_transcoder_alert(struct dahdi_transcoder_channel *ztc);

/* Unregister a span */
int dahdi_unregister(struct dahdi_span *span);

/* Gives a name to an LBO */
char *dahdi_lboname(int lbo);

/* Tell DAHDI about changes in received rbs bits */
void dahdi_rbsbits(struct dahdi_chan *chan, int bits);

/* Tell DAHDI abou changes in received signalling */
void dahdi_hooksig(struct dahdi_chan *chan, dahdi_rxsig_t rxsig);

/* Queue an event on a channel */
void dahdi_qevent_nolock(struct dahdi_chan *chan, int event);

/* Queue an event on a channel, locking it first */
void dahdi_qevent_lock(struct dahdi_chan *chan, int event);

/* Notify a change possible change in alarm status on a channel */
void dahdi_alarm_channel(struct dahdi_chan *chan, int alarms);

/* Notify a change possible change in alarm status on a span */
void dahdi_alarm_notify(struct dahdi_span *span);

/* Initialize a tone state */
void dahdi_init_tone_state(struct dahdi_tone_state *ts, struct dahdi_tone *zt);

/* Get a given MF tone struct, suitable for dahdi_tone_nextsample. */
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

/* Used by dynamic DAHDI -- don't use directly */
void dahdi_set_dynamic_ioctl(int (*func)(unsigned int cmd, unsigned long data));

/* Used by DAHDI HPEC module -- don't use directly */
void dahdi_set_hpec_ioctl(int (*func)(unsigned int cmd, unsigned long data));

/* Used privately by DAHDI.  Avoid touching directly */
struct dahdi_tone {
	int fac1;
	int init_v2_1;
	int init_v3_1;

	int fac2;
	int init_v2_2;
	int init_v3_2;

	int tonesamples;		/* How long to play this tone before 
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

#endif /* __KERNEL__ */

/* The following is for the PCI RADIO interface only. This is specified in
this file because external processes need to interact with the device.
Some devices have private functions used for test/diagnostic only, but
this is not the case here. */

struct dahdi_radio_stat {
	unsigned short ctcode_rx;	/* code of currently received CTCSS 
					   or DCS, 0 for none */
	unsigned short ctclass;		/* class of currently received CTCSS or
					    DCS code */
	unsigned short ctcode_tx;	/* code of currently encoded CTCSS or
					   DCS, 0 for none */
	unsigned char radstat;		/* status bits of radio */
};

#define	RAD_SERIAL_BUFLEN 128

struct dahdi_radio_param {
	unsigned short radpar;	/* param identifier */
	unsigned short index;	/* tone number */
	int data;		/* param */
	int data2;		/* param 2 */
	unsigned char buf[RAD_SERIAL_BUFLEN];
};


/* Get current status IOCTL */
#define	DAHDI_RADIO_GETSTAT	_IOR (DAHDI_CODE, 57, struct dahdi_radio_stat)
/* Set a channel parameter IOCTL */
#define	DAHDI_RADIO_SETPARAM	_IOW (DAHDI_CODE, 58, struct dahdi_radio_param)
/* Get a channel parameter IOCTL */
#define	DAHDI_RADIO_GETPARAM	_IOR (DAHDI_CODE, 59, struct dahdi_radio_param)


/* Defines for Radio Status (dahdi_radio_stat.radstat) bits */

#define	DAHDI_RADSTAT_RX	1	/* currently "receiving " */
#define	DAHDI_RADSTAT_TX	2	/* currently "transmitting" */
#define	DAHDI_RADSTAT_RXCT	4	/* currently receiving continuous tone with 
				   current settings */
#define	DAHDI_RADSTAT_RXCOR	8	/* currently receiving COR (irrelevant of COR
				   ignore) */
#define	DAHDI_RADSTAT_IGNCOR	16	/* currently ignoring COR */
#define	DAHDI_RADSTAT_IGNCT	32	/* currently ignoring CTCSS/DCS decode */
#define	DAHDI_RADSTAT_NOENCODE 64	/* currently blocking CTCSS/DCS encode */

/* Defines for Radio Parameters (dahdi_radio_param.radpar) */

#define	DAHDI_RADPAR_INVERTCOR 1	/* invert the COR signal (0/1) */
#define	DAHDI_RADPAR_IGNORECOR 2	/* ignore the COR signal (0/1) */
#define	DAHDI_RADPAR_IGNORECT 3	/* ignore the CTCSS/DCS decode (0/1) */
#define	DAHDI_RADPAR_NOENCODE 4	/* block the CTCSS/DCS encode (0/1) */
#define	DAHDI_RADPAR_CORTHRESH 5	/* COR trigger threshold (0-7) */

#define	DAHDI_RADPAR_EXTRXTONE 6	/* 0 means use internal decoder, 1 means UIOA
				   logic true is CT decode, 2 means UIOA logic
				   false is CT decode */
#define	DAHDI_RADPAR_NUMTONES	7	/* returns maximum tone index (curently 15) */
#define	DAHDI_RADPAR_INITTONE	8	/* init all tone indexes to 0 (no tones) */
#define	DAHDI_RADPAR_RXTONE	9	/* CTCSS tone, (1-32) or DCS tone (1-777),
				   or 0 meaning no tone, set index also (1-15) */
#define	DAHDI_RADPAR_RXTONECLASS 10	/* Tone class (0-65535), set index also (1-15) */
#define	DAHDI_RADPAR_TXTONE 11	/* CTCSS tone (1-32) or DCS tone (1-777) or 0
				   to indicate no tone, to transmit 
				   for this tone index (0-32, 0 disables
				   transmit CTCSS), set index also (0-15) */
#define	DAHDI_RADPAR_DEBOUNCETIME 12	/* receive indication debounce time, 
				   milliseconds (1-999) */
#define	DAHDI_RADPAR_BURSTTIME 13	/* end of transmit with no CT tone in
				   milliseconds (0-999) */


#define	DAHDI_RADPAR_UIODATA 14	/* read/write UIOA and UIOB data. Bit 0 is
				   UIOA, bit 1 is UIOB */
#define	DAHDI_RADPAR_UIOMODE 15	/* 0 means UIOA and UIOB are both outputs, 1
				   means UIOA is input, UIOB is output, 2 
				   means UIOB is input and UIOA is output,
				   3 means both UIOA and UIOB are inputs. Note
				   mode for UIOA is overridden when in
				   EXTRXTONE mode. */

#define	DAHDI_RADPAR_REMMODE 16	/* Remote control data mode */
	#define	DAHDI_RADPAR_REM_NONE 0 	/* no remote control data mode */
	#define	DAHDI_RADPAR_REM_RBI1 1	/* Doug Hall RBI-1 data mode */
	#define	DAHDI_RADPAR_REM_SERIAL 2	/* Serial Data, 9600 BPS */
	#define	DAHDI_RADPAR_REM_SERIAL_ASCII 3	/* Serial Ascii Data, 9600 BPS */

#define	DAHDI_RADPAR_REMCOMMAND 17	/* Remote conrtol write data block & do cmd */

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

#define	DAHDI_RADPAR_DEEMP 18 /* Audio De-empahsis (on or off) */ 

#define	DAHDI_RADPAR_PREEMP 19 /* Audio Pre-empahsis (on or off) */ 

#define	DAHDI_RADPAR_RXGAIN 20 /* Audio (In to system) Rx Gain */ 

#define	DAHDI_RADPAR_TXGAIN 21 /* Audio (Out from system) Tx Gain */ 

struct torisa_debug {
	unsigned int txerrors;
	unsigned int irqcount;
	unsigned int taskletsched;
	unsigned int taskletrun;
	unsigned int taskletexec;
	int span1flags;
	int span2flags;
};

/* Special torisa ioctl */
#define TORISA_GETDEBUG		_IOW (DAHDI_CODE, 60, struct torisa_debug)

/*!
	\brief Size-limited null-terminating string copy.
	\param dst The destination buffer
	\param src The source string
	\param size The size of the destination buffer
	\return Nothing.

	This is similar to \a strncpy, with two important differences:
	- the destination buffer will \b always be null-terminated
	- the destination buffer is not filled with zeros past the copied string length
	These differences make it slightly more efficient, and safer to use since it will
	not leave the destination buffer unterminated. There is no need to pass an artificially
	reduced buffer size to this function (unlike \a strncpy), and the buffer does not need
	to be initialized to zeroes prior to calling this function.
*/
static inline void dahdi_copy_string(char *dst, const char *src, unsigned int size)
{
	while (*src && size) {
		*dst++ = *src++;
		size--;
	}
	if (__builtin_expect(!size, 0))
		dst--;
	*dst = '\0';
}

#endif /* _LINUX_DAHDI_H */
