/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2006, Xorcom
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
#include "card_fxo.h"
#include "dahdi_debug.h"
#include "xbus-core.h"

static const char rcsid[] = "$Id$";

static DEF_PARM(int, debug, 0, 0644, "Print DBG statements");
static DEF_PARM(uint, poll_battery_interval, 500, 0644, "Poll battery interval in milliseconds (0 - disable)");
#ifdef	WITH_METERING
static DEF_PARM(uint, poll_metering_interval, 500, 0644, "Poll metering interval in milliseconds (0 - disable)");
#endif
static DEF_PARM(int, ring_debounce, 50, 0644, "Number of ticks to debounce a false RING indication");
static DEF_PARM(int, caller_id_style, 0, 0444, "Caller-Id detection style: 0 - [BELL], 1 - [BT], 2 - [PASS]");

enum cid_style {
	CID_STYLE_BELL		= 0,	/* E.g: US (Bellcore) */
	CID_STYLE_ETSI_POLREV	= 1,	/* E.g: UK (British Telecom) */
	CID_STYLE_PASS_ALWAYS	= 2,	/* E.g: DK */
};

/* Signaling is opposite (fxs signalling for fxo card) */
#if 1
#define	FXO_DEFAULT_SIGCAP	(DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS)
#else
#define	FXO_DEFAULT_SIGCAP	(DAHDI_SIG_SF)
#endif

enum fxo_leds {
	LED_GREEN,
	LED_RED,
};

#define	NUM_LEDS		2
#define	DELAY_UNTIL_DIALTONE	3000

/*
 * Minimum duration for polarity reversal detection (in ticks)
 * Should be longer than the time to detect a ring, so voltage
 * fluctuation during ring won't trigger false detection.
 */
#define	POLREV_THRESHOLD	200
#define	BAT_THRESHOLD		3
#define	BAT_DEBOUNCE		1000	/* compensate for battery voltage fluctuation (in ticks) */
#define	POWER_DENIAL_CURRENT	3
#define	POWER_DENIAL_TIME	80	/* ticks */
#define	POWER_DENIAL_SAFEZONE	100	/* ticks */
#define	POWER_DENIAL_DELAY	2500	/* ticks */

/* Shortcuts */
#define	DAA_WRITE	1
#define	DAA_READ	0
#define	DAA_DIRECT_REQUEST(xbus,xpd,port,writing,reg,dL)	\
	xpp_register_request((xbus), (xpd), (port), (writing), (reg), 0, 0, (dL), 0, 0, 0)

/*---------------- FXO Protocol Commands ----------------------------------*/

static /* 0x0F */ DECLARE_CMD(FXO, XPD_STATE, bool on);

static bool fxo_packet_is_valid(xpacket_t *pack);
static void fxo_packet_dump(const char *msg, xpacket_t *pack);
static int proc_fxo_info_read(char *page, char **start, off_t off, int count, int *eof, void *data);
#ifdef	WITH_METERING
static int proc_xpd_metering_read(char *page, char **start, off_t off, int count, int *eof, void *data);
#endif
static void dahdi_report_battery(xpd_t *xpd, lineno_t chan);

#define	PROC_REGISTER_FNAME	"slics"
#define	PROC_FXO_INFO_FNAME	"fxo_info"
#ifdef	WITH_METERING
#define	PROC_METERING_FNAME	"metering_read"
#endif

#define	REG_DAA_CONTROL1	0x05	/*  5 -  DAA Control 1	*/
#define	REG_DAA_CONTROL1_OH	BIT(0)	/* Off-Hook.		*/
#define	REG_DAA_CONTROL1_ONHM	BIT(3)	/* On-Hook Line Monitor	*/

#define	DAA_REG_METERING	0x11	/* 17 */
#define	DAA_REG_CURRENT		0x1C	/* 28 */
#define	DAA_REG_VBAT		0x1D	/* 29 */

enum battery_state {
	BATTERY_UNKNOWN	= 0,
	BATTERY_ON		= 1,
	BATTERY_OFF		= -1
};

enum polarity_state {
	POL_UNKNOWN	= 0,
	POL_POSITIVE	= 1,
	POL_NEGATIVE	= -1
};

enum power_state {
	POWER_UNKNOWN	= 0,
	POWER_ON	= 1,
	POWER_OFF	= -1
};

struct FXO_priv_data {
#ifdef	WITH_METERING
	struct proc_dir_entry	*meteringfile;
#endif
	struct proc_dir_entry	*fxo_info;
	uint			poll_counter;
	signed char		battery_voltage[CHANNELS_PERXPD];
	signed char		battery_current[CHANNELS_PERXPD];
	enum battery_state	battery[CHANNELS_PERXPD];
	ushort			nobattery_debounce[CHANNELS_PERXPD];
	enum polarity_state	polarity[CHANNELS_PERXPD];
	ushort			polarity_debounce[CHANNELS_PERXPD];
	enum power_state	power[CHANNELS_PERXPD];
	xpp_line_t		maybe_power_denial;
	ushort			power_denial_debounce[CHANNELS_PERXPD];
	ushort			power_denial_delay[CHANNELS_PERXPD];
	ushort			power_denial_minimum[CHANNELS_PERXPD];
	ushort			power_denial_safezone[CHANNELS_PERXPD];
	xpp_line_t		ledstate[NUM_LEDS];	/* 0 - OFF, 1 - ON */
	xpp_line_t		ledcontrol[NUM_LEDS];	/* 0 - OFF, 1 - ON */
	int			led_counter[NUM_LEDS][CHANNELS_PERXPD];
	atomic_t		ring_debounce[CHANNELS_PERXPD];
#ifdef	WITH_METERING
	uint			metering_count[CHANNELS_PERXPD];
	xpp_line_t		metering_tone_state;
#endif
};

/*
 * LED counter values:
 *	n>1	: BLINK every n'th tick
 */
