/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2006, Xorcom
 *
 * Parts derived from Cologne demo driver for the chip.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include "xpd.h"
#include "xproto.h"
#include "xpp_dahdi.h"
#include "card_pri.h"
#include "dahdi_debug.h"
#include "xpd.h"
#include "xbus-core.h"

static const char rcsid[] = "$Id$";

static DEF_PARM(int, debug, 0, 0644, "Print DBG statements");	/* must be before dahdi_debug.h */
static DEF_PARM(uint, poll_interval, 500, 0644, "Poll channel state interval in milliseconds (0 - disable)");

#define	PRI_LINES_BITMASK	BITMASK(31)
#define	PRI_DCHAN_SIGCAP	(			  \
					DAHDI_SIG_EM	| \
					DAHDI_SIG_CLEAR	| \
					DAHDI_SIG_FXSLS	| \
					DAHDI_SIG_FXSGS	| \
					DAHDI_SIG_FXSKS	| \
					DAHDI_SIG_FXOLS	| \
					DAHDI_SIG_FXOGS	| \
					DAHDI_SIG_FXOKS	| \
					DAHDI_SIG_CAS	| \
					DAHDI_SIG_DACS	| \
					DAHDI_SIG_SF	  \
				)
#define	PRI_BCHAN_SIGCAP	(DAHDI_SIG_CLEAR | DAHDI_SIG_DACS)
#define	MAX_SLAVES		4		/* we have MUX of 4 clocks */

#define	PRI_PORT(xpd)	((xpd)->addr.subunit)

/*---------------- PRI Protocol Commands ----------------------------------*/

static bool pri_packet_is_valid(xpacket_t *pack);
static void pri_packet_dump(const char *msg, xpacket_t *pack);
static int proc_pri_info_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_pri_info_write(struct file *file, const char __user *buffer, unsigned long count, void *data);
static int pri_startup(struct dahdi_span *span);
static int pri_shutdown(struct dahdi_span *span);
static int pri_lineconfig(xpd_t *xpd, int lineconfig);

#define	PROC_REGISTER_FNAME	"slics"
#define	PROC_PRI_INFO_FNAME	"pri_info"

enum pri_protocol {
	PRI_PROTO_0  	= 0,
	PRI_PROTO_E1	= 1,
	PRI_PROTO_T1 	= 2,
	PRI_PROTO_J1 	= 3
};

static const char *pri_protocol_name(enum pri_protocol pri_protocol)
{
	static const char *protocol_names[] = {
		[PRI_PROTO_0] = "??",	/* unkown */
		[PRI_PROTO_E1] = "E1",
		[PRI_PROTO_T1] = "T1",
		[PRI_PROTO_J1] = "J1"
		};
	return protocol_names[pri_protocol];
}

static int pri_num_channels(enum pri_protocol pri_protocol)
{
	static int num_channels[] = {
		[PRI_PROTO_0] = 0,
		[PRI_PROTO_E1] = 31,
		[PRI_PROTO_T1] = 24,
		[PRI_PROTO_J1] = 0
		};
	return num_channels[pri_protocol];
}

static const char *type_name(enum pri_protocol pri_protocol)
{
	static const char	*names[4] = {
		[PRI_PROTO_0] = "Unknown",
		[PRI_PROTO_E1] = "E1",
		[PRI_PROTO_T1] = "T1",
		[PRI_PROTO_J1] = "J1"
	};

	return names[pri_protocol];
}

static int pri_linecompat(enum pri_protocol pri_protocol)
{
	static const int linecompat[] = {
		[PRI_PROTO_0] = 0,
		[PRI_PROTO_E1] =
			/* coding */
			DAHDI_CONFIG_CCS |
			// CAS |
			DAHDI_CONFIG_CRC4 |
			/* framing */
			DAHDI_CONFIG_AMI | DAHDI_CONFIG_HDB3,
		[PRI_PROTO_T1] =
			/* coding */
			// DAHDI_CONFIG_D4 |
			DAHDI_CONFIG_ESF |
			/* framing */
			DAHDI_CONFIG_AMI | DAHDI_CONFIG_B8ZS,
		[PRI_PROTO_J1] = 0
	};

	DBG(GENERAL, "pri_linecompat: pri_protocol=%d\n", pri_protocol);
	return linecompat[pri_protocol];
}

#define	PRI_DCHAN_IDX(priv)	((priv)->dchan_num - 1)

enum pri_led_state {
	PRI_LED_OFF		= 0x0,
	PRI_LED_ON		= 0x1,
	/*
	 * We blink by software from driver, so that
	 * if the driver malfunction that blink would stop.
	 */
	// PRI_LED_BLINK_SLOW	= 0x2,	/* 1/2 a second blink cycle */
	// PRI_LED_BLINK_FAST	= 0x3	/* 1/4 a second blink cycle */
};

enum pri_led_selectors {
	BOTTOM_RED_LED		= 0,
	BOTTOM_GREEN_LED	= 1,
	TOP_RED_LED		= 2,
	TOP_GREEN_LED		= 3,
};

#define	NUM_LEDS	4

struct pri_leds {
	byte	state:2;	/* enum pri_led_state */
	byte	led_sel:2;	/* enum pri_led_selectors */
	byte	reserved:4;
};

#define	REG_FRS0	0x4C	/* Framer Receive Status Register 0 */
#define	REG_FRS0_T1_FSR	BIT(0)	/* T1 - Frame Search Restart Flag */
#define	REG_FRS0_LMFA	BIT(1)	/* Loss of Multiframe Alignment */
#define	REG_FRS0_E1_NMF	BIT(2)	/* E1 - No Multiframe Alignment Found */
#define	REG_FRS0_RRA	BIT(4)	/* Receive Remote Alarm: T1-YELLOW-Alarm */
#define	REG_FRS0_LFA	BIT(5)	/* Loss of Frame Alignment */
#define	REG_FRS0_AIS	BIT(6)	/* Alarm Indication Signal: T1-BLUE-Alarm */
#define	REG_FRS0_LOS	BIT(7)	/* Los Of Signal: T1-RED-Alarm */

#define	REG_FRS1	0x4D	/* Framer Receive Status Register 1 */

#define	REG_LIM0	0x36
#define	REG_LIM0_MAS	BIT(0)	/* Master Mode, DCO-R circuitry is frequency
                                                                                          synchronized to the clock supplied by SYNC */
#define	REG_LIM0_RTRS	BIT(5)	/*
				 * Receive Termination Resistance Selection:
				 * integrated resistor to create 75 Ohm termination (100 || 300 = 75)
				 * 0 = 100 Ohm
				 * 1 = 75 Ohm
				 */
#define	REG_LIM0_LL	BIT(1)	/* LL (Local Loopback) */

#define	REG_FMR0	0x1C
#define	REG_FMR0_E_RC0	BIT(4)	/* Receive Code - LSB */
#define	REG_FMR0_E_RC1	BIT(5)	/* Receive Code - MSB */
#define	REG_FMR0_E_XC0	BIT(6)	/* Transmit Code - LSB */
#define	REG_FMR0_E_XC1	BIT(7)	/* Transmit Code - MSB */

#define	REG_FMR1	0x1D
#define	REG_FMR1_XAIS	BIT(0)	/* Transmit AIS toward transmit end */
#define	REG_FMR1_SSD0	BIT(1)
#define	REG_FMR1_ECM	BIT(2)
#define	REG_FMR1_XFS	BIT(3)
#define	REG_FMR1_PMOD	BIT(4)	/* E1 = 0, T1/J1 = 1 */
#define	REG_FMR1_EDL	BIT(5)
#define	REG_FMR1_AFR	BIT(6)

