/*
 * DAHDI configuration options 
 *
 */
#ifndef _ZCONFIG_H
#define _ZCONFIG_H

#ifdef __KERNEL__
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/config.h>
#else
#include <linux/autoconf.h>
#endif
#endif

/* DAHDI compile time options */

/*
 * Uncomment if you have a European phone, or any other phone with a 
 *  short flash time.
 * This will stop the flash being mis-detected as a pulse dial "1" on
 *  phones with short flashes
 */
/* #define SHORT_FLASH_TIME */

/*
 * Uncomment to disable calibration and/or DC/DC converter tests
 * (not generally recommended)
 */
/* #define NO_CALIBRATION */
/* #define NO_DCDC */

/*
 * Boost ring voltage (Higher ring voltage, takes more power)
 * Note: this only affects the wcfxsusb and wcusb drivers; all other
 *       drivers have a 'boostringer' module parameter.
 */
/* #define BOOST_RINGER */

/*
 * Define CONFIG_CALC_XLAW if you have a small number of channels and/or
 * a small level 2 cache, to optimize for few channels
 *
 */
/* #define CONFIG_CALC_XLAW */

/*
 * Define if you want MMX optimizations in DAHDI
 *
 * Note: CONFIG_DAHDI_MMX is generally incompatible with AMD 
 * processors and can cause system instability!
 * 
 */
/* #define CONFIG_DAHDI_MMX */

/*
 * Define to turn off the echo canceler disable tone detector,
 * which will cause DAHDI to ignore the 2100 Hz echo cancel disable
 * tone.
 */
/* #define NO_ECHOCAN_DISABLE */

/* We now use the linux kernel config to detect which options to use */
/* You can still override them below */
#if defined(CONFIG_HDLC) || defined(CONFIG_HDLC_MODULE)
#define DAHDI_HDLC_TYPE_TRANS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,3)
#define HDLC_MAINTAINERS_ARE_MORE_STUPID_THAN_I_THOUGHT
#endif
#endif

#ifdef CONFIG_PPP
#define CONFIG_DAHDI_PPP
#endif

/*
 * Uncomment CONFIG_DAHDI_NET to enable SyncPPP, CiscoHDLC, and Frame Relay
 * support.
 */
/* #define CONFIG_DAHDI_NET */

/*
 * Uncomment CONFIG_OLD_HDLC_API if your are compiling with CONFIG_DAHDI_NET
 * defined and you are using the old kernel HDLC interface (or if you get
 * an error about ETH_P_HDLC while compiling).
 */
/* #define CONFIG_OLD_HDLC_API */

/*
 * Uncomment for Generic PPP support (i.e. ZapRAS)
 */
/* #define CONFIG_DAHDI_PPP */
/*
 * Uncomment to enable "watchdog" to monitor if interfaces
 * stop taking interrupts or otherwise misbehave
 */
/* #define CONFIG_DAHDI_WATCHDOG */

/*
 * Uncomment for Non-standard FXS groundstart start state (A=Low, B=Low)
 * particularly for CAC channel bank groundstart FXO ports.
 */
/* #define CONFIG_CAC_GROUNDSTART */

/* 
 * Uncomment if you happen have an early TDM400P Rev H which 
 * sometimes forgets its PCI ID to have wcfxs match essentially all
 * subvendor ID's
 */
/* #define TDM_REVH_MATCHALL */

/* 
 * Uncomment the following if you want to support E&M trunks being
 * able to "flash" after going off-hook (dont ask why, just nod :-) ).
 *
 * NOTE: *DO NOT* Enable "EMFLASH" and "EMPULSE" at the same time!!
 *
 */
/* #define EMFLASH */

/* 
 * Uncomment the following if you want to support E&M trunks being
 * able to recognize Dial Pulse digits. This can validly be enabled
 * so that either Dial Pulse or DTMF/MF tones will be recognized, but
 * the drawback is that the ONHOOK will take an extra {rxwinktime}
 * to be recognized.
 *
 * NOTE: *DO NOT* Enable "EMFLASH" and "EMPULSE" at the same time!!
 *
 */
/* #define EMPULSE */

/* 
 * Comment out the following if you dont want events to indicate the
 * beginning of an incoming ring. Most non-Asterisk applications will
 * want this commented out.
 */
#define RINGBEGIN

/* 
 * Uncomment the following if you need to support FXS Flash events.
 * Most applications will want this commented out.
 */
/* #define FXSFLASH */

/*
 * Enable sync_tick() calls. Allows low-level drivers to synchronize
 * their internal clocks to the DAHDI master clock.
 */
#define DAHDI_SYNC_TICK

/*
 * Skip processing PCM if low-level driver won't use it anyway
 */
/* #define	OPTIMIZE_CHANMUTE */

#endif