#define	LED_COUNTER(priv,pos,color)	((priv)->led_counter[color][pos])
#define	IS_BLINKING(priv,pos,color)	(LED_COUNTER(priv,pos,color) > 0)
#define	MARK_BLINK(priv,pos,color,t)	((priv)->led_counter[color][pos] = (t))
#define	MARK_OFF(priv,pos,color)	do { BIT_CLR((priv)->ledcontrol[color],(pos)); MARK_BLINK((priv),(pos),(color),0); } while(0)
#define	MARK_ON(priv,pos,color)		do { BIT_SET((priv)->ledcontrol[color],(pos)); MARK_BLINK((priv),(pos),(color),0); } while(0)

#define	LED_BLINK_RING			(1000/8)	/* in ticks */

/*---------------- FXO: Static functions ----------------------------------*/

static void reset_battery_readings(xpd_t *xpd, lineno_t pos)
{
	struct FXO_priv_data	*priv = xpd->priv;

	priv->nobattery_debounce[pos] = 0;
	priv->power_denial_debounce[pos] = 0;
	priv->power_denial_delay[pos] = 0;
	BIT_CLR(priv->maybe_power_denial, pos);
}

static const int	led_register_mask[] = { 	BIT(7),	BIT(6),	BIT(5) };

/*
 * LED control is done via DAA register 0x20
 */
static int do_led(xpd_t *xpd, lineno_t chan, byte which, bool on)
{
	int			ret = 0;
	struct FXO_priv_data	*priv;
	xbus_t			*xbus;
	byte			value;

	BUG_ON(!xpd);
	xbus = xpd->xbus;
	priv = xpd->priv;
	which = which % NUM_LEDS;
	if(IS_SET(xpd->digital_outputs, chan) || IS_SET(xpd->digital_inputs, chan))
		goto out;
	if(chan == PORT_BROADCAST) {
		priv->ledstate[which] = (on) ? ~0 : 0;
	} else {
		if(on) {
			BIT_SET(priv->ledstate[which], chan);
		} else {
			BIT_CLR(priv->ledstate[which], chan);
		}
	}
	value = 0;
	value |= ((BIT(5) | BIT(6) | BIT(7)) & ~led_register_mask[which]);
	value |= (on) ? BIT(0) : 0;
	value |= (on) ? BIT(1) : 0;
	LINE_DBG(LEDS, xpd, chan, "LED: which=%d -- %s\n", which, (on) ? "on" : "off");
	ret = DAA_DIRECT_REQUEST(xbus, xpd, chan, DAA_WRITE, 0x20, value);
out:
	return ret;
}

static void handle_fxo_leds(xpd_t *xpd)
{
	int			i;
	unsigned long		flags;
	const enum fxo_leds	colors[] = { LED_GREEN, LED_RED };
	enum fxo_leds		color;
	unsigned int		timer_count;
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	spin_lock_irqsave(&xpd->lock, flags);
	priv = xpd->priv;
	timer_count = xpd->timer_count;
	for(color = 0; color < ARRAY_SIZE(colors); color++) {
		for_each_line(xpd, i) {
			if(IS_SET(xpd->digital_outputs, i) || IS_SET(xpd->digital_inputs, i))
				continue;
			if((xpd->blink_mode & BIT(i)) || IS_BLINKING(priv, i, color)) {		// Blinking
				int	mod_value = LED_COUNTER(priv, i, color);

				if(!mod_value)
					mod_value = DEFAULT_LED_PERIOD;		/* safety value */
				// led state is toggled
				if((timer_count % mod_value) == 0) {
					LINE_DBG(LEDS, xpd, i, "ledstate=%s\n", (IS_SET(priv->ledstate[color], i))?"ON":"OFF");
					if(!IS_SET(priv->ledstate[color], i)) {
						do_led(xpd, i, color, 1);
					} else {
						do_led(xpd, i, color, 0);
					}
				}
			} else if(IS_SET(priv->ledcontrol[color], i) && !IS_SET(priv->ledstate[color], i)) {
				do_led(xpd, i, color, 1);
			} else if(!IS_SET(priv->ledcontrol[color], i) && IS_SET(priv->ledstate[color], i)) {
				do_led(xpd, i, color, 0);
			}
		}
	}
	spin_unlock_irqrestore(&xpd->lock, flags);
}

static void update_dahdi_ring(xpd_t *xpd, int pos, bool on)
{
	enum dahdi_rxsig	rxsig;

	BUG_ON(!xpd);
	if(on) {
		if(caller_id_style == CID_STYLE_BELL) {
			LINE_DBG(SIGNAL, xpd, pos, "Caller-ID PCM: off\n");
			BIT_CLR(xpd->cid_on, pos);
		}
		rxsig = DAHDI_RXSIG_RING;
	} else {
		if(caller_id_style == CID_STYLE_BELL) {
			LINE_DBG(SIGNAL, xpd, pos, "Caller-ID PCM: on\n");
			BIT_SET(xpd->cid_on, pos);
		}
		rxsig = DAHDI_RXSIG_OFFHOOK;
	}
	pcm_recompute(xpd, 0);
	/*
	 * We should not spinlock before calling dahdi_hooksig() as
	 * it may call back into our xpp_hooksig() and cause
	 * a nested spinlock scenario
	 */
	if(SPAN_REGISTERED(xpd))
		dahdi_hooksig(xpd->chans[pos], rxsig);
}

static void mark_ring(xpd_t *xpd, lineno_t pos, bool on, bool update_dahdi)
{
	struct FXO_priv_data	*priv;

	priv = xpd->priv;
	BUG_ON(!priv);
	atomic_set(&priv->ring_debounce[pos], 0);	/* Stop debouncing */
	/*
	 * We don't want to check battery during ringing
	 * due to voltage fluctuations.
	 */
	reset_battery_readings(xpd, pos);
	if(on && !xpd->ringing[pos]) {
		LINE_DBG(SIGNAL, xpd, pos, "START\n");
		xpd->ringing[pos] = 1;
		MARK_BLINK(priv, pos, LED_GREEN, LED_BLINK_RING);
		if(update_dahdi)
			update_dahdi_ring(xpd, pos, on);
	} else if(!on && xpd->ringing[pos]) {
		LINE_DBG(SIGNAL, xpd, pos, "STOP\n");
		xpd->ringing[pos] = 0;
		if(IS_BLINKING(priv, pos, LED_GREEN))
			MARK_BLINK(priv, pos, LED_GREEN, 0);
		if(update_dahdi)
			update_dahdi_ring(xpd, pos, on);
	}
}