#define	REG_FMR2	0x1E
#define	REG_FMR2_E_ALMF	BIT(0)	/* Automatic Loss of Multiframe */
#define	REG_FMR2_T_EXZE	BIT(0)	/* Excessive Zeros Detection Enable */
#define	REG_FMR2_E_AXRA	BIT(1)	/* Automatic Transmit Remote Alarm */
#define	REG_FMR2_T_AXRA	BIT(1)	/* Automatic Transmit Remote Alarm */
#define	REG_FMR2_E_PLB	BIT(2)	/* Payload Loop-Back */
#define	REG_FMR2_E_RFS0	BIT(6)	/* Receive Framing Select - LSB */
#define	REG_FMR2_E_RFS1	BIT(7)	/* Receive Framing Select - MSB */
#define	REG_FMR2_T_SSP	BIT(5)	/* Select Synchronization/Resynchronization Procedure */
#define	REG_FMR2_T_MCSP	BIT(6)	/* Multiple Candidates Synchronization Procedure */
#define	REG_FMR2_T_AFRS	BIT(7)	/* Automatic Force Resynchronization */

#define	REG_FMR4	0x20
#define	REG_FMR4_FM1	BIT(1)

#define REG_XSP_E	0x21
#define REG_FMR5_T	0x21
#define	REG_XSP_E_XSIF	BIT(2)	/* Transmit Spare Bit For International Use (FAS Word)  */
#define	REG_FMR5_T_XTM	BIT(2)	/* Transmit Transparent Mode  */
#define	REG_XSP_E_AXS	BIT(3)	/* Automatic Transmission of Submultiframe Status  */
#define	REG_XSP_E_EBP	BIT(4)	/* E-Bit Polarity, Si-bit position of every outgoing CRC multiframe  */
#define	REG_XSP_E_CASEN	BIT(7)	/* Channel Associated Signaling Enable  */

#define	REG_RC0		0x24
#define	REG_RC0_SJR	BIT(7)	/* T1 = 0, J1 = 1 */

#define	REG_CMR1	0x44
#define	REG_CMR1_DRSS	(BIT(7) | BIT(6))
#define	REG_CMR1_RS	(BIT(5) | BIT(4))
#define	REG_CMR1_STF	BIT(2)

struct PRI_priv_data {
	bool				clock_source;
	struct proc_dir_entry		*pri_info;
	enum pri_protocol		pri_protocol;
	int				deflaw;
	unsigned int			dchan_num;
	bool				initialized;
	bool				local_loopback;
	uint				poll_noreplies;
	uint				layer1_replies;
	byte				reg_frs0;
	byte				reg_frs1;
	bool				layer1_up;
	int				alarms;
	byte				dchan_tx_sample;
	byte				dchan_rx_sample;
	uint				dchan_tx_counter;
	uint				dchan_rx_counter;
	bool				dchan_alive;
	uint				dchan_alive_ticks;
	enum pri_led_state		ledstate[NUM_LEDS];
};

static xproto_table_t	PROTO_TABLE(PRI);

DEF_RPACKET_DATA(PRI, SET_LED,	/* Set one of the LED's */
	struct pri_leds		pri_leds;
	);


static /* 0x33 */ DECLARE_CMD(PRI, SET_LED, enum pri_led_selectors led_sel, enum pri_led_state to_led_state);

#define	DO_LED(xpd, which, tostate)	\
		CALL_PROTO(PRI, SET_LED, (xpd)->xbus, (xpd), (which), (tostate))

/*---------------- PRI: Methods -------------------------------------------*/

static int query_subunit(xpd_t *xpd, byte regnum)
{
	XPD_DBG(REGS, xpd, "(%d%d): REG=0x%02X\n",
		xpd->addr.unit, xpd->addr.subunit,
		regnum);
	return xpp_register_request(
			xpd->xbus, xpd,
			PRI_PORT(xpd),		/* portno	*/
			0,			/* writing	*/
			regnum,
			0,			/* do_subreg	*/
			0,			/* subreg	*/
			0,			/* data_L	*/
			0,			/* do_datah	*/
			0,			/* data_H	*/
			0			/* should_reply	*/
			);
}


static int write_subunit(xpd_t *xpd, byte regnum, byte val)
{
	XPD_DBG(REGS, xpd, "(%d%d): REG=0x%02X dataL=0x%02X\n",
		xpd->addr.unit, xpd->addr.subunit,
		regnum, val);
	return xpp_register_request(
			xpd->xbus, xpd,
			PRI_PORT(xpd),		/* portno	*/
			1,			/* writing	*/
			regnum,
			0,			/* do_subreg	*/
			0,			/* subreg	*/
			val,			/* data_L	*/
			0,			/* do_datah	*/
			0,			/* data_H	*/
			0			/* should_reply	*/
			);
}

static int pri_write_reg(xpd_t *xpd, int regnum, byte val)
{
	XPD_DBG(REGS, xpd, "(%d%d): REG=0x%02X dataL=0x%02X\n",
		xpd->addr.unit, xpd->addr.subunit,
		regnum, val);
	return xpp_register_request(
			xpd->xbus, xpd,
			PRI_PORT(xpd),		/* portno	*/
			1,			/* writing	*/
			regnum,
			0,			/* do_subreg	*/
			0,			/* subreg	*/
			val,			/* data_L	*/
			0,			/* do_datah	*/
			0,			/* data_H	*/
			0			/* should_reply	*/
			);
}

static void pri_proc_remove(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	XPD_DBG(PROC, xpd, "\n");
#ifdef	CONFIG_PROC_FS
	if(priv->pri_info) {
		XPD_DBG(PROC, xpd, "Removing xpd PRI_INFO file\n");
		remove_proc_entry(PROC_PRI_INFO_FNAME, xpd->proc_xpd_dir);
	}
#endif
}

static int pri_proc_create(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	XPD_DBG(PROC, xpd, "\n");
#ifdef	CONFIG_PROC_FS
	XPD_DBG(PROC, xpd, "Creating PRI_INFO file\n");
	priv->pri_info = create_proc_entry(PROC_PRI_INFO_FNAME, 0644, xpd->proc_xpd_dir);
	if(!priv->pri_info) {
		XPD_ERR(xpd, "Failed to create proc '%s'\n", PROC_PRI_INFO_FNAME);
		goto err;
	}
	priv->pri_info->owner = THIS_MODULE;
	priv->pri_info->write_proc = proc_pri_info_write;
	priv->pri_info->read_proc = proc_pri_info_read;
	priv->pri_info->data = xpd;
#endif
	return 0;
err:
	pri_proc_remove(xbus, xpd);
	return -EINVAL;
}

static bool valid_pri_modes(const xpd_t *xpd)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	if(
		priv->pri_protocol != PRI_PROTO_E1 &&
		priv->pri_protocol != PRI_PROTO_T1 &&
		priv->pri_protocol != PRI_PROTO_J1)
		return 0;
	return 1;
}

/*
 * Set E1/T1/J1
 * May only be called on unregistered xpd's
 * (the span and channel description are set according to this)
 */