static int do_sethook(xpd_t *xpd, int pos, bool to_offhook)
{
	unsigned long		flags;
	xbus_t			*xbus;
	struct FXO_priv_data	*priv;
	int			ret = 0;
	byte			value;

	BUG_ON(!xpd);
	BUG_ON(xpd->direction == TO_PHONE);		// We can SETHOOK state only on PSTN
	xbus = xpd->xbus;
	priv = xpd->priv;
	BUG_ON(!priv);
	if(priv->battery[pos] != BATTERY_ON && to_offhook) {
		LINE_NOTICE(xpd, pos, "Cannot take offhook while battery is off!\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&xpd->lock, flags);
	mark_ring(xpd, pos, 0, 0);				// No more rings
	value = REG_DAA_CONTROL1_ONHM;				/* Bit 3 is for CID */
	if(to_offhook)
		value |= REG_DAA_CONTROL1_OH;
	LINE_DBG(SIGNAL, xpd, pos, "SETHOOK: value=0x%02X %s\n", value, (to_offhook)?"OFFHOOK":"ONHOOK");
	if(to_offhook)
		MARK_ON(priv, pos, LED_GREEN);
	else
		MARK_OFF(priv, pos, LED_GREEN);
	ret = DAA_DIRECT_REQUEST(xbus, xpd, pos, DAA_WRITE, REG_DAA_CONTROL1, value);
	if(to_offhook) {
		BIT_SET(xpd->offhook, pos);
	} else {
		BIT_CLR(xpd->offhook, pos);
	}
	if(caller_id_style != CID_STYLE_PASS_ALWAYS) {
		LINE_DBG(SIGNAL, xpd, pos, "Caller-ID PCM: off\n");
		BIT_CLR(xpd->cid_on, pos);
	}
#ifdef	WITH_METERING
	priv->metering_count[pos] = 0;
	priv->metering_tone_state = 0L;
	DAA_DIRECT_REQUEST(xbus, xpd, pos, DAA_WRITE, DAA_REG_METERING, 0x2D);
#endif
	reset_battery_readings(xpd, pos);	/* unstable during hook changes */
	priv->power_denial_safezone[pos] = (to_offhook) ? POWER_DENIAL_SAFEZONE : 0;
	if(!to_offhook)
		priv->power[pos] = POWER_UNKNOWN;
	spin_unlock_irqrestore(&xpd->lock, flags);
	return ret;
}

/*---------------- FXO: Methods -------------------------------------------*/

static void fxo_proc_remove(xbus_t *xbus, xpd_t *xpd)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	XPD_DBG(PROC, xpd, "\n");
#ifdef	CONFIG_PROC_FS
#ifdef	WITH_METERING
	if(priv->meteringfile) {
		XPD_DBG(PROC, xpd, "Removing xpd metering tone file\n");
		priv->meteringfile->data = NULL;
		remove_proc_entry(PROC_METERING_FNAME, xpd->proc_xpd_dir);
		priv->meteringfile = NULL;
	}
#endif
	if(priv->fxo_info) {
		XPD_DBG(PROC, xpd, "Removing xpd FXO_INFO file\n");
		remove_proc_entry(PROC_FXO_INFO_FNAME, xpd->proc_xpd_dir);
		priv->fxo_info = NULL;
	}
#endif
}

static int fxo_proc_create(xbus_t *xbus, xpd_t *xpd)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
#ifdef	CONFIG_PROC_FS
	XPD_DBG(PROC, xpd, "Creating FXO_INFO file\n");
	priv->fxo_info = create_proc_read_entry(PROC_FXO_INFO_FNAME, 0444, xpd->proc_xpd_dir, proc_fxo_info_read, xpd);
	if(!priv->fxo_info) {
		XPD_ERR(xpd, "Failed to create proc file '%s'\n", PROC_FXO_INFO_FNAME);
		goto err;
	}
	priv->fxo_info->owner = THIS_MODULE;
#ifdef	WITH_METERING
	XPD_DBG(PROC, xpd, "Creating Metering tone file\n");
	priv->meteringfile = create_proc_read_entry(PROC_METERING_FNAME, 0444, xpd->proc_xpd_dir,
			proc_xpd_metering_read, xpd);
	if(!priv->meteringfile) {
		XPD_ERR(xpd, "Failed to create proc file '%s'\n", PROC_METERING_FNAME);
		goto err;
	}
	priv->meteringfile->owner = THIS_MODULE;
#endif
#endif
	return 0;
err:
	return -EINVAL;
}

static xpd_t *FXO_card_new(xbus_t *xbus, int unit, int subunit, const xproto_table_t *proto_table, byte subtype, int subunits, bool to_phone)
{
	xpd_t		*xpd = NULL;
	int		channels;

	if(to_phone) {
		XBUS_NOTICE(xbus,
			"XPD=%d%d: try to instanciate FXO with reverse direction\n",
			unit, subunit);
		return NULL;
	}
	if(subtype == 2)
		channels = min(2, CHANNELS_PERXPD);
	else
		channels = min(8, CHANNELS_PERXPD);
	xpd = xpd_alloc(sizeof(struct FXO_priv_data), proto_table, channels);
	if(!xpd)
		return NULL;
	xpd->direction = TO_PSTN;
	xpd->type_name = "FXO";
	if(xpd_common_init(xbus, xpd, unit, subunit, subtype, subunits) < 0)
		goto err;
	if(fxo_proc_create(xbus, xpd) < 0)
		goto err;
	return xpd;
err:
	xpd_free(xpd);
	return NULL;
}

static int FXO_card_init(xbus_t *xbus, xpd_t *xpd)
{
	struct FXO_priv_data	*priv;
	int			i;

	BUG_ON(!xpd);
	priv = xpd->priv;
	// Hanghup all lines
	for_each_line(xpd, i) {
		do_sethook(xpd, i, 0);
		priv->polarity[i] = POL_UNKNOWN;	/* will be updated on next battery sample */
		priv->battery[i] = BATTERY_UNKNOWN;	/* will be updated on next battery sample */
		priv->power[i] = POWER_UNKNOWN;	/* will be updated on next battery sample */
		if(caller_id_style == CID_STYLE_PASS_ALWAYS)
			BIT_SET(xpd->cid_on, i);
	}
	XPD_DBG(GENERAL, xpd, "done\n");
	for_each_line(xpd, i) {
		do_led(xpd, i, LED_GREEN, 0);
	}
	for_each_line(xpd, i) {
		do_led(xpd, i, LED_GREEN, 1);
		msleep(50);
	}
	for_each_line(xpd, i) {
		do_led(xpd, i, LED_GREEN, 0);
		msleep(50);
	}
	pcm_recompute(xpd, 0);
	return 0;
}

static int FXO_card_remove(xbus_t *xbus, xpd_t *xpd)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	XPD_DBG(GENERAL, xpd, "\n");
	fxo_proc_remove(xbus, xpd);
	return 0;
}

static int FXO_card_dahdi_preregistration(xpd_t *xpd, bool on)
{
	xbus_t			*xbus;
	struct FXO_priv_data	*priv;
	int			i;

	BUG_ON(!xpd);
	xbus = xpd->xbus;
	BUG_ON(!xbus);
	priv = xpd->priv;
	BUG_ON(!priv);
	XPD_DBG(GENERAL, xpd, "%s\n", (on)?"ON":"OFF");
	xpd->span.spantype = "FXO";
	for_each_line(xpd, i) {
		struct dahdi_chan	*cur_chan = xpd->chans[i];

		XPD_DBG(GENERAL, xpd, "setting FXO channel %d\n", i);
		snprintf(cur_chan->name, MAX_CHANNAME, "XPP_FXO/%02d/%1d%1d/%d",
			xbus->num, xpd->addr.unit, xpd->addr.subunit, i);
		cur_chan->chanpos = i + 1;
		cur_chan->pvt = xpd;
		cur_chan->sigcap = FXO_DEFAULT_SIGCAP;
	}
	for_each_line(xpd, i) {
		MARK_ON(priv, i, LED_GREEN);
		msleep(4);
		MARK_ON(priv, i, LED_RED);
	}
	return 0;
}

static int FXO_card_dahdi_postregistration(xpd_t *xpd, bool on)
{
	xbus_t			*xbus;
	struct FXO_priv_data	*priv;
	int			i;

	BUG_ON(!xpd);
	xbus = xpd->xbus;
	BUG_ON(!xbus);
	priv = xpd->priv;
	BUG_ON(!priv);
	XPD_DBG(GENERAL, xpd, "%s\n", (on)?"ON":"OFF");
	for_each_line(xpd, i) {
		dahdi_report_battery(xpd, i);
		MARK_OFF(priv, i, LED_GREEN);
		msleep(2);
		MARK_OFF(priv, i, LED_RED);
		msleep(2);
	}
	return 0;
}

static int FXO_card_hooksig(xbus_t *xbus, xpd_t *xpd, int pos, enum dahdi_txsig txsig)
{
	struct FXO_priv_data	*priv;
	int			ret = 0;

	priv = xpd->priv;
	BUG_ON(!priv);
	LINE_DBG(SIGNAL, xpd, pos, "%s\n", txsig2str(txsig));
	BUG_ON(xpd->direction != TO_PSTN);
	/* XXX Enable hooksig for FXO XXX */
	switch(txsig) {
		case DAHDI_TXSIG_START:
			break;
		case DAHDI_TXSIG_OFFHOOK:
			ret = do_sethook(xpd, pos, 1);
			break;
		case DAHDI_TXSIG_ONHOOK:
			ret = do_sethook(xpd, pos, 0);
			break;
		default:
			XPD_NOTICE(xpd, "Can't set tx state to %s (%d)\n",
				txsig2str(txsig), txsig);
			return -EINVAL;
	}
	pcm_recompute(xpd, 0);
	return ret;
}

static void dahdi_report_battery(xpd_t *xpd, lineno_t chan)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	if(SPAN_REGISTERED(xpd)) {
		switch(priv->battery[chan]) {
			case BATTERY_UNKNOWN:
				/* no-op */
				break;
			case BATTERY_OFF:
				LINE_DBG(SIGNAL, xpd, chan, "Send DAHDI_ALARM_RED\n");
				dahdi_alarm_channel(xpd->chans[chan], DAHDI_ALARM_RED);
				break;
			case BATTERY_ON:
				LINE_DBG(SIGNAL, xpd, chan, "Send DAHDI_ALARM_NONE\n");
				dahdi_alarm_channel(xpd->chans[chan], DAHDI_ALARM_NONE);
				break;
		}
	}
}

static int FXO_card_open(xpd_t *xpd, lineno_t chan)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	return 0;
}

static void poll_battery(xbus_t *xbus, xpd_t *xpd)
{
	int	i;

	for_each_line(xpd, i) {
		DAA_DIRECT_REQUEST(xbus, xpd, i, DAA_READ, DAA_REG_VBAT, 0);
	}
}

#ifdef	WITH_METERING
static void poll_metering(xbus_t *xbus, xpd_t *xpd)
{
	int	i;

	for_each_line(xpd, i) {
		if (IS_SET(xpd->offhook, i))
			DAA_DIRECT_REQUEST(xbus, xpd, i, DAA_READ, DAA_REG_METERING, 0);
	}
}
#endif