static int set_pri_proto(xpd_t *xpd, enum pri_protocol set_proto)
{
	struct PRI_priv_data	*priv;
	int			deflaw;
	unsigned int		dchan_num;
	byte			fmr1 =
					REG_FMR1_AFR |
					REG_FMR1_XFS |
					REG_FMR1_ECM;
	int			default_lineconfig = 0;
	byte			rc0 = 0;	/* FIXME: PCM offsets */
	int			ret;

	BUG_ON(!xpd);
	priv = xpd->priv;
	if(SPAN_REGISTERED(xpd)) {
		XPD_NOTICE(xpd, "Registered as span %d. Cannot do setup pri protocol (%s)\n",
			xpd->span.spanno, __FUNCTION__);
		return -EBUSY;
	}
	switch(set_proto) {
		case PRI_PROTO_E1:
			deflaw = DAHDI_LAW_ALAW;
			dchan_num = 16;
			default_lineconfig = DAHDI_CONFIG_CRC4 | DAHDI_CONFIG_HDB3;
			break;
		case PRI_PROTO_T1:
			deflaw = DAHDI_LAW_MULAW;
			dchan_num = 24;
			default_lineconfig = DAHDI_CONFIG_ESF | DAHDI_CONFIG_B8ZS;
			fmr1 |= REG_FMR1_PMOD;
			break;
		case PRI_PROTO_J1:
			/*
			 * Check all assumptions
			 */
			deflaw = DAHDI_LAW_MULAW;
			dchan_num = 24;
			fmr1 |= REG_FMR1_PMOD;
			rc0 |= REG_RC0_SJR;
			default_lineconfig = 0;	/* FIXME: J1??? */
			XPD_NOTICE(xpd, "J1 is not supported yet\n");
			return -ENOSYS;
		default:
			XPD_ERR(xpd, "%s: Unknown pri protocol = %d\n",
				__FUNCTION__, set_proto);
			return -EINVAL;
	}
	priv->pri_protocol = set_proto;
	xpd->channels = pri_num_channels(set_proto);
	xpd->pcm_len = RPACKET_HEADERSIZE + sizeof(xpp_line_t)  +  xpd->channels * DAHDI_CHUNKSIZE;
	xpd->wanted_pcm_mask = BITMASK(xpd->channels);
	priv->deflaw = deflaw;
	priv->dchan_num = dchan_num;
	xpd->type_name = type_name(priv->pri_protocol);
	XPD_DBG(GENERAL, xpd, "%s, channels=%d, dchan_num=%d, deflaw=%d\n",
			pri_protocol_name(set_proto),
			xpd->channels,
			priv->dchan_num,
			priv->deflaw
			);
	write_subunit(xpd, REG_FMR1, fmr1);
#ifdef JAPANEZE_SUPPORT
	if(rc0)
		write_subunit(xpd, REG_RC0, rc0);
#endif
	/*
	 * Must set default now, so layer1 polling (Register REG_FRS0) would
	 * give reliable results.
	 */
	ret = pri_lineconfig(xpd, default_lineconfig);
	if(ret) {
		XPD_NOTICE(xpd, "Failed setting PRI default line config\n");
		return ret;
	}
	return 0;
}

static void dahdi_update_syncsrc(xpd_t *xpd)
{
	struct PRI_priv_data	*priv;
	xpd_t			*subxpd;
	int			best_spanno = 0;
	int			i;

	if(!SPAN_REGISTERED(xpd))
		return;
	for(i = 0; i < MAX_SLAVES; i++) {
		subxpd = xpd_byaddr(xpd->xbus, xpd->addr.unit, i);
		if(!subxpd)
			continue;
		priv = subxpd->priv;
		if(priv->clock_source) {
			if(best_spanno)
				XPD_ERR(xpd, "Duplicate XPD's with clock_source=1\n");
			best_spanno = subxpd->span.spanno;
		}
	}
	for(i = 0; i < MAX_SLAVES; i++) {
		subxpd = xpd_byaddr(xpd->xbus, xpd->addr.unit, i);
		if(!subxpd)
			continue;
		if(subxpd->span.syncsrc == best_spanno)
			XPD_DBG(SYNC, xpd, "Setting SyncSource to span %d\n", best_spanno);
		else
			XPD_DBG(SYNC, xpd, "Slaving to span %d\n", best_spanno);
		subxpd->span.syncsrc = best_spanno;
	}
}

/*
 * Called from:
 *   - set_master_mode() --
 *       As a result of dahdi_cfg
 *   - layer1_state() --
 *       As a result of an alarm.
 */
static void set_clocking(xpd_t *xpd)
{
	xbus_t			*xbus;
	xpd_t			*best_xpd = NULL;
	int			best_subunit = -1;	/* invalid */
	int			best_subunit_prio = 0;
	int			i;

	xbus = get_xbus(xpd->xbus->num);
	/* Find subunit with best timing priority */
	for(i = 0; i < MAX_SLAVES; i++) {
		struct PRI_priv_data	*priv;
		xpd_t			*subxpd;
		
		subxpd = xpd_byaddr(xbus, xpd->addr.unit, i);
		if(!subxpd)
			continue;
		priv = subxpd->priv;
		if(priv->alarms != 0)
			continue;
		if(subxpd->timing_priority > best_subunit_prio) {
			best_xpd = subxpd;
			best_subunit = i;
			best_subunit_prio = subxpd->timing_priority;
		}
	}
	/* Now set it */
	if(best_xpd && ((struct PRI_priv_data *)(best_xpd->priv))->clock_source == 0) {
		byte	cmr1_val =
				REG_CMR1_RS |
				REG_CMR1_STF |
				(REG_CMR1_DRSS & (best_subunit << 6));
		XPD_DBG(SYNC, best_xpd,
			"ClockSource Set: cmr1=0x%02X\n", cmr1_val);
		pri_write_reg(xpd, REG_CMR1, cmr1_val);
		((struct PRI_priv_data *)(best_xpd->priv))->clock_source = 1;
	}
	/* clear old clock sources */
	for(i = 0; i < MAX_SLAVES; i++) {
		struct PRI_priv_data	*priv;
		xpd_t			*subxpd;

		subxpd = xpd_byaddr(xbus, xpd->addr.unit, i);
		if(subxpd && subxpd != best_xpd) {
			XPD_DBG(SYNC, subxpd, "Clearing clock source\n");
			priv = subxpd->priv;
			priv->clock_source = 0;
		}
	}
	dahdi_update_syncsrc(xpd);
	put_xbus(xbus);
}

/*
 * Normally set by the timing parameter in /etc/dahdi/system.conf
 * If this is called by dahdi_cfg, than it's too late to change
 * dahdi sync priority (we are already registered)
 */
static int set_master_mode(const char *msg, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;
	byte			lim0 = 0;
	byte			xsp  = 0;
	bool			is_master_mode = xpd->timing_priority == 0;

	BUG_ON(!xpd);
	priv = xpd->priv;
	lim0 |= (priv->local_loopback) ? REG_LIM0_LL : 0;
	if(is_master_mode)
		lim0 |=  REG_LIM0_MAS;
	else
		lim0 &= ~REG_LIM0_MAS;
	if(priv->pri_protocol == PRI_PROTO_E1)
	{
		lim0 &= ~REG_LIM0_RTRS; /*  Receive termination: Integrated resistor is switched off (100 Ohm, no internal 300 Ohm)  */;
		xsp  |=  REG_XSP_E_EBP | REG_XSP_E_AXS | REG_XSP_E_XSIF;
	} else if(priv->pri_protocol == PRI_PROTO_T1) { 
		lim0 |=  REG_LIM0_RTRS; /*  Receive termination: Integrated resistor is switched on (100 Ohm || 300 Ohm = 75 Ohm) */
		xsp  |=  REG_FMR5_T_XTM;
	}
	XPD_DBG(SIGNAL, xpd, "%s(%s): %s\n", __FUNCTION__, msg, (is_master_mode) ? "MASTER" : "SLAVE");
	write_subunit(xpd, REG_LIM0 , lim0);
	write_subunit(xpd, REG_XSP_E, xsp);
	set_clocking(xpd);
	return 0;
}