static void handle_fxo_ring(xpd_t *xpd)
{
	struct FXO_priv_data	*priv;
	int			i;

	priv = xpd->priv;
	for_each_line(xpd, i) {
		if(atomic_read(&priv->ring_debounce[i]) > 0) {
			/* Maybe start ring */
			if(atomic_dec_and_test(&priv->ring_debounce[i]))
				mark_ring(xpd, i, 1, 1);
		} else if (atomic_read(&priv->ring_debounce[i]) < 0) {
			/* Maybe stop ring */
			if(atomic_inc_and_test(&priv->ring_debounce[i]))
				mark_ring(xpd, i, 0, 1);
		}
	}
}

static void handle_fxo_power_denial(xpd_t *xpd)
{
	struct FXO_priv_data	*priv;
	int			i;

	priv = xpd->priv;
	for_each_line(xpd, i) {
		if(priv->power_denial_minimum[i] > 0) {
			priv->power_denial_minimum[i]--;
			if(priv->power_denial_minimum[i] <= 0) {
				/*
				 * But maybe the FXS started to ring (and the firmware haven't
				 * detected it yet). This would cause false power denials.
				 * So we just flag it and schedule more ticks to wait.
				 */
				LINE_DBG(SIGNAL, xpd, i, "Possible Power Denial Hangup\n");
				priv->power_denial_debounce[i] = 0;
				BIT_SET(priv->maybe_power_denial, i);
			}
		}
		if(priv->power_denial_safezone[i] > 0) {
			if(--priv->power_denial_safezone[i]) {
				/*
				 * Poll current, previous answers are meaningless
				 */
				DAA_DIRECT_REQUEST(xpd->xbus, xpd, i, DAA_READ, DAA_REG_CURRENT, 0);
			}
		}
		if(IS_SET(priv->maybe_power_denial, i) && !xpd->ringing[i] && IS_SET(xpd->offhook, i)) {
			/*
			 * Ring detection by the firmware takes some time.
			 * Therefore we delay our decision until we are
			 * sure that no ring has started during this time.
			 */
			priv->power_denial_delay[i]++;
			if (priv->power_denial_delay[i] >= POWER_DENIAL_DELAY) {
				LINE_DBG(SIGNAL, xpd, i, "Power Denial Hangup\n");
				priv->power_denial_delay[i] = 0;
				BIT_CLR(priv->maybe_power_denial, i);
				do_sethook(xpd, i, 0);
				update_line_status(xpd, i, 0);
				pcm_recompute(xpd, 0);
			}
		} else {
			priv->power_denial_delay[i] = 0;
			BIT_CLR(priv->maybe_power_denial, i);
		}
	}
}

static int FXO_card_tick(xbus_t *xbus, xpd_t *xpd)
{
	struct FXO_priv_data	*priv;

	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	if(poll_battery_interval != 0 && (priv->poll_counter % poll_battery_interval) == 0)
		poll_battery(xbus, xpd);
#ifdef	WITH_METERING
	if(poll_metering_interval != 0 && (priv->poll_counter % poll_metering_interval) == 0)
		poll_metering(xbus, xpd);
#endif
	handle_fxo_leds(xpd);
	handle_fxo_ring(xpd);
	handle_fxo_power_denial(xpd);
	priv->poll_counter++;
	return 0;
}

/* FIXME: based on data from from wctdm.h */
#include <dahdi/wctdm_user.h>
/*
 * The first register is the ACIM, the other are coefficient registers.
 * We define the array size explicitly to track possible inconsistencies
 * if the struct is modified.
 */
static const char echotune_regs[sizeof(struct wctdm_echo_coefs)] = {30, 45, 46, 47, 48, 49, 50, 51, 52};

static int FXO_card_ioctl(xpd_t *xpd, int pos, unsigned int cmd, unsigned long arg)
{
	int 			i,ret;
	unsigned char		echotune_data[ARRAY_SIZE(echotune_regs)];

	BUG_ON(!xpd);
	if(!TRANSPORT_RUNNING(xpd->xbus))
		return -ENODEV;
	switch (cmd) {
		case WCTDM_SET_ECHOTUNE:
			XPD_DBG(GENERAL, xpd, "-- Setting echo registers: \n");
			/* first off: check if this span is fxs. If not: -EINVALID */
			if (copy_from_user(&echotune_data, (void __user *)arg, sizeof(echotune_data)))
				return -EFAULT;

			for (i = 0; i < ARRAY_SIZE(echotune_regs); i++) {
				XPD_DBG(REGS, xpd, "Reg=0x%02X, data=0x%02X\n", echotune_regs[i], echotune_data[i]);
				ret = DAA_DIRECT_REQUEST(xpd->xbus, xpd, pos, DAA_WRITE, echotune_regs[i], echotune_data[i]);
				if (ret < 0) {
					LINE_NOTICE(xpd, pos, "Couldn't write %0x02X to register %0x02X\n",
							echotune_data[i], echotune_regs[i]);
					return ret;
				}
				msleep(1);
			}

			XPD_DBG(GENERAL, xpd, "-- Set echo registers successfully\n");
			break;
		case DAHDI_TONEDETECT:
			/*
			 * Asterisk call all span types with this (FXS specific)
			 * call. Silently ignore it.
			 */
			LINE_DBG(GENERAL, xpd, pos,
				"DAHDI_TONEDETECT (FXO: NOTIMPLEMENTED)\n");
			return -ENOTTY;
		default:
			report_bad_ioctl(THIS_MODULE->name, xpd, pos, cmd);
			return -ENOTTY;
	}
	return 0;
}

/*---------------- FXO: HOST COMMANDS -------------------------------------*/