static int set_localloop(const char *msg, xpd_t *xpd, bool localloop)
{
	struct PRI_priv_data	*priv;
	byte			lim0 = 0;
	byte			xsp  = 0;

	BUG_ON(!xpd);
	priv = xpd->priv;
	if(SPAN_REGISTERED(xpd)) {
		XPD_NOTICE(xpd, "Registered as span %d. Cannot do %s(%s)\n",
			xpd->span.spanno, __FUNCTION__, msg);
		return -EBUSY;
	}
	lim0 |= (localloop) ? REG_LIM0_LL : 0;
	if(priv->clock_source)
		lim0 |=  REG_LIM0_MAS;
	else
		lim0 &= ~REG_LIM0_MAS;
	if(priv->pri_protocol == PRI_PROTO_E1)
	{
		lim0 |= REG_LIM0_RTRS; /*  Receive termination: Integrated resistor is switched on (100 Ohm || 300 Ohm = 75 Ohm) */
		xsp  |= REG_XSP_E_EBP | REG_XSP_E_AXS | REG_XSP_E_XSIF;
	} else if(priv->pri_protocol == PRI_PROTO_T1) { 
		lim0 &= ~REG_LIM0_RTRS ; /*  Receive termination: Integrated resistor is switched off (100 Ohm, no internal 300 Ohm)  */;
		xsp  |=  REG_FMR5_T_XTM;
	}
	priv->local_loopback = localloop;
	XPD_DBG(SIGNAL, xpd, "%s(%s): %s\n", __FUNCTION__, msg, (localloop) ? "LOCALLOOP" : "NO");
	write_subunit(xpd, REG_LIM0 , lim0);
	write_subunit(xpd, REG_XSP_E, xsp);
	return 0;
}

#define	VALID_CONFIG(bit,flg,str)	[bit] = { .flags = flg, .name = str }

static const struct {
	const char	*name;
	const int	flags;
} valid_spanconfigs[sizeof(unsigned int)*8] = {
	/* These apply to T1 */
//	VALID_CONFIG(4, DAHDI_CONFIG_D4, "D4"),	FIXME: should support
	VALID_CONFIG(5, DAHDI_CONFIG_ESF, "ESF"),
	VALID_CONFIG(6, DAHDI_CONFIG_AMI, "AMI"),
	VALID_CONFIG(7, DAHDI_CONFIG_B8ZS, "B8ZS"),
	/* These apply to E1 */
	VALID_CONFIG(8, DAHDI_CONFIG_CCS, "CCS"),
	VALID_CONFIG(9, DAHDI_CONFIG_HDB3, "HDB3"),
	VALID_CONFIG(10, DAHDI_CONFIG_CRC4, "CRC4"),
};

static int pri_lineconfig(xpd_t *xpd, int lineconfig)
{
	struct PRI_priv_data	*priv;
	const char		*framingstr = "";
	const char		*codingstr = "";
	const char		*crcstr = "";
	byte			fmr0 = 0;  /* Dummy initilizations to    */
	byte			fmr2 = 0;  /* silense false gcc warnings */
	byte			fmr4 = 0;  /* Dummy initilizations to    */
	unsigned int		bad_bits;
	int			i;

	BUG_ON(!xpd);
	priv = xpd->priv;
	/*
	 * validate
	 */
	bad_bits = lineconfig & pri_linecompat(priv->pri_protocol);
	bad_bits = bad_bits ^ lineconfig;
	for(i = 0; i < ARRAY_SIZE(valid_spanconfigs); i++) {
		unsigned int	flags = valid_spanconfigs[i].flags;

		if(bad_bits & BIT(i)) {
			if(flags) {
				XPD_ERR(xpd,
					"Bad config item '%s' for %s. Ignore\n",
					valid_spanconfigs[i].name,
					pri_protocol_name(priv->pri_protocol));
			} else {
				/* we got real garbage */
				XPD_ERR(xpd,
					"Unknown config item 0x%lX for %s. Ignore\n",
					BIT(i),
					pri_protocol_name(priv->pri_protocol));
			}
		}
		if(flags && flags != BIT(i)) {
			ERR("%s: BUG: i=%d flags=0x%X\n",
				__FUNCTION__, i, flags);
			// BUG();
		}
	}
	if(bad_bits)
		goto bad_lineconfig;
	if(priv->pri_protocol == PRI_PROTO_E1) {
		fmr2 = REG_FMR2_E_AXRA | REG_FMR2_E_ALMF;	/* 0x03 */
		fmr4 = 0x9F;								/*  E1.XSW:  All spare bits = 1*/
	} else if(priv->pri_protocol == PRI_PROTO_T1) {
		fmr2 = REG_FMR2_T_SSP | REG_FMR2_T_AXRA;	/* 0x22 */
		fmr4 = 0x0C;
	} else if(priv->pri_protocol == PRI_PROTO_J1) {
		fmr4 = 0x1C;
		XPD_ERR(xpd, "J1 unsupported yet\n");
		return -ENOSYS;
	}
	if(priv->local_loopback)
		fmr2 |= REG_FMR2_E_PLB;
	/* framing first */
	if (lineconfig & DAHDI_CONFIG_B8ZS) {
		framingstr = "B8ZS";
		fmr0 = REG_FMR0_E_XC1 | REG_FMR0_E_XC0 | REG_FMR0_E_RC1 | REG_FMR0_E_RC0;
	} else if (lineconfig & DAHDI_CONFIG_AMI) {
		framingstr = "AMI";
		fmr0 = REG_FMR0_E_XC1 | REG_FMR0_E_RC1;
	} else if (lineconfig & DAHDI_CONFIG_HDB3) {
		framingstr = "HDB3";
		fmr0 = REG_FMR0_E_XC1 | REG_FMR0_E_XC0 | REG_FMR0_E_RC1 | REG_FMR0_E_RC0;
	}
	/* then coding */
	if (lineconfig & DAHDI_CONFIG_ESF) {
		codingstr = "ESF";
		fmr4 |= REG_FMR4_FM1;
		fmr2 |= REG_FMR2_T_AXRA | REG_FMR2_T_MCSP | REG_FMR2_T_SSP;
	} else if (lineconfig & DAHDI_CONFIG_D4) {
		codingstr = "D4";
	} else if (lineconfig & DAHDI_CONFIG_CCS) {
		codingstr = "CCS";
		/* do nothing */
	}
	/* E1's can enable CRC checking */
	if (lineconfig & DAHDI_CONFIG_CRC4) {
		crcstr = "CRC4";
		fmr2 |= REG_FMR2_E_RFS1;
	}
	XPD_DBG(GENERAL, xpd, "[%s] lineconfig=%s/%s/%s %s (0x%X)\n",
		(priv->clock_source)?"MASTER":"SLAVE",
		framingstr, codingstr, crcstr,
		(lineconfig & DAHDI_CONFIG_NOTOPEN)?"YELLOW":"",
		lineconfig);
	if(fmr0 != 0) {
		XPD_DBG(GENERAL, xpd, "%s: fmr0(0x%02X) = 0x%02X\n", __FUNCTION__, REG_FMR0, fmr0);
		write_subunit(xpd, REG_FMR0, fmr0);
	}
	XPD_DBG(GENERAL, xpd, "%s: fmr4(0x%02X) = 0x%02X\n", __FUNCTION__, REG_FMR4, fmr4);
	write_subunit(xpd, REG_FMR4, fmr4);
	XPD_DBG(GENERAL, xpd, "%s: fmr2(0x%02X) = 0x%02X\n", __FUNCTION__, REG_FMR2, fmr2);
	write_subunit(xpd, REG_FMR2, fmr2);
	return 0;
bad_lineconfig:
	XPD_ERR(xpd, "Bad lineconfig. Abort\n");
	return -EINVAL;
}

/*
 * Called only for 'span' keyword in /etc/dahdi/system.conf
 */