static /* 0x0F */ HOSTCMD(FXO, XPD_STATE, bool on)
{
	int			ret = 0;
	struct FXO_priv_data	*priv;

	BUG_ON(!xbus);
	BUG_ON(!xpd);
	priv = xpd->priv;
	BUG_ON(!priv);
	XPD_DBG(GENERAL, xpd, "%s\n", (on) ? "on" : "off");
	return ret;
}

/*---------------- FXO: Astribank Reply Handlers --------------------------*/

HANDLER_DEF(FXO, SIG_CHANGED)
{
	xpp_line_t	sig_status = RPACKET_FIELD(pack, FXO, SIG_CHANGED, sig_status);
	xpp_line_t	sig_toggles = RPACKET_FIELD(pack, FXO, SIG_CHANGED, sig_toggles);
	unsigned long	flags;
	int		i;
	struct FXO_priv_data	*priv;

	if(!xpd) {
		notify_bad_xpd(__FUNCTION__, xbus, XPACKET_ADDR(pack), cmd->name);
		return -EPROTO;
	}
	priv = xpd->priv;
	BUG_ON(!priv);
	XPD_DBG(SIGNAL, xpd, "(PSTN) sig_toggles=0x%04X sig_status=0x%04X\n", sig_toggles, sig_status);
	spin_lock_irqsave(&xpd->lock, flags);
	for_each_line(xpd, i) {
		int	debounce;

		if(IS_SET(sig_toggles, i)) {
			if(priv->battery[i] == BATTERY_OFF) {
				/*
				 * With poll_battery_interval==0 we cannot have BATTERY_OFF
				 * so we won't get here
				 */
				LINE_NOTICE(xpd, i, "SIG_CHANGED while battery is off. Ignored.\n");
				continue;
			}
			/* First report false ring alarms */
			debounce = atomic_read(&priv->ring_debounce[i]);
			if(debounce)
				LINE_NOTICE(xpd, i, "debounced false ring (only %d ticks)\n", debounce);
			/*
			 * Now set a new ring alarm.
			 * It will be checked in handle_fxo_ring()
			 */
			debounce = (IS_SET(sig_status, i)) ? ring_debounce : -ring_debounce;
			atomic_set(&priv->ring_debounce[i], debounce);
		}
	}
	spin_unlock_irqrestore(&xpd->lock, flags);
	return 0;
}

static void update_battery_voltage(xpd_t *xpd, byte data_low, xportno_t portno)
{
	struct FXO_priv_data	*priv;
	enum polarity_state	pol;
	int			msec;
	signed char		volts = (signed char)data_low;

	priv = xpd->priv;
	BUG_ON(!priv);
	priv->battery_voltage[portno] = volts;
	if(xpd->ringing[portno])
		goto ignore_reading;	/* ring voltage create false alarms */
	if(abs(volts) < BAT_THRESHOLD) {
		/*
		 * Check for battery voltage fluctuations
		 */
		if(priv->battery[portno] != BATTERY_OFF) {
			int	milliseconds;

			milliseconds = priv->nobattery_debounce[portno]++ *
				poll_battery_interval;
			if(milliseconds > BAT_DEBOUNCE) {
				LINE_DBG(SIGNAL, xpd, portno, "BATTERY OFF voltage=%d\n", volts);
				priv->battery[portno] = BATTERY_OFF;
				if(SPAN_REGISTERED(xpd))
					dahdi_report_battery(xpd, portno);
				priv->polarity[portno] = POL_UNKNOWN;	/* What's the polarity ? */
				priv->power[portno] = POWER_UNKNOWN;	/* What's the current ? */
				/*
				 * Stop further processing for now
				 */
				goto ignore_reading;
			}

		}
	} else {
		priv->nobattery_debounce[portno] = 0;
		if(priv->battery[portno] != BATTERY_ON) {
			LINE_DBG(SIGNAL, xpd, portno, "BATTERY ON voltage=%d\n", volts);
			priv->battery[portno] = BATTERY_ON;
			if(SPAN_REGISTERED(xpd))
				dahdi_report_battery(xpd, portno);
		}
	}
#if 0
	/*
	 * Mark FXO ports without battery!
	 */
	if(priv->battery[portno] != BATTERY_ON)
		MARK_ON(priv, portno, LED_RED);
	else
		MARK_OFF(priv, portno, LED_RED);
#endif
	if(priv->battery[portno] != BATTERY_ON) {
		priv->polarity[portno] = POL_UNKNOWN;	/* What's the polarity ? */
		return;
	}
	/*
	 * Handle reverse polarity
	 */
	if(volts == 0)
		pol = POL_UNKNOWN;
	else if(volts < 0)
		pol = POL_NEGATIVE;
	else
		pol = POL_POSITIVE;
	if(priv->polarity[portno] == pol) {
		/*
		 * Same polarity, reset debounce counter
		 */
		priv->polarity_debounce[portno] = 0;
		return;
	}
	/*
	 * Track polarity reversals and debounce spikes.
	 * Only reversals with long duration count.
	 */
	msec = priv->polarity_debounce[portno]++ * poll_battery_interval;
	if (msec >= POLREV_THRESHOLD) {
		priv->polarity_debounce[portno] = 0;
		if(pol != POL_UNKNOWN) {
			char	*polname = NULL;

			if(pol == POL_POSITIVE)
				polname = "Positive";
			else if(pol == POL_NEGATIVE)
				polname = "Negative";
			else
				BUG();
			LINE_DBG(SIGNAL, xpd, portno,
				"Polarity changed to %s\n", polname);
			/*
			 * Inform dahdi/Asterisk:
			 * 1. Maybe used for hangup detection during offhook
			 * 2. In some countries used to report caller-id during onhook
			 *    but before first ring.
			 */
			if(caller_id_style == CID_STYLE_ETSI_POLREV) {
				LINE_DBG(SIGNAL, xpd, portno, "Caller-ID PCM: on\n");
				BIT_SET(xpd->cid_on, portno);	/* will be cleared on ring/offhook */
			}
			if(SPAN_REGISTERED(xpd)) {
				LINE_DBG(SIGNAL, xpd, portno,
					"Send DAHDI_EVENT_POLARITY: %s\n", polname);
				dahdi_qevent_lock(xpd->chans[portno], DAHDI_EVENT_POLARITY);
			}
		}
		priv->polarity[portno] = pol;
	}
	return;
ignore_reading:
	/*
	 * Reset debounce counters to prevent false alarms
	 */
	reset_battery_readings(xpd, portno);	/* unstable during hook changes */
}