static int pri_spanconfig(struct dahdi_span *span, struct dahdi_lineconfig *lc)
{
	xpd_t			*xpd = span->pvt;
	struct PRI_priv_data	*priv;
	int			ret;

	BUG_ON(!xpd);
	priv = xpd->priv;
	if(lc->span != xpd->span.spanno) {
		XPD_ERR(xpd, "I am span %d but got spanconfig for span %d\n",
			xpd->span.spanno, lc->span);
		return -EINVAL;
	}
	/*
	 * FIXME: lc->name is unused by dahdi_cfg and dahdi...
	 *        We currently ignore it also.
	 */
	XPD_DBG(GENERAL, xpd, "[%s] lbo=%d lineconfig=0x%X sync=%d\n",
		(priv->clock_source)?"MASTER":"SLAVE", lc->lbo, lc->lineconfig, lc->sync);
	ret = pri_lineconfig(xpd, lc->lineconfig);
	if(!ret) {
		span->lineconfig = lc->lineconfig;
		xpd->timing_priority = lc->sync;
		set_master_mode("spanconfig", xpd);
		elect_syncer("PRI-master_mode");
	}
	return ret;
}

/*
 * Set signalling type (if appropriate)
 * Called from dahdi with spinlock held on chan. Must not call back
 * dahdi functions.
 */
static int pri_chanconfig(struct dahdi_chan *chan, int sigtype)
{
	DBG(GENERAL, "channel %d (%s) -> %s\n", chan->channo, chan->name, sig2str(sigtype));
	// FIXME: sanity checks:
	// - should be supported (within the sigcap)
	// - should not replace fxs <->fxo ??? (covered by previous?)
	return 0;
}

static xpd_t *PRI_card_new(xbus_t *xbus, int unit, int subunit, const xproto_table_t *proto_table, byte subtype, int subunits, bool to_phone)
{
	xpd_t			*xpd = NULL;
	struct PRI_priv_data	*priv;
	int			channels = min(31, CHANNELS_PERXPD);	/* worst case */
	int			ret = 0;

	XBUS_DBG(GENERAL, xbus, "\n");
	xpd = xpd_alloc(sizeof(struct PRI_priv_data), proto_table, channels);
	if(!xpd)
		return NULL;
	priv = xpd->priv;
	priv->pri_protocol = PRI_PROTO_0;		/* Default, changes in set_pri_proto() */
	priv->deflaw = DAHDI_LAW_DEFAULT;		/* Default, changes in set_pri_proto() */
	xpd->type_name = type_name(priv->pri_protocol);
	if(xpd_common_init(xbus, xpd, unit, subunit, subtype, subunits) < 0)
		goto err;
	if(pri_proc_create(xbus, xpd) < 0)
		goto err;
	/* Assume E1, changes later from user space */
	ret = set_pri_proto(xpd, PRI_PROTO_E1);
	if(ret < 0)
		goto err;
	return xpd;
err:
	xpd_free(xpd);
	return NULL;
}

static int PRI_card_init(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;
	int			ret = 0;
	xproto_table_t		*proto_table;

	BUG_ON(!xpd);
	xpd->type = XPD_TYPE_PRI;
	proto_table = &PROTO_TABLE(PRI);
	priv = xpd->priv;
	/*
	 * initialization script should have set correct
	 * operating modes.
	 */
	if(!valid_pri_modes(xpd)) {
		XPD_NOTICE(xpd, "PRI protocol not set\n");
		goto err;
	}
	xpd->type_name = type_name(priv->pri_protocol);
	xpd->direction = TO_PSTN;
	XPD_DBG(DEVICES, xpd, "%s\n", xpd->type_name);
	xpd->timing_priority = 1;		/* SLAVE */
	set_master_mode(__FUNCTION__, xpd);
	for(ret = 0; ret < NUM_LEDS; ret++) {
		DO_LED(xpd, ret, PRI_LED_ON);
		msleep(20);
		DO_LED(xpd, ret, PRI_LED_OFF);
	}
	priv->initialized = 1;
	return 0;
err:
	pri_proc_remove(xbus, xpd);
	XPD_ERR(xpd, "Failed initializing registers (%d)\n", ret);
	return ret;
}

static int PRI_card_remove(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	XPD_DBG(GENERAL, xpd, "\n");
	pri_proc_remove(xbus, xpd);
	return 0;
}

static int PRI_card_dahdi_preregistration(xpd_t *xpd, bool on)
{
	xbus_t			*xbus;
	struct PRI_priv_data	*priv;
	int			i;
	
	BUG_ON(!xpd);
	xbus = xpd->xbus;
	priv = xpd->priv;
	BUG_ON(!xbus);
	XPD_DBG(GENERAL, xpd, "%s (proto=%s, channels=%d, deflaw=%d)\n",
		(on)?"on":"off",
		pri_protocol_name(priv->pri_protocol),
		xpd->channels,
		priv->deflaw);
	if(!on) {
		/* Nothing to do yet */
		return 0;
	}
	xpd->span.spantype = pri_protocol_name(priv->pri_protocol);
	xpd->span.linecompat = pri_linecompat(priv->pri_protocol);
	xpd->span.deflaw = priv->deflaw;
	for_each_line(xpd, i) {
		struct dahdi_chan	*cur_chan = xpd->chans[i];
		bool		is_dchan = i == PRI_DCHAN_IDX(priv);

		XPD_DBG(GENERAL, xpd, "setting PRI channel %d (%s)\n", i,
			(is_dchan)?"DCHAN":"CLEAR");
		snprintf(cur_chan->name, MAX_CHANNAME, "XPP_%s/%02d/%1d%1d/%d",
				xpd->type_name, xbus->num, xpd->addr.unit, xpd->addr.subunit, i);
		cur_chan->chanpos = i + 1;
		cur_chan->pvt = xpd;
		if(is_dchan) {	/* D-CHAN */
			cur_chan->sigcap = PRI_DCHAN_SIGCAP;
			//FIXME: cur_chan->flags |= DAHDI_FLAG_PRIDCHAN;
			cur_chan->flags &= ~DAHDI_FLAG_HDLC;
		} else
			cur_chan->sigcap = PRI_BCHAN_SIGCAP;
	}
	xpd->offhook = xpd->wanted_pcm_mask;
	xpd->span.spanconfig = pri_spanconfig;
	xpd->span.chanconfig = pri_chanconfig;
	xpd->span.startup = pri_startup;
	xpd->span.shutdown = pri_shutdown;
	return 0;
}

static int PRI_card_dahdi_postregistration(xpd_t *xpd, bool on)
{
	xbus_t			*xbus;
	struct PRI_priv_data	*priv;
	
	BUG_ON(!xpd);
	xbus = xpd->xbus;
	priv = xpd->priv;
	BUG_ON(!xbus);
	XPD_DBG(GENERAL, xpd, "%s\n", (on)?"on":"off");
	dahdi_update_syncsrc(xpd);
	return(0);
}

static int PRI_card_hooksig(xbus_t *xbus, xpd_t *xpd, int pos, dahdi_txsig_t txsig)
{
	LINE_DBG(SIGNAL, xpd, pos, "%s\n", txsig2str(txsig));
	return 0;
}

static void dchan_state(xpd_t *xpd, bool up)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(priv->dchan_alive == up)
		return;
	if(!priv->layer1_up)	/* No layer1, kill dchan */
		up = 0;
	if(up) {
		XPD_DBG(SIGNAL, xpd, "STATE CHANGE: D-Channel RUNNING\n");
		priv->dchan_alive = 1;
	} else {
		int	d = PRI_DCHAN_IDX(priv);

		if(SPAN_REGISTERED(xpd) && d >= 0 && d < xpd->channels) {
			byte	*pcm;

			pcm = (byte *)xpd->span.chans[d]->readchunk;
			pcm[0] = 0x00;
			pcm = (byte *)xpd->span.chans[d]->writechunk;
			pcm[0] = 0x00;
		}
		XPD_DBG(SIGNAL, xpd, "STATE CHANGE: D-Channel STOPPED\n");
		priv->dchan_rx_counter = priv->dchan_tx_counter = 0;
		priv->dchan_alive = 0;
		priv->dchan_alive_ticks = 0;
		priv->dchan_rx_sample = priv->dchan_tx_sample = 0x00;
	}
}