static void update_battery_current(xpd_t *xpd, byte data_low, xportno_t portno)
{
	struct FXO_priv_data	*priv;

	priv = xpd->priv;
	BUG_ON(!priv);
	priv->battery_current[portno] = data_low;
	/*
	 * During ringing, current is not stable.
	 * During onhook there should not be current anyway.
	 */
	if(xpd->ringing[portno] || !IS_SET(xpd->offhook, portno))
		goto ignore_it;
	/*
	 * Power denial with no battery voltage is meaningless
	 */
	if(priv->battery[portno] != BATTERY_ON)
		goto ignore_it;
	/* Safe zone after offhook */
	if(priv->power_denial_safezone[portno] > 0)
		goto ignore_it;
	if(data_low < POWER_DENIAL_CURRENT) {
		if(priv->power[portno] == POWER_ON) {
			LINE_DBG(SIGNAL, xpd, portno, "power: ON -> OFF\n");
			priv->power[portno] = POWER_OFF;
			priv->power_denial_minimum[portno] = POWER_DENIAL_TIME;
		}
	} else {
		LINE_DBG(SIGNAL, xpd, portno, "power: ON\n");
		priv->power[portno] = POWER_ON;
		priv->power_denial_minimum[portno] = 0;
		update_line_status(xpd, portno, 1);
	}
	return;
ignore_it:
	BIT_CLR(priv->maybe_power_denial, portno);
	priv->power_denial_debounce[portno] = 0;
}

#ifdef	WITH_METERING
#define	BTD_BIT	BIT(0)

static void update_metering_state(xpd_t *xpd, byte data_low, lineno_t portno)
{
	struct FXO_priv_data	*priv;
	bool			metering_tone = data_low & BTD_BIT;
	bool			old_metering_tone;

	priv = xpd->priv;
	BUG_ON(!priv);
	old_metering_tone = IS_SET(priv->metering_tone_state, portno);
	LINE_DBG(SIGNAL, xpd, portno, "METERING: %s [dL=0x%X] (%d)\n",
		(metering_tone) ? "ON" : "OFF",
		data_low, priv->metering_count[portno]);
	if(metering_tone && !old_metering_tone) {
		/* Rising edge */
		priv->metering_count[portno]++;
		BIT_SET(priv->metering_tone_state, portno);
	} else if(!metering_tone && old_metering_tone)
		BIT_CLR(priv->metering_tone_state, portno);
	if(metering_tone) {
		/* Clear the BTD bit */
		data_low &= ~BTD_BIT;
		DAA_DIRECT_REQUEST(xpd->xbus, xpd, portno, DAA_WRITE, DAA_REG_METERING, data_low);
	}
}
#endif

static int FXO_card_register_reply(xbus_t *xbus, xpd_t *xpd, reg_cmd_t *info)
{
	struct FXO_priv_data	*priv;
	lineno_t		portno;

	priv = xpd->priv;
	BUG_ON(!priv);
	portno = info->portnum;
	switch(REG_FIELD(info, regnum)) {
		case DAA_REG_VBAT:
			update_battery_voltage(xpd, REG_FIELD(info, data_low), portno);
			break;
		case DAA_REG_CURRENT:
			update_battery_current(xpd, REG_FIELD(info, data_low), portno);
			break;
#ifdef	WITH_METERING
		case DAA_REG_METERING:
			update_metering_state(xpd, REG_FIELD(info, data_low), portno);
			break;
#endif
	}
	LINE_DBG(REGS, xpd, portno, "%c reg_num=0x%X, dataL=0x%X dataH=0x%X\n",
			((info->bytes == 3)?'I':'D'),
			REG_FIELD(info, regnum),
			REG_FIELD(info, data_low),
			REG_FIELD(info, data_high));
	/* Update /proc info only if reply relate to the last slic read request */
	if(
			REG_FIELD(&xpd->requested_reply, regnum) == REG_FIELD(info, regnum) &&
			REG_FIELD(&xpd->requested_reply, do_subreg) == REG_FIELD(info, do_subreg) &&
			REG_FIELD(&xpd->requested_reply, subreg) == REG_FIELD(info, subreg)) {
		xpd->last_reply = *info;
	}
	return 0;
}


static xproto_table_t PROTO_TABLE(FXO) = {
	.owner = THIS_MODULE,
	.entries = {
		/*	Prototable	Card	Opcode		*/
		XENTRY(	FXO,		FXO,	SIG_CHANGED	),
	},
	.name = "FXO",	/* protocol name */
	.ports_per_subunit = 8,
	.type = XPD_TYPE_FXO,
	.xops = {
		.card_new	= FXO_card_new,
		.card_init	= FXO_card_init,
		.card_remove	= FXO_card_remove,
		.card_dahdi_preregistration	= FXO_card_dahdi_preregistration,
		.card_dahdi_postregistration	= FXO_card_dahdi_postregistration,
		.card_hooksig	= FXO_card_hooksig,
		.card_tick	= FXO_card_tick,
		.card_pcm_fromspan	= generic_card_pcm_fromspan,
		.card_pcm_tospan	= generic_card_pcm_tospan,
		.card_ioctl	= FXO_card_ioctl,
		.card_open	= FXO_card_open,
		.card_register_reply	= FXO_card_register_reply,

		.XPD_STATE	= XPROTO_CALLER(FXO, XPD_STATE),
	},
	.packet_is_valid = fxo_packet_is_valid,
	.packet_dump = fxo_packet_dump,
};