/*
 * LED managment is done by the driver now:
 *   - Turn constant ON RED/GREEN led to indicate MASTER/SLAVE port
 *   - Very fast "Double Blink" to indicate Layer1 alive (without D-Channel)
 *   - Constant blink (1/2 sec cycle) to indicate D-Channel alive.
 */
static void handle_leds(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;
	unsigned int		timer_count;
	int			which_led;
	int			other_led;
	enum pri_led_state	ledstate;
	int			mod;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(xpd->timing_priority == 0) {
		which_led = TOP_RED_LED;
		other_led = BOTTOM_GREEN_LED;
	} else {
		which_led = BOTTOM_GREEN_LED;
		other_led = TOP_RED_LED;
	}
	ledstate = priv->ledstate[which_led];
	timer_count = xpd->timer_count;
	if(xpd->blink_mode) {
		if((timer_count % DEFAULT_LED_PERIOD) == 0) {
			// led state is toggled
			if(ledstate == PRI_LED_OFF) {
				DO_LED(xpd, which_led, PRI_LED_ON);
				DO_LED(xpd, other_led, PRI_LED_ON);
			} else {
				DO_LED(xpd, which_led, PRI_LED_OFF);
				DO_LED(xpd, other_led, PRI_LED_OFF);
			}
		}
		return;
	}
	if(priv->ledstate[other_led] != PRI_LED_OFF)
		DO_LED(xpd, other_led, PRI_LED_OFF);
	if(priv->dchan_alive) {
		mod = timer_count % 1000;
		switch(mod) {
			case 0:
				DO_LED(xpd, which_led, PRI_LED_ON);
				break;
			case 500:
				DO_LED(xpd, which_led, PRI_LED_OFF);
				break;
		}
	} else if(priv->layer1_up) {
		mod = timer_count % 1000;
		switch(mod) {
			case 0:
			case 100:
				DO_LED(xpd, which_led, PRI_LED_ON);
				break;
			case 50:
			case 150:
				DO_LED(xpd, which_led, PRI_LED_OFF);
				break;
		}
	} else {
		if(priv->ledstate[which_led] != PRI_LED_ON)
			DO_LED(xpd, which_led, PRI_LED_ON);
	}
}

static int PRI_card_tick(xbus_t *xbus, xpd_t *xpd)
{
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(!priv->initialized || !xbus->self_ticking)
		return 0;
	/*
	 * Poll layer1 status (cascade subunits)
	 */
	if(poll_interval != 0 &&
		((xpd->timer_count % poll_interval) == 0)) {
		priv->poll_noreplies++;
		query_subunit(xpd, REG_FRS0);
		//query_subunit(xpd, REG_FRS1);
	}
	if(priv->dchan_tx_counter >= 1 && priv->dchan_rx_counter > 1) {
		dchan_state(xpd, 1);
		priv->dchan_alive_ticks++;
	}
	handle_leds(xbus, xpd);
	return 0;
}

static int PRI_card_ioctl(xpd_t *xpd, int pos, unsigned int cmd, unsigned long arg)
{
	BUG_ON(!xpd);
	if(!TRANSPORT_RUNNING(xpd->xbus))
		return -ENODEV;
	switch (cmd) {
		case DAHDI_TONEDETECT:
			/*
			 * Asterisk call all span types with this (FXS specific)
			 * call. Silently ignore it.
			 */
			LINE_DBG(SIGNAL, xpd, pos, "PRI: Starting a call\n");
			return -ENOTTY;
		default:
			report_bad_ioctl(THIS_MODULE->name, xpd, pos, cmd);
			return -ENOTTY;
	}
	return 0;
}

static int PRI_card_close(xpd_t *xpd, lineno_t pos)
{
	//struct dahdi_chan	*chan = &xpd->span.chans[pos];
	dchan_state(xpd, 0);
	return 0;
}

/*
 * Called only for 'span' keyword in /etc/dahdi/system.conf
 */
static int pri_startup(struct dahdi_span *span)
{
	xpd_t			*xpd = span->pvt;
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(!TRANSPORT_RUNNING(xpd->xbus)) {
		XPD_DBG(GENERAL, xpd, "Startup called by dahdi. No Hardware. Ignored\n");
		return -ENODEV;
	}
	XPD_DBG(GENERAL, xpd, "STARTUP\n");
	// Turn on all channels
	CALL_XMETHOD(XPD_STATE, xpd->xbus, xpd, 1);
	return 0;
}

/*
 * Called only for 'span' keyword in /etc/dahdi/system.conf
 */
static int pri_shutdown(struct dahdi_span *span)
{
	xpd_t			*xpd = span->pvt;
	struct PRI_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(!TRANSPORT_RUNNING(xpd->xbus)) {
		XPD_DBG(GENERAL, xpd, "Shutdown called by dahdi. No Hardware. Ignored\n");
		return -ENODEV;
	}
	XPD_DBG(GENERAL, xpd, "SHUTDOWN\n");
	// Turn off all channels
	CALL_XMETHOD(XPD_STATE, xpd->xbus, xpd, 0);
	return 0;
}

/*! Copy PCM chunks from the buffers of the xpd to a new packet
 * \param xbus	xbus of source xpd.
 * \param xpd	source xpd.
 * \param lines	a bitmask of the active channels that need to be copied. 
 * \param pack	packet to be filled.
 *
 * On PRI this function is should also shift the lines mask one bit, as
 * channel 0 on the wire is an internal chip control channel. We only
 * send 31 channels to the device, but they should be called 1-31 rather
 * than 0-30 .
 */
static void PRI_card_pcm_fromspan(xbus_t *xbus, xpd_t *xpd, xpp_line_t lines, xpacket_t *pack)
{
	struct PRI_priv_data	*priv;
	byte			*pcm;
	struct dahdi_chan	**chans;
	unsigned long		flags;
	int			i;
	int			physical_chan;
	int			physical_mask = 0;

	BUG_ON(!xbus);
	BUG_ON(!xpd);
	BUG_ON(!pack);
	priv = xpd->priv;
	BUG_ON(!priv);
	pcm = RPACKET_FIELD(pack, GLOBAL, PCM_WRITE, pcm);
	spin_lock_irqsave(&xpd->lock, flags);
	chans = xpd->span.chans;
	physical_chan = 0;
	for_each_line(xpd, i) {
		if(priv->pri_protocol == PRI_PROTO_E1) {
			/* In E1 - Only 0'th channel is unused */
			if(i == 0) {
				physical_chan++;
			}
		} else if(priv->pri_protocol == PRI_PROTO_T1) {
			/* In T1 - Every 4'th channel is unused */
			if((i % 3) == 0) {
				physical_chan++;
			}
		}
		if(IS_SET(lines, i)) {
			physical_mask |= BIT(physical_chan);
			if(SPAN_REGISTERED(xpd)) {
				if(i == PRI_DCHAN_IDX(priv)) {
					if(priv->dchan_tx_sample != chans[i]->writechunk[0]) {
						priv->dchan_tx_sample = chans[i]->writechunk[0];
						priv->dchan_tx_counter++;
					} else if(chans[i]->writechunk[0] == 0xFF)
						dchan_state(xpd, 0);
				}
#ifdef	DEBUG_PCMTX
				if(pcmtx >= 0 && pcmtx_chan == i)
					memset((u_char *)pcm, pcmtx, DAHDI_CHUNKSIZE);
				else
#endif
					memcpy((u_char *)pcm, chans[i]->writechunk, DAHDI_CHUNKSIZE);
				// fill_beep((u_char *)pcm, xpd->addr.subunit, 2);
			} else
				memset((u_char *)pcm, 0x7F, DAHDI_CHUNKSIZE);
			pcm += DAHDI_CHUNKSIZE;
		}
		physical_chan++;
	}
	RPACKET_FIELD(pack, GLOBAL, PCM_WRITE, lines) = physical_mask;
	XPD_COUNTER(xpd, PCM_WRITE)++;
	spin_unlock_irqrestore(&xpd->lock, flags);
}

/*! Copy PCM chunks from the packet we recieved to the xpd struct.
 * \param xbus	xbus of target xpd.
 * \param xpd	target xpd.
 * \param pack	Source packet.
 *
 * On PRI this function is should also shift the lines back mask one bit, as
 * channel 0 on the wire is an internal chip control channel. 
 *
 * \see PRI_card_pcm_fromspan
 */
static void PRI_card_pcm_tospan(xbus_t *xbus, xpd_t *xpd, xpacket_t *pack)
{
	struct PRI_priv_data	*priv;
	byte			*pcm;
	struct dahdi_chan	**chans;
	xpp_line_t		physical_mask;
	unsigned long		flags;
	int			i;
	int			logical_chan;

	if(!SPAN_REGISTERED(xpd))
		return;
	priv = xpd->priv;
	BUG_ON(!priv);
	pcm = RPACKET_FIELD(pack, GLOBAL, PCM_READ, pcm);
	physical_mask = RPACKET_FIELD(pack, GLOBAL, PCM_WRITE, lines);
	spin_lock_irqsave(&xpd->lock, flags);
	chans = xpd->span.chans;
	logical_chan = 0;
	for (i = 0; i < CHANNELS_PERXPD; i++) {
		volatile u_char	*r;

		if(priv->pri_protocol == PRI_PROTO_E1) {
			/* In E1 - Only 0'th channel is unused */
			if(i == 0)
				continue;
		} else if(priv->pri_protocol == PRI_PROTO_T1) {
			/* In T1 - Every 4'th channel is unused */
			if((i % 4) == 0)
				continue;
		}
		if(logical_chan == PRI_DCHAN_IDX(priv)) {
			if(priv->dchan_rx_sample != pcm[0]) {
				if(debug & DBG_PCM) {
					XPD_INFO(xpd, "RX-D-Chan: prev=0x%X now=0x%X\n",
							priv->dchan_rx_sample, pcm[0]);
					dump_packet("RX-D-Chan", pack, 1);
				}
				priv->dchan_rx_sample = pcm[0];
				priv->dchan_rx_counter++;
			} else if(pcm[0] == 0xFF)
				dchan_state(xpd, 0);
		}
		if(IS_SET(physical_mask, i)) {
			r = chans[logical_chan]->readchunk;
			// memset((u_char *)r, 0x5A, DAHDI_CHUNKSIZE);	// DEBUG
			// fill_beep((u_char *)r, 1, 1);	// DEBUG: BEEP
			memcpy((u_char *)r, pcm, DAHDI_CHUNKSIZE);
			pcm += DAHDI_CHUNKSIZE;
		}
		logical_chan++;
	}
	XPD_COUNTER(xpd, PCM_READ)++;
	spin_unlock_irqrestore(&xpd->lock, flags);
}

/*---------------- PRI: HOST COMMANDS -------------------------------------*/

static /* 0x0F */ HOSTCMD(PRI, XPD_STATE, bool on)
{
	BUG_ON(!xpd);
	XPD_DBG(GENERAL, xpd, "%s\n", (on)?"on":"off");
	return 0;
}

static /* 0x33 */ HOSTCMD(PRI, SET_LED, enum pri_led_selectors led_sel, enum pri_led_state to_led_state)
{
	int			ret = 0;
	xframe_t		*xframe;
	xpacket_t		*pack;
	struct pri_leds		*pri_leds;
	struct PRI_priv_data	*priv;

	BUG_ON(!xbus);
	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	XPD_DBG(LEDS, xpd, "led_sel=%d to_state=%d\n", led_sel, to_led_state);
	XFRAME_NEW_CMD(xframe, pack, xbus, PRI, SET_LED, xpd->xbus_idx);
	pri_leds = &RPACKET_FIELD(pack, PRI, SET_LED, pri_leds);
	pri_leds->state = to_led_state;
	pri_leds->led_sel = led_sel;
	pri_leds->reserved = 0;
	XPACKET_LEN(pack) = RPACKET_SIZE(PRI, SET_LED);
	ret = send_cmd_frame(xbus, xframe);
	priv->ledstate[led_sel] = to_led_state;
	return ret;
}

/*---------------- PRI: Astribank Reply Handlers --------------------------*/
static void layer1_state(xpd_t *xpd, byte data_low)
{
	struct PRI_priv_data	*priv;
	int			alarms = 0;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	priv->poll_noreplies = 0;
	if(data_low & REG_FRS0_LOS)
		alarms |=  DAHDI_ALARM_RED;
	if(data_low & REG_FRS0_AIS)
		alarms |= DAHDI_ALARM_BLUE;
	if(data_low & REG_FRS0_RRA)
		alarms |= DAHDI_ALARM_YELLOW;
	priv->layer1_up = alarms == 0;
#if 0
	/*
	 * Some bad bits (e.g: LMFA and NMF have no alarm "colors"
	 * associated. However, layer1 is still not working if they are set.
	 * FIXME: These behave differently in E1/T1, so ignore them for while.
	 */
	if(data_low & (REG_FRS0_LMFA | REG_FRS0_E1_NMF))
		priv->layer1_up = 0;
#endif
	priv->alarms = alarms;
	if(!priv->layer1_up)
		dchan_state(xpd, 0);
	if(SPAN_REGISTERED(xpd) && xpd->span.alarms != alarms) {
		char	str1[MAX_PROC_WRITE];
		char	str2[MAX_PROC_WRITE];

		alarm2str(xpd->span.alarms, str1, sizeof(str1));
		alarm2str(alarms, str2, sizeof(str2));
		XPD_NOTICE(xpd, "Alarms: 0x%X (%s) => 0x%X (%s)\n",
				xpd->span.alarms, str1,
				alarms, str2);
		xpd->span.alarms = alarms;
		dahdi_alarm_notify(&xpd->span);
		set_clocking(xpd);
	}
	priv->reg_frs0 = data_low;
	priv->layer1_replies++;
	XPD_DBG(REGS, xpd, "subunit=%d data_low=0x%02X\n", xpd->addr.subunit, data_low);
}

static int PRI_card_register_reply(xbus_t *xbus, xpd_t *xpd, reg_cmd_t *info)
{
	unsigned long		flags;
	struct PRI_priv_data	*priv;
	struct xpd_addr		addr;
	xpd_t			*orig_xpd;

	/* Map UNIT + PORTNUM to XPD */
	orig_xpd = xpd;
	addr.unit = orig_xpd->addr.unit;
	addr.subunit = info->portnum;
	xpd = xpd_byaddr(xbus, addr.unit, addr.subunit);
	if(!xpd) {
		static int	rate_limit;

		if((rate_limit++ % 1003) < 5)
			notify_bad_xpd(__FUNCTION__, xbus, addr , orig_xpd->xpdname);
		return -EPROTO;
	}
	spin_lock_irqsave(&xpd->lock, flags);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(info->is_multibyte) {
		XPD_NOTICE(xpd, "Got Multibyte: %d bytes, eoframe: %d\n",
				info->bytes, info->eoframe);
		goto end;
	}
	if(REG_FIELD(info, regnum) == REG_FRS0 && !REG_FIELD(info, do_subreg))
		layer1_state(xpd, REG_FIELD(info, data_low));
	if(REG_FIELD(info, regnum) == REG_FRS1 && !REG_FIELD(info, do_subreg))
		priv->reg_frs1 = REG_FIELD(info, data_low);
	/* Update /proc info only if reply relate to the last slic read request */
	if(
			REG_FIELD(&xpd->requested_reply, regnum) == REG_FIELD(info, regnum) &&
			REG_FIELD(&xpd->requested_reply, do_subreg) == REG_FIELD(info, do_subreg) &&
			REG_FIELD(&xpd->requested_reply, subreg) == REG_FIELD(info, subreg)) {
		xpd->last_reply = *info;
	}
	
end:
	spin_unlock_irqrestore(&xpd->lock, flags);
	return 0;
}