static bool fxo_packet_is_valid(xpacket_t *pack)
{
	const xproto_entry_t	*xe;

	//DBG(GENERAL, "\n");
	xe = xproto_card_entry(&PROTO_TABLE(FXO), XPACKET_OP(pack));
	return xe != NULL;
}

static void fxo_packet_dump(const char *msg, xpacket_t *pack)
{
	DBG(GENERAL, "%s\n", msg);
}

/*------------------------- DAA Handling --------------------------*/

static int proc_fxo_info_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int			len = 0;
	unsigned long		flags;
	xpd_t			*xpd = data;
	struct FXO_priv_data	*priv;
	int			i;

	if(!xpd)
		return -ENODEV;
	spin_lock_irqsave(&xpd->lock, flags);
	priv = xpd->priv;
	BUG_ON(!priv);
	len += sprintf(page + len, "\t%-17s: ", "Channel");
	for_each_line(xpd, i) {
		if(!IS_SET(xpd->digital_outputs, i) && !IS_SET(xpd->digital_inputs, i))
			len += sprintf(page + len, "%4d ", i % 10);
	}
	len += sprintf(page + len, "\nLeds:");
	len += sprintf(page + len, "\n\t%-17s: ", "state");
	for_each_line(xpd, i) {
		if(!IS_SET(xpd->digital_outputs, i) && !IS_SET(xpd->digital_inputs, i))
			len += sprintf(page + len, "  %d%d ",
				IS_SET(priv->ledstate[LED_GREEN], i),
				IS_SET(priv->ledstate[LED_RED], i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "blinking");
	for_each_line(xpd, i) {
		if(!IS_SET(xpd->digital_outputs, i) && !IS_SET(xpd->digital_inputs, i))
			len += sprintf(page + len, "  %d%d ",
				IS_BLINKING(priv,i,LED_GREEN),
				IS_BLINKING(priv,i,LED_RED));
	}
	len += sprintf(page + len, "\nBattery-Data:");
	len += sprintf(page + len, "\n\t%-17s: ", "voltage");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->battery_voltage[i]);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "current");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->battery_current[i]);
	}
	len += sprintf(page + len, "\nBattery:");
	len += sprintf(page + len, "\n\t%-17s: ", "on");
	for_each_line(xpd, i) {
		char	*bat;

		if(priv->battery[i] == BATTERY_ON)
			bat = "+";
		else if(priv->battery[i] == BATTERY_OFF)
			bat = "-";
		else
			bat = ".";
		len += sprintf(page + len, "%4s ", bat);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "debounce");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->nobattery_debounce[i]);
	}
	len += sprintf(page + len, "\nPolarity-Reverse:");
	len += sprintf(page + len, "\n\t%-17s: ", "polarity");
	for_each_line(xpd, i) {
		char	*polname;

		if(priv->polarity[i] == POL_POSITIVE)
			polname = "+";
		else if(priv->polarity[i] == POL_NEGATIVE)
			polname = "-";
		else
			polname = ".";
		len += sprintf(page + len, "%4s ", polname);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "debounce");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->polarity_debounce[i]);
	}
	len += sprintf(page + len, "\nPower-Denial:");
	len += sprintf(page + len, "\n\t%-17s: ", "power");
	for_each_line(xpd, i) {
		char	*curr;

		if(priv->power[i] == POWER_ON)
			curr = "+";
		else if(priv->power[i] == POWER_OFF)
			curr = "-";
		else
			curr = ".";
		len += sprintf(page + len, "%4s ", curr);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "maybe");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", IS_SET(priv->maybe_power_denial, i));
	}
	len += sprintf(page + len, "\n\t%-17s: ", "debounce");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->power_denial_debounce[i]);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "safezone");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->power_denial_safezone[i]);
	}
	len += sprintf(page + len, "\n\t%-17s: ", "delay");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->power_denial_delay[i]);
	}
#ifdef	WITH_METERING
	len += sprintf(page + len, "\nMetering:");
	len += sprintf(page + len, "\n\t%-17s: ", "count");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%4d ", priv->metering_count[i]);
	}
#endif
	len += sprintf(page + len, "\n");
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

#ifdef	WITH_METERING
static int proc_xpd_metering_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int			len = 0;
	unsigned long		flags;
	xpd_t			*xpd = data;
	struct FXO_priv_data	*priv;
	int			i;

	if(!xpd)
		return -ENODEV;
	priv = xpd->priv;
	BUG_ON(!priv);
	spin_lock_irqsave(&xpd->lock, flags);
	len += sprintf(page + len, "# Chan\tMeter (since last read)\n");
	for_each_line(xpd, i) {
		len += sprintf(page + len, "%d\t%d\n",
			i, priv->metering_count[i]);
	}
	spin_unlock_irqrestore(&xpd->lock, flags);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	/* Zero meters */
	for_each_line(xpd, i)
		priv->metering_count[i] = 0;
	return len;
}
#endif

static int __init card_fxo_startup(void)
{
	if(ring_debounce <= 0) {
		ERR("ring_debounce=%d. Must be positive number of ticks\n", ring_debounce);
		return -EINVAL;
	}
	INFO("revision %s\n", XPP_VERSION);
#ifdef	WITH_METERING
	INFO("FEATURE: WITH METERING Detection\n");
#else
	INFO("FEATURE: NO METERING Detection\n");
#endif
	xproto_register(&PROTO_TABLE(FXO));
	return 0;
}

static void __exit card_fxo_cleanup(void)
{
	xproto_unregister(&PROTO_TABLE(FXO));
}

MODULE_DESCRIPTION("XPP FXO Card Driver");
MODULE_AUTHOR("Oron Peled <oron@actcom.co.il>");
MODULE_LICENSE("GPL");
MODULE_VERSION(XPP_VERSION);
MODULE_ALIAS_XPD(XPD_TYPE_FXO);

module_init(card_fxo_startup);
module_exit(card_fxo_cleanup);