static xproto_table_t PROTO_TABLE(PRI) = {
	.owner = THIS_MODULE,
	.entries = {
		/*	Table	Card	Opcode		*/
	},
	.name = "PRI",	/* protocol name */
	.ports_per_subunit = 1,
	.type = XPD_TYPE_PRI,
	.xops = {
		.card_new	= PRI_card_new,
		.card_init	= PRI_card_init,
		.card_remove	= PRI_card_remove,
		.card_dahdi_preregistration	= PRI_card_dahdi_preregistration,
		.card_dahdi_postregistration	= PRI_card_dahdi_postregistration,
		.card_hooksig	= PRI_card_hooksig,
		.card_tick	= PRI_card_tick,
		.card_pcm_fromspan	= PRI_card_pcm_fromspan,
		.card_pcm_tospan	= PRI_card_pcm_tospan,
		.card_ioctl	= PRI_card_ioctl,
		.card_close	= PRI_card_close,
		.card_register_reply	= PRI_card_register_reply,

		.XPD_STATE	= XPROTO_CALLER(PRI, XPD_STATE),
	},
	.packet_is_valid = pri_packet_is_valid,
	.packet_dump = pri_packet_dump,
};

static bool pri_packet_is_valid(xpacket_t *pack)
{
	const xproto_entry_t	*xe = NULL;
	// DBG(GENERAL, "\n");
	xe = xproto_card_entry(&PROTO_TABLE(PRI), XPACKET_OP(pack));
	return xe != NULL;
}

static void pri_packet_dump(const char *msg, xpacket_t *pack)
{
	DBG(GENERAL, "%s\n", msg);
}
/*------------------------- REGISTER Handling --------------------------*/
static int proc_pri_info_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	xpd_t			*xpd = data;
	struct PRI_priv_data	*priv;
	char			buf[MAX_PROC_WRITE];
	char			*p;
	char			*tok;
	static const char	*msg = "PROC";	/* for logs */
	int			ret = 0;
	bool			got_localloop = 0;
	bool			got_nolocalloop = 0;
	bool			got_e1 = 0;
	bool			got_t1 = 0;
	bool			got_j1 = 0;

	if(!xpd)
		return -ENODEV;
	priv = xpd->priv;
	if(count >= MAX_PROC_WRITE) {	/* leave room for null */
		XPD_ERR(xpd, "write too long (%ld)\n", count);
		return -E2BIG;
	}
	if(copy_from_user(buf, buffer, count)) {
		XPD_ERR(xpd, "Failed reading user data\n");
		return -EFAULT;
	}
	buf[count] = '\0';
	XPD_DBG(PROC, xpd, "PRI-SETUP: got %s\n", buf);
	/*
	 * First parse. Act only of *everything* is good.
	 */
	p = buf;
	while((tok = strsep(&p, " \t\v\n")) != NULL) {
		if(*tok == '\0')
			continue;
		XPD_DBG(PROC, xpd, "Got token='%s'\n", tok);
		if(strnicmp(tok, "LOCALLOOP", 8) == 0)
			got_localloop = 1;
		else if(strnicmp(tok, "NOLOCALLOOP", 8) == 0)
			got_nolocalloop = 1;
		else if(strnicmp(tok, "E1", 2) == 0)
			got_e1 = 1;
		else if(strnicmp(tok, "T1", 2) == 0)
			got_t1 = 1;
		else if(strnicmp(tok, "J1", 2) == 0) {
			got_j1 = 1;
		} else {
			XPD_NOTICE(xpd, "PRI-SETUP: unknown keyword: '%s'\n", tok);
			return -EINVAL;
		}
	}
	if(got_e1)
		ret = set_pri_proto(xpd, PRI_PROTO_E1);
	else if(got_t1)
		ret = set_pri_proto(xpd, PRI_PROTO_T1);
	else if(got_j1)
		ret = set_pri_proto(xpd, PRI_PROTO_J1);
	if(priv->pri_protocol == PRI_PROTO_0) {
		XPD_ERR(xpd,
			"Must set PRI protocol (E1/T1/J1) before setting other parameters\n");
		return -EINVAL;
	}
	if(got_localloop)
		ret = set_localloop(msg, xpd, 1);
	if(got_nolocalloop)
		ret = set_localloop(msg, xpd, 0);
	return (ret) ? ret : count;
}


static int proc_pri_info_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int			len = 0;
	unsigned long		flags;
	xpd_t			*xpd = data;
	struct PRI_priv_data	*priv;
	int			i;

	DBG(PROC, "\n");
	if(!xpd)
		return -ENODEV;
	spin_lock_irqsave(&xpd->lock, flags);
	priv = xpd->priv;
	BUG_ON(!priv);
	len += sprintf(page + len, "PRI: %s %s%s (deflaw=%d, dchan=%d)\n",
		(priv->clock_source) ? "MASTER" : "SLAVE",
		pri_protocol_name(priv->pri_protocol),
		(priv->local_loopback) ? " LOCALLOOP" : "",
		priv->deflaw, priv->dchan_num);
	len += sprintf(page + len, "%05d Layer1: ", priv->layer1_replies);
	if(priv->poll_noreplies > 1)
		len += sprintf(page + len, "No Replies [%d]\n",
			priv->poll_noreplies);
	else {
		len += sprintf(page + len, "%s\n",
				((priv->layer1_up) ?  "UP" : "DOWN"));
		len += sprintf(page + len,
				"Framer Status: FRS0=0x%02X, FRS1=0x%02X ALARMS:",
				priv->reg_frs0, priv->reg_frs1);
		if(priv->reg_frs0 & REG_FRS0_LOS)
			len += sprintf(page + len, " RED");
		if(priv->reg_frs0 & REG_FRS0_AIS)
			len += sprintf(page + len, " BLUE");
		if(priv->reg_frs0 & REG_FRS0_RRA)
			len += sprintf(page + len, " YELLOW");
		len += sprintf(page + len, "\n");
	}
	len += sprintf(page + len, "D-Channel: TX=[%5d] (0x%02X)   RX=[%5d] (0x%02X) ",
			priv->dchan_tx_counter, priv->dchan_tx_sample,
			priv->dchan_rx_counter, priv->dchan_rx_sample);
	if(priv->dchan_alive) {
		len += sprintf(page + len, "(alive %d K-ticks)\n",
			priv->dchan_alive_ticks/1000);
	} else {
		len += sprintf(page + len, "(dead)\n");
	}
	for(i = 0; i < NUM_LEDS; i++)
		len += sprintf(page + len, "LED #%d: %d\n", i, priv->ledstate[i]);
	spin_unlock_irqrestore(&xpd->lock, flags);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int __init card_pri_startup(void)
{
	DBG(GENERAL, "\n");

	INFO("revision %s\n", XPP_VERSION);
	xproto_register(&PROTO_TABLE(PRI));
	return 0;
}

static void __exit card_pri_cleanup(void)
{
	DBG(GENERAL, "\n");
	xproto_unregister(&PROTO_TABLE(PRI));
}

MODULE_DESCRIPTION("XPP PRI Card Driver");
MODULE_AUTHOR("Oron Peled <oron@actcom.co.il>");
MODULE_LICENSE("GPL");
MODULE_VERSION(XPP_VERSION);
MODULE_ALIAS_XPD(XPD_TYPE_PRI);

module_init(card_pri_startup);
module_exit(card_pri_cleanup);
