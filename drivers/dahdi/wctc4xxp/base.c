/*
 * Wildcard TC400B Driver
 *
 * Written by John Sloan <jsloan@digium.com>
 *
 * Copyright (C) 2006, Digium, Inc.
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
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>

#include <dahdi/kernel.h>
#include <dahdi/user.h>


/* #define USE_TEST_HW */
#define USE_TDM_CONFIG
#define QUIET_DSP

#define WC_MAX_IFACES 128

#define NUM_CARDS 24
#define NUM_EC	  4

/* NUM_CHANNELS must be checked if new firmware (dte_firm.h) is used */
#define NUM_CHANNELS 120

#define DTE_FORMAT_ULAW   0x00
#define DTE_FORMAT_G723_1 0x04
#define DTE_FORMAT_ALAW   0x08
#define DTE_FORMAT_G729A  0x12
#define DTE_FORMAT_UNDEF  0xFF

#define G729_LENGTH 20
#define G723_LENGTH 30

#define G729_SAMPLES 160	/* G.729 */
#define G723_SAMPLES 240 	/* G.723.1 */

#define G729_BYTES 20		/* G.729 */
#define G723_6K_BYTES 24 	/* G.723.1 at 6.3kb/s */
#define G723_5K_BYTES 20	/* G.723.1 at 5.3kb/s */
#define G723_SID_BYTES 4	/* G.723.1 SID frame */

#define ACK_SPACE 20

#define MAX_COMMANDS (NUM_CHANNELS + ACK_SPACE)
#define MAX_RCV_COMMANDS 16

/* 1432 for boot, 274 for 30msec ulaw, 194 for 20mec ulaw */
#define BOOT_CMD_LEN 1500
#define OTHER_CMD_LEN 300

#define MAX_COMMAND_LEN BOOT_CMD_LEN	/* Must be the larger of BOOT_CMD_LEN or OTHER_CMD_LEN */

#define ERING_SIZE (NUM_CHANNELS / 2)		/* Maximum ring size */

#define SFRAME_SIZE MAX_COMMAND_LEN

#define PCI_WINDOW_SIZE ((2*  2 * ERING_SIZE * SFRAME_SIZE) + (2 * ERING_SIZE * 4))

#define MDIO_SHIFT_CLK		0x10000
#define MDIO_DATA_WRITE0 	0x00000
#define MDIO_DATA_WRITE1 	0x20000
#define MDIO_ENB		0x00000
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ		0x80000

#define RCV_CSMENCAPS     1
#define RCV_RTP           2
#define RCV_CSMENCAPS_ACK 3
#define RCV_OTHER         99


/* TDM Commands */
#define CMD_MSG_TDM_SELECT_BUS_MODE_LEN 30
#define CMD_MSG_TDM_SELECT_BUS_MODE(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x01, 0x00,0x06,0x17,0x04, 0xFF,0xFF, \
	0x04,0x00 }
#define CMD_MSG_TDM_ENABLE_BUS_LEN 30
#define CMD_MSG_TDM_ENABLE_BUS(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x02, 0x00,0x06,0x05,0x04, 0xFF,0xFF, \
	0x04,0x00 }
#define CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN 34
#define CMD_MSG_SUPVSR_SETUP_TDM_PARMS(s,p1,p2,p3) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x10, p1, 0x00,0x06,0x07,0x04, 0xFF,0xFF, \
	p2,0x83, 0x00,0x0C, 0x00,0x00, p3,0x00 }
#define CMD_MSG_TDM_OPT_LEN 30
#define CMD_MSG_TDM_OPT(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x35,0x04, 0xFF,0xFF, \
	0x00,0x00 }
#define CMD_MSG_DEVICE_SET_COUNTRY_CODE_LEN 30
#define CMD_MSG_DEVICE_SET_COUNTRY_CODE(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x1B,0x04, 0xFF,0xFF, \
	0x00,0x00 }

/* CPU Commands */
#define CMD_MSG_SET_ARM_CLK_LEN 32
#define CMD_MSG_SET_ARM_CLK(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0C, 0x00, 0x00,0x06,0x11,0x04, 0x00,0x00, \
	0x2C,0x01, 0x00,0x00 }
#define CMD_MSG_SET_SPU_CLK_LEN 32
#define CMD_MSG_SET_SPU_CLK(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0C, 0x00, 0x00,0x06,0x12,0x04, 0x00,0x00, \
	0x2C,0x01, 0x00,0x00 }
#define CMD_MSG_SPU_FEATURES_CONTROL_LEN 30
#define CMD_MSG_SPU_FEATURES_CONTROL(s,p1) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x13,0x00, 0xFF,0xFF, \
	p1,0x00 }
#define CMD_MSG_DEVICE_STATUS_CONFIG_LEN 30
#define CMD_MSG_DEVICE_STATUS_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x0F,0x04, 0xFF,0xFF, \
	0x05,0x00 }

/* General IP/RTP Commands */
#define CMD_MSG_SET_ETH_HEADER_LEN 44
#define CMD_MSG_SET_ETH_HEADER(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x18, 0x00, 0x00,0x06,0x00,0x01, 0xFF,0xFF, \
	0x01,0x00, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x00,0x11,0x22,0x33,0x44,0x55, 0x08,0x00 }
#define CMD_MSG_IP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_IP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x02,0x03, 0xFF,0xFF, \
	0x00,0x02 }
#define CMD_MSG_ARP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_ARP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x05,0x01, 0xFF,0xFF, \
	0x01,0x00 }
#define CMD_MSG_ICMP_SERVICE_CONFIG_LEN 30
#define CMD_MSG_ICMP_SERVICE_CONFIG(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x04,0x03, 0xFF,0xFF, \
	0x01,0xFF }
#define CMD_MSG_IP_OPTIONS_LEN 30
#define CMD_MSG_IP_OPTIONS(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x06,0x03, 0xFF,0xFF, \
	0x02,0x00 }

/* Supervisor channel commands */
#define CMD_MSG_CREATE_CHANNEL_LEN 32
#define CMD_MSG_CREATE_CHANNEL(s,t) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0C, 0x00, 0x00,0x06,0x10,0x00, 0x00,0x00, \
	0x02,0x00, (t&0x00FF), ((t&0xFF00) >> 8) }
#define CMD_MSG_QUERY_CHANNEL_LEN 30
#define CMD_MSG_QUERY_CHANNEL(s,t) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x01,0x06,0x10,0x00, 0x00,0x00, \
	(t&0x00FF), ((t&0xFF00) >> 8) }
#define CMD_MSG_TRANS_CONNECT_LEN 38
#define CMD_MSG_TRANS_CONNECT(s,e,c1,c2,f1,f2) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x12, 0x00, 0x00,0x06,0x22,0x93, 0x00,0x00, \
	e,0x00, (c1&0x00FF),((c1&0xFF00)>>8), f1,0x00, (c2&0x00FF),((c2&0xFF00)>>8), f2,0x00 }
#define CMD_MSG_DESTROY_CHANNEL_LEN 32
#define CMD_MSG_DESTROY_CHANNEL(s,t) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x11,0x00, 0x00,0x00, \
	(t&0x00FF),((t&0xFF00)>>8), 0x00, 0x00 }

/* Individual channel config commands */
#define CMD_MSG_SET_IP_HDR_CHANNEL_LEN 58
#define CMD_MSG_SET_IP_HDR_CHANNEL(s,c,t2,t1) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00) >> 8),(c&0x00FF), 0x26, 0x00, 0x00,0x02,0x00,0x90, 0x00,0x00, \
	0x00,0x00, 0x45,0x00, 0x00,0x00, 0x00,0x00, 0x40,0x00, 0x80,0x11, 0x00,0x00, \
	0xC0,0xA8,0x09,0x03, 0xC0,0xA8,0x09,0x03, \
	((t2&0xFF00)>>8)+0x50,(t2&0x00FF), ((t1&0xFF00)>>8)+0x50,(t1&0x00FF), 0x00,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_VCEOPT_LEN 40
#define CMD_MSG_VOIP_VCEOPT(s,c,l,w) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x12, 0x00, 0x00,0x02,0x01,0x80, 0x00,0x00, \
	0x21,l, 0x00,0x1C, 0x04,0x00, 0x00,0x00, w,0x00, 0x80,0x11 }
#define CMD_MSG_VOIP_VOPENA_LEN 44
#define CMD_MSG_VOIP_VOPENA(s,c,f) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x16, 0x00, 0x00,0x02,0x00,0x80, 0x00,0x00, \
	0x01,0x00, 0x80,f, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x12,0x34, 0x56,0x78, 0x00,0x00 }
#define CMD_MSG_VOIP_VOPENA_CLOSE_LEN 32
#define CMD_MSG_VOIP_VOPENA_CLOSE(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x00,0x80, 0x00,0x00, \
	0x00,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_INDCTRL_LEN 32
#define CMD_MSG_VOIP_INDCTRL(s,c) {0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x84,0x80, 0x00,0x00, \
	0x07,0x00, 0x00,0x00 }
#define CMD_MSG_VOIP_DTMFOPT_LEN 32
#define CMD_MSG_VOIP_DTMFOPT(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x02,0x80, 0x00,0x00, \
	0x08,0x00, 0x00,0x00 }

#define CMD_MSG_VOIP_TONECTL_LEN 32
#define CMD_MSG_VOIP_TONECTL(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, ((c&0xFF00)>>8),(c&0x00FF), 0x0A, 0x00, 0x00,0x02,0x5B,0x80, 0x00,0x00, \
	0x00,0x00, 0x00,0x00 }

/* CPU ACK command */ 
#define CMD_MSG_ACK_LEN 20
#define CMD_MSG_ACK(s,c) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s, 0xE0, (c&0x00FF), ((c>>8)&0x00FF) }

/* Wrapper for RTP packets */
#define CMD_MSG_IP_UDP_RTP_LEN 54
#define CMD_MSG_IP_UDP_RTP(p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17,p18,p19,p20,s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x08,0x00, \
	0x45,0x00, p1,p2, 0x00,p3, 0x40,0x00, 0x80,0x11, p4,p5, \
	0xC0,0xA8,0x09,0x03, 0xC0,0xA8,0x09,0x03, p6,p7, p8,p9, p10,p11, p12,p13, \
	0x80,p14, p15,p16, p17,p18,p19,p20, 0x12,0x34,0x56,(s&0xFF)}

#define CMD_MSG_DW_WRITE_LEN 38
#define CMD_MSG_DW_WRITE(s,a,d) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01,s&0x0F,0x00,0x00,0x00,0x00,0x00,0x04,0x17,0x00,0x00, \
	((a>>24)&0x00FF),((a>>16)&0x00FF), ((a>>8)&0x00FF),(a&0x00FF), \
	((d>>24)&0x00FF),((d>>16)&0x00FF), ((d>>8)&0x00FF),(d&0x00FF) }

#define CMD_MSG_FORCE_ALERT_LEN 32
#define CMD_MSG_FORCE_ALERT(s) { \
	0x00,0x11,0x22,0x33,0x44,0x55, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF, 0x88,0x9B, \
	0x00,0x01, s&0x0F, 0x01, 0xFF,0xFF, 0x0A, 0x00, 0x00,0x06,0x09,0x04, 0x00,0x00, \
	0x24,0x00, 0x00,0x00 }

#define dahdi_send_cmd(wc, command, length, hex) \
	({ \
		int ret = 0; \
		do { \
	 		if (ret == 2) \
	 		{ \
				wc->ztsnd_rtx++; \
	 			if (hex == 0x0010) \
					wc->ztsnd_0010_rtx++; \
			} \
			down(&wc->cmdqsem); \
	 		wc->last_command_sent = hex; \
			if ( (((wc->cmdq_wndx + 1) % MAX_COMMANDS) == wc->cmdq_rndx) && debug ) \
				printk("wcdte error: cmdq is full.\n"); \
			else { \
				unsigned char fifo[OTHER_CMD_LEN] = command; \
				int i; \
				wc->cmdq[wc->cmdq_wndx].cmdlen = length; \
				for (i = 0; i < length; i++) \
					wc->cmdq[wc->cmdq_wndx].cmd[i] = fifo[i]; \
				wc->last_seqno = fifo[16]; \
				wc->cmdq_wndx = (wc->cmdq_wndx + 1) % MAX_COMMANDS; \
			} \
			__transmit_demand(wc); \
			up(&wc->cmdqsem); \
			if (hex == 0x0000) \
				ret = wcdte_waitfor_csmencaps(wc, RCV_CSMENCAPS_ACK, 2); \
			else { \
				ret = wcdte_waitfor_csmencaps(wc, RCV_CSMENCAPS, 0); \
				if (wc->dsp_crashed) \
					return 1; \
			} \
			if (ret == 1) \
				return(1); \
		} while (ret == 2); \
	})


struct cmdq {
	unsigned int cmdlen;
	unsigned char cmd[MAX_COMMAND_LEN];
};

struct wcdte {
	struct pci_dev *dev;
	char *variety;
	unsigned int intcount;
	unsigned int rxints;
	unsigned int txints;
	unsigned int intmask;
	int pos;
	int freeregion;
	int rdbl;
	int tdbl;
	int cards;
	spinlock_t reglock;
	wait_queue_head_t regq;
	int rcvflags;
	
	struct semaphore chansem;
	struct semaphore cmdqsem;
	struct cmdq cmdq[MAX_COMMANDS];
	unsigned int cmdq_wndx;
	unsigned int cmdq_rndx;

	unsigned int last_seqno;
	unsigned int last_rseqno;
	unsigned int last_command_sent;
	unsigned int last_rcommand;
	unsigned int last_rparm1;
	unsigned int seq_num;
	long timeout;

	unsigned int dsp_crashed;
	unsigned int dumping;

	unsigned int ztsnd_rtx;
	unsigned int ztsnd_0010_rtx;

	unsigned char numchannels;
	unsigned char complexname[40];

	unsigned long iobase;
	dma_addr_t 	readdma;
	dma_addr_t	writedma;
	dma_addr_t	descripdma;
	volatile unsigned int *writechunk;					/* Double-word aligned write memory */
	volatile unsigned int *readchunk;					/* Double-word aligned read memory */
	volatile unsigned int *descripchunk;					/* Descriptors */
	
	int wqueints;
	struct workqueue_struct *dte_wq;
	struct work_struct dte_work;

	struct dahdi_transcoder *uencode;
	struct dahdi_transcoder *udecode;
};

struct wcdte_desc {
	char *name;
	int flags;
};

static struct wcdte_desc wctc400p = { "Wildcard TC400P+TC400M", 0 };
static struct wcdte_desc wctce400 = { "Wildcard TCE400+TC400M", 0 };

static struct wcdte *ifaces[WC_MAX_IFACES];



/*
 * The following is the definition of the state structure
 * used by the G.721/G.723 encoder and decoder to preserve their internal
 * state between successive calls.  The meanings of the majority
 * of the state structure fields are explained in detail in the
 * CCITT Recommendation G.721.  The field names are essentially indentical
 * to variable names in the bit level description of the coding algorithm
 * included in this Recommendation.
 */
struct dte_state {
	int encoder;	/* If we're an encoder */
	struct wcdte *wc;

	unsigned int timestamp;
	unsigned int seqno;

	unsigned int cmd_seqno;

	unsigned int timeslot_in_num;		/* DTE chennel on which results we be received from */
	unsigned int timeslot_out_num;		/* DTE channel to send data to */

	unsigned int chan_in_num;		/* DTE chennel on which results we be received from */
	unsigned int chan_out_num;		/* DTE channel to send data to */
	
	unsigned int packets_sent;
	unsigned int packets_received;

	unsigned int last_dte_seqno;
	unsigned int dte_seqno_rcv;

	unsigned char ssrc;
};


static struct dahdi_transcoder *uencode;
static struct dahdi_transcoder *udecode;
static struct dte_state *encoders;
static struct dte_state *decoders;
static int debug = 0;
static int debug_des = 0;			/* Set the number of descriptor packet bytes to output on errors, 0 disables output */
static int debug_des_cnt = 0;			/* Set the number of times descriptor packets are displayed before the output is disabled */
static int force_alert = 0;
static int debug_notimeout = 0;
static char *mode;
static int debug_packets = 0;

static int wcdte_create_channel(struct wcdte *wc, int simple, int complicated, int part1_id, int part2_id, unsigned int *dte_chan1, unsigned int *dte_chan2);
static int wcdte_destroy_channel(struct wcdte *wc, unsigned int chan1, unsigned int chan2);
static int __wcdte_setup_channels(struct wcdte *wc);

static int __dump_descriptors(struct wcdte *wc)
{
	volatile unsigned char *writechunk, *readchunk;
	int o2, i, j;

	if (debug_des_cnt == 0)
		return 1;

	printk("Transmit Descriptors (wc->tdbl = %d)\n", wc->tdbl);
	for (i = 0; i < ERING_SIZE; i++)
	{
		writechunk = (volatile unsigned char *)(wc->writechunk);
		writechunk += i * SFRAME_SIZE;
		o2 = i * 4;

		if (i == wc->tdbl)
			printk("->");
		else
			printk("  ");
		if ((le32_to_cpu(wc->descripchunk[o2]) & 0x80000000))
			printk("AN983 owns : ");
		else
			printk("Driver owns: ");

		for (j = 0; j < debug_des; j++)
			printk("%02X ", writechunk[j]);
		printk("\n");
	}

	printk("Receive Descriptors (wc->rdbl = %d)\n", wc->rdbl);
	for (i = 0; i < ERING_SIZE; i++)
	{
		readchunk = (volatile unsigned char *)wc->readchunk;
		readchunk += i * SFRAME_SIZE;
		o2 = i * 4;
		o2 += ERING_SIZE * 4;

		if (i == wc->rdbl)
			printk("->");
		else
			printk("  ");
		if ((le32_to_cpu(wc->descripchunk[o2]) & 0x80000000))
			printk("AN983 owns : ");
		else
			printk("Driver owns: ");

		for (j = 0; j < debug_des; j++)
			printk("%02X ", readchunk[j]);
		printk("\n");
	}
	if (debug_des_cnt > 0)
		debug_des_cnt--;
	return 0;
}

/* Sanity check values */
static inline int dahdi_tc_sanitycheck(struct dahdi_transcode_header *zth, unsigned int outbytes)
{
	if (zth->dstoffset >= sizeof(zth->dstdata))
		return 0;
	if (zth->dstlen >= sizeof(zth->dstdata))
		return 0;
	if (outbytes >= sizeof(zth->dstdata))
		return 0;
	if ((zth->dstoffset + zth->dstlen + outbytes) >= sizeof(zth->dstdata))
		return 0;
	if (zth->srcoffset >= sizeof(zth->srcdata))
		return 0;
	if (zth->srclen >= sizeof(zth->srcdata))
		return 0;
	if ((zth->srcoffset + zth->srclen) > sizeof(zth->srcdata))
		return 0;
	return 1;
}

static void dte_init_state(struct dte_state *state_ptr, int encoder, unsigned int channel, struct wcdte *wc)
{
	state_ptr->encoder = encoder;
	state_ptr->wc = wc;
	state_ptr->timestamp = 0;
	state_ptr->seqno = 0;

	state_ptr->cmd_seqno = 0;

	state_ptr->packets_sent = 0;
	state_ptr->packets_received = 0;
	state_ptr->last_dte_seqno = 0;
	state_ptr->dte_seqno_rcv = 0;

	state_ptr->chan_in_num = 999;
	state_ptr->chan_out_num = 999;

	state_ptr->ssrc = 0x78;
	
	if (encoder == 1)
	{
		state_ptr->timeslot_in_num = channel * 2;
		state_ptr->timeslot_out_num = channel * 2 + 1;
	} else {
		state_ptr->timeslot_in_num = channel * 2 + 1;
		state_ptr->timeslot_out_num = channel * 2;
	}
}

static unsigned int wcdte_zapfmt_to_dtefmt(unsigned int fmt)
{
	unsigned int pt;
	
	switch(fmt)
	{
		case DAHDI_FORMAT_G723_1:
			pt = DTE_FORMAT_G723_1;
			break;
		case DAHDI_FORMAT_ULAW:
			pt = DTE_FORMAT_ULAW;
			break;
		case DAHDI_FORMAT_ALAW:
			pt = DTE_FORMAT_ALAW;
			break;
		case DAHDI_FORMAT_G729A:
			pt = DTE_FORMAT_G729A;
			break;
		default:
			pt = DTE_FORMAT_UNDEF;
	}

	return(pt);
}

static inline void __wcdte_setctl(struct wcdte *wc, unsigned int addr, unsigned int val)
{
	outl(val, wc->iobase + addr);
}

static inline unsigned int __wcdte_getctl(struct wcdte *wc, unsigned int addr)
{
	return inl(wc->iobase + addr);
}

static inline void wcdte_setctl(struct wcdte *wc, unsigned int addr, unsigned int val)
{
	unsigned long flags;
	spin_lock_irqsave(&wc->reglock, flags);
	__wcdte_setctl(wc, addr, val);
	spin_unlock_irqrestore(&wc->reglock, flags);
}

static inline void wcdte_reinit_descriptor(struct wcdte *wc, int tx, int dbl, char *s)
{
	int o2 = 0;
	o2 += dbl * 4;

	if (!tx)
		o2 += ERING_SIZE * 4;
	wc->descripchunk[o2] = cpu_to_le32(0x80000000);

	wcdte_setctl(wc, 0x0008, 0x00000000);
}

static inline unsigned int wcdte_getctl(struct wcdte *wc, unsigned int addr)
{
	unsigned long flags;
	unsigned int val;
	spin_lock_irqsave(&wc->reglock, flags);
	val = __wcdte_getctl(wc, addr);
	spin_unlock_irqrestore(&wc->reglock, flags);
	return val;
}

static inline int __transmit_demand(struct wcdte *wc)
{
	volatile unsigned char *writechunk;
	int o2,i,j;
	unsigned int reg, xmt_length;

	reg = wcdte_getctl(wc, 0x0028) & 0x00700000;
	
	/* Already transmiting, no need to demand another */
	if (!((reg == 0) || (reg = 6)))
		return(1);

	/* Nothing to transmit */
	if (wc->cmdq_rndx == wc->cmdq_wndx)
		return(1);

	/* Nothing to transmit */
	if (wc->cmdq[wc->cmdq_rndx].cmdlen == 0 )
		return(1);

	writechunk = (volatile unsigned char *)(wc->writechunk);

	writechunk += wc->tdbl * SFRAME_SIZE;

	o2 = wc->tdbl * 4;

	do
	{
	} while ((le32_to_cpu(wc->descripchunk[o2]) & 0x80000000));

	xmt_length = wc->cmdq[wc->cmdq_rndx].cmdlen;
	if (xmt_length < 64)
		xmt_length = 64;
	
	wc->descripchunk[o2+1] = cpu_to_le32((le32_to_cpu(wc->descripchunk[o2+1]) & 0xFBFFF800) | xmt_length);
				
	for(i = 0; i < wc->cmdq[wc->cmdq_rndx].cmdlen; i++)
		writechunk[i] = wc->cmdq[wc->cmdq_rndx].cmd[i];
	for (j = i; j < xmt_length; j++)
		writechunk[j] = 0;

	if (debug_packets && (writechunk[12] == 0x88) && (writechunk[13] == 0x9B))
	{
		printk("wcdte debug: TX: ");
		for (i=0; i<debug_packets; i++)
			printk("%02X ", writechunk[i]);
		printk("\n");
	}

	wc->cmdq[wc->cmdq_rndx].cmdlen = 0;

	wc->descripchunk[o2] = cpu_to_le32(0x80000000);
	wcdte_setctl(wc, 0x0008, 0x00000000);			/* Transmit Poll Demand */
	
	wc->tdbl = (wc->tdbl + 1) % ERING_SIZE;

	wc->cmdq_rndx = (wc->cmdq_rndx + 1) % MAX_COMMANDS;

	return(0);
}

static inline int transmit_demand(struct wcdte *wc)
{
	int val;
	down(&wc->cmdqsem);
	val = __transmit_demand(wc);
	up(&wc->cmdqsem);
	return val;
}

static int dte_operation(struct dahdi_transcoder_channel *ztc, int op)
{
	struct dahdi_transcoder_channel *compl_ztc;
	struct dte_state *st = ztc->pvt, *compl_st;
	struct dahdi_transcode_header *zth = ztc->tch;
	struct wcdte *wc = st->wc;
	unsigned char *chars;
	unsigned int inbytes = 0;
	unsigned int timestamp_inc = 0;
	int i = 0;
	int res = 0;
	unsigned int ipchksum, ndx;
	switch(op) {
	case DAHDI_TCOP_ALLOCATE:
		down(&wc->chansem);
		if (ztc->chan_built == 0)
		{
			if (st->encoder == 1)
				wcdte_create_channel(wc, wcdte_zapfmt_to_dtefmt(zth->srcfmt), wcdte_zapfmt_to_dtefmt(zth->dstfmt),
						st->timeslot_in_num, st->timeslot_out_num, &(st->chan_in_num), &(st->chan_out_num));
			else
				wcdte_create_channel(wc, wcdte_zapfmt_to_dtefmt(zth->dstfmt), wcdte_zapfmt_to_dtefmt(zth->srcfmt),
						st->timeslot_out_num, st->timeslot_in_num, &(st->chan_out_num), &(st->chan_in_num));
			/* Mark this channel as built */
			ztc->chan_built = 1;
			ztc->built_fmts = zth->dstfmt | zth->srcfmt;

			/* Mark the channel complement (other half of encoder/decoder pair) as built */
			ndx = st->timeslot_in_num/2;
			if (st->encoder == 1)
				compl_ztc = &(wc->udecode->channels[ndx]);
			else
				compl_ztc = &(wc->uencode->channels[ndx]);
			compl_ztc->chan_built = 1;
			compl_ztc->built_fmts = zth->dstfmt | zth->srcfmt;
			compl_st = compl_ztc->pvt;
			compl_st->chan_in_num = st->chan_out_num;
			compl_st->chan_out_num = st->chan_in_num;
		}
		up(&wc->chansem);
		break;
	case DAHDI_TCOP_RELEASE:
		down(&wc->chansem);
		ndx = st->timeslot_in_num/2;

		if (st->encoder == 1)
			compl_ztc = &(wc->udecode->channels[ndx]);
		else
			compl_ztc = &(wc->uencode->channels[ndx]);

		/* If the channel complement (other half of the encoder/decoder pair) is not being used... */
		if ((compl_ztc->flags & DAHDI_TC_FLAG_BUSY) == 0)
		{
			if (st->encoder == 1)
				wcdte_destroy_channel(wc, st->chan_in_num, st->chan_out_num);
			else
				wcdte_destroy_channel(wc, st->chan_out_num, st->chan_in_num);

			/* Mark this channel as not built */
			ztc->chan_built = 0;
			ztc->built_fmts = 0;
			st->chan_in_num = 999;
			st->chan_out_num = 999;
			
			/* Mark the channel complement as not built */
			compl_ztc->chan_built = 0;
			compl_ztc->built_fmts = 0;
			compl_st = compl_ztc->pvt;
			compl_st->chan_in_num = 999;
			compl_st->chan_out_num = 999;
		}
		st->dte_seqno_rcv = 0;
		up(&wc->chansem);
		break;
	case DAHDI_TCOP_TRANSCODE:
		if ( (((zth->srcfmt == DAHDI_FORMAT_ULAW) || (zth->srcfmt == DAHDI_FORMAT_ALAW)) && ((zth->dstfmt == DAHDI_FORMAT_G729A  && zth->srclen >= G729_SAMPLES) ||(zth->dstfmt == DAHDI_FORMAT_G723_1  && zth->srclen >= G723_SAMPLES)) )
			|| ((zth->srcfmt == DAHDI_FORMAT_G729A) && (zth->srclen >= G729_BYTES))
			|| ((zth->srcfmt == DAHDI_FORMAT_G723_1) && (zth->srclen >= G723_SID_BYTES)) )
		{
			do
			{
				chars = (unsigned char *)(zth->srcdata + zth->srcoffset);
					
				if ((zth->srcfmt == DAHDI_FORMAT_ULAW) || (zth->srcfmt == DAHDI_FORMAT_ALAW)) {
					if (zth->dstfmt == DAHDI_FORMAT_G729A) {
						inbytes = G729_SAMPLES; 
						timestamp_inc = G729_SAMPLES; 
					} else if (zth->dstfmt == DAHDI_FORMAT_G723_1) {
						inbytes = G723_SAMPLES; 
						timestamp_inc = G723_SAMPLES; 
					}
				} else if (zth->srcfmt == DAHDI_FORMAT_G729A) {
					inbytes = G729_BYTES;
					timestamp_inc = G729_SAMPLES;
				} else if (zth->srcfmt == DAHDI_FORMAT_G723_1) {
					/* determine the size of the frame */
					switch (chars[0] & 0x03) {
					case 0x00:
						inbytes = G723_6K_BYTES;
						break;
					case 0x01:
						inbytes = G723_5K_BYTES;
						break;
					case 0x02:
						inbytes = G723_SID_BYTES;
						break;
					case 0x03:
						/* this is a 'reserved' value in the G.723.1
						   spec and should never occur in real media streams */
						inbytes = G723_SID_BYTES;
						break;
					}
					timestamp_inc = G723_SAMPLES;
				}

				zth->srclen -= inbytes;

				{
					unsigned char fifo[OTHER_CMD_LEN] = CMD_MSG_IP_UDP_RTP(
						 ((inbytes+40) >> 8)                 & 0xFF,
						  (inbytes+40)                       & 0xFF,
						   st->seqno                         & 0xFF,
						   0x00,
						   0x00,
						(((st->timeslot_out_num) >> 8)+0x50) & 0xFF,
						  (st->timeslot_out_num)             & 0xFF,
						(((st->timeslot_in_num) >> 8)+0x50)  & 0xFF,
						  (st->timeslot_in_num)              & 0xFF,
						 ((inbytes+20) >> 8)                 & 0xFF,
						  (inbytes+20)                       & 0xFF,
						   0x00,
						   0x00,
						   wcdte_zapfmt_to_dtefmt(zth->srcfmt),					 
						 ((st->seqno) >> 8)                  & 0xFF,
						  (st->seqno)                        & 0xFF,
						 ((st->timestamp) >> 24)             & 0xFF,
						 ((st->timestamp) >> 16)             & 0xFF,
	 					 ((st->timestamp) >> 8)              & 0xFF,
						  (st->timestamp)                    & 0xFF,
						  (st->ssrc)                         & 0xFF);

					ipchksum = 0x9869 + (fifo[16] << 8) + fifo[17]
						+ (fifo[18] << 8) + fifo[19];
					while (ipchksum >> 16)
						ipchksum = (ipchksum & 0xFFFF) + (ipchksum >> 16);
					ipchksum = (~ipchksum) & 0xFFFF;

					fifo[24] = ipchksum >> 8;
					fifo[25] = ipchksum & 0xFF;

					st->seqno += 1;
					st->timestamp += timestamp_inc;

					for (i = 0; i < inbytes; i++)
						fifo[i+CMD_MSG_IP_UDP_RTP_LEN]= chars[i];

					down(&wc->cmdqsem);
		
					if ( (((wc->cmdq_wndx + 1) % MAX_COMMANDS) == wc->cmdq_rndx) && debug )
						printk("wcdte error: cmdq is full.\n");
					else
					{
						wc->cmdq[wc->cmdq_wndx].cmdlen = CMD_MSG_IP_UDP_RTP_LEN+inbytes;
						for (i = 0; i < CMD_MSG_IP_UDP_RTP_LEN+inbytes; i++)
							wc->cmdq[wc->cmdq_wndx].cmd[i] = fifo[i];
						wc->cmdq_wndx = (wc->cmdq_wndx + 1) % MAX_COMMANDS;
					}
					
					__transmit_demand(wc);
					up(&wc->cmdqsem);
				}
				st->packets_sent++;



				zth->srcoffset += inbytes;


			} while ((((zth->srcfmt == DAHDI_FORMAT_ULAW) || (zth->srcfmt == DAHDI_FORMAT_ALAW)) && ((zth->dstfmt == DAHDI_FORMAT_G729A  && zth->srclen >= G729_SAMPLES) ||(zth->dstfmt == DAHDI_FORMAT_G723_1  && zth->srclen >= G723_SAMPLES)) )
				|| ((zth->srcfmt == DAHDI_FORMAT_G729A) && (zth->srclen >= G729_BYTES))
				|| ((zth->srcfmt == DAHDI_FORMAT_G723_1) && (zth->srclen >= G723_SID_BYTES)) );

		} else {
			dahdi_transcoder_alert(ztc);
		}

		res = 0;
		break;
	}
	return res;
}

static void wcdte_stop_dma(struct wcdte *wc);

static inline void wcdte_receiveprep(struct wcdte *wc, int dbl)
{
	volatile unsigned char *readchunk;
	struct dahdi_transcoder_channel *ztc = NULL;
	struct dahdi_transcode_header *zth = NULL;
	struct dte_state *st = NULL;
	int o2,i;
	unsigned char rseq, rcodec;
	unsigned int rcommand, rchannel, rlen, rtp_rseq, rtp_eseq;
	unsigned char *chars = NULL;
	unsigned int ztc_ndx;

	readchunk = (volatile unsigned char *)wc->readchunk;
	readchunk += dbl * SFRAME_SIZE;

	o2 = dbl * 4;
	o2 += ERING_SIZE * 4;
	
	/* Control in packet */
	if ((readchunk[12] == 0x88) && (readchunk[13] == 0x9B))
	{
		if (debug_packets)
		{
			printk("wcdte debug: RX: ");
			for (i=0; i<debug_packets; i++)
				printk("%02X ", readchunk[i]);
			printk("\n");
		}
		/* See if message must be ACK'd */
		if ((readchunk[17] & 0x80) == 0)
		{
			rcommand = readchunk[24] | (readchunk[25] << 8);
			rchannel = readchunk[18] | (readchunk[19] << 8);
			rseq = readchunk[16];

			down(&wc->cmdqsem);
			if ((readchunk[17] & 0x40) == 0) {
				if ( (((wc->cmdq_wndx + 1) % MAX_COMMANDS) == wc->cmdq_rndx) && debug )
					printk("wcdte error: cmdq is full (rndx = %d, wndx = %d).\n", wc->cmdq_rndx, wc->cmdq_wndx);
				else
				{
					unsigned char fifo[OTHER_CMD_LEN] = CMD_MSG_ACK(rseq++, rchannel);

					wc->cmdq[wc->cmdq_wndx].cmdlen = CMD_MSG_ACK_LEN;
					for (i = 0; i < wc->cmdq[wc->cmdq_wndx].cmdlen; i++)
						wc->cmdq[wc->cmdq_wndx].cmd[i] = fifo[i];
					wc->cmdq_wndx = (wc->cmdq_wndx + 1) % MAX_COMMANDS;
				}

				__transmit_demand(wc);
			}
		
			wc->rcvflags = RCV_CSMENCAPS;
			if (rcommand == wc->last_command_sent) {
				wc->last_rcommand = rcommand;
				wc->last_rparm1 = readchunk[28] | (readchunk[29] << 8);
				wake_up(&wc->regq);
			} else {
				if (debug)
				printk("wcdte error: unexpected command response received (sent: %04X, received: %04X)\n", wc->last_command_sent, rcommand);
			}
			up(&wc->cmdqsem);
		}
		else
		{
			wc->last_rseqno = readchunk[16];
			wc->rcvflags = RCV_CSMENCAPS_ACK;
			if (!wc->dumping)
				wake_up_interruptible(&wc->regq);
			else
				wake_up(&wc->regq);
		}

		if ((readchunk[22] == 0x75) && (readchunk[23] = 0xC1))
		{
			if (debug)
				printk("wcdte error: received alert (0x%02X%02X) from dsp\n", readchunk[29], readchunk[28]);
			if (debug_des) {
				down(&wc->cmdqsem);
				__dump_descriptors(wc);
				up(&wc->cmdqsem);
			}
		}

		if (wc->dumping && (readchunk[22] == 0x04) && (readchunk[23] = 0x14)) {
			for (i = 27; i < 227; i++)
				printk("%02X ", readchunk[i]);
			printk("\n");
		}
	}

	/* IP/UDP in packet */
	else if ((readchunk[12] == 0x08) && (readchunk[13] == 0x00)
		&& (readchunk[50] == 0x12) && (readchunk[51] == 0x34) && (readchunk[52] = 0x56) && (readchunk[53] == 0x78))
	{
		rchannel = (readchunk[37] | (readchunk[36] << 8)) - 0x5000;
		rlen = (readchunk[39] | (readchunk[38] << 8)) - 20;
		rtp_rseq = (readchunk[45] | (readchunk[44] << 8));
		rcodec = readchunk[43];

		ztc_ndx = rchannel/2;

		if (ztc_ndx >= wc->numchannels)
		{
			if (debug)
				printk("wcdte error: Invalid channel number received (ztc_ndx = %d) (numchannels = %d)\n", ztc_ndx, wc->numchannels);
			rcodec = DTE_FORMAT_UNDEF;
		}

		if ((rcodec == 0x00) || (rcodec == 0x08))	/* ulaw or alaw (decoders) */
		{
			ztc = &(wc->udecode->channels[ztc_ndx]);
			zth = ztc->tch;
			st = ztc->pvt;

			if (zth == NULL)
			{
				if (debug)
					printk("wcdte error: Tried to put DTE data into a freed zth header! (ztc_ndx = %d, ztc->chan_built = %d)\n", ztc_ndx, ztc->chan_built);
				if (debug_des) {
					down(&wc->cmdqsem);
					__dump_descriptors(wc);
					up(&wc->cmdqsem);
				}
				rcodec = DTE_FORMAT_UNDEF;
			} else {
				chars = (unsigned char *)(zth->dstdata + zth->dstoffset + zth->dstlen);
				st->packets_received++;
			}

		}
		
		if ((rcodec == 0x04) || (rcodec == 0x12))	/* g.723 or g.729 (encoders) */
		{
			ztc = &(wc->uencode->channels[ztc_ndx]);
			zth = ztc->tch;
			st = ztc->pvt;

			if (zth == NULL)
			{
				if (debug)
					printk("wcdte error: Tried to put DTE data into a freed zth header! (ztc_ndx = %d, ztc->chan_built = %d)\n", ztc_ndx, ztc->chan_built);
				if (debug_des) {
					down(&wc->cmdqsem);
					__dump_descriptors(wc);
					up(&wc->cmdqsem);
				}
				rcodec = DTE_FORMAT_UNDEF;
			} else {
				chars = (unsigned char *)(zth->dstdata + zth->dstoffset + zth->dstlen);
				st->packets_received++;
			}

		}

		if (st->dte_seqno_rcv == 0)
		{
			st->dte_seqno_rcv = 1;
			st->last_dte_seqno = rtp_rseq;
		} else {
			rtp_eseq = (st->last_dte_seqno + 1) & 0xFFFF;
			if ( (rtp_rseq != rtp_eseq) && debug )
				printk("wcdte error: Bad seqno from DTE! [%04X][%d][%d][%d]\n", (readchunk[37] | (readchunk[36] << 8)), rchannel, rtp_rseq, st->last_dte_seqno);

			st->last_dte_seqno = rtp_rseq;
		}

		if (rcodec == 0x00)	/* ulaw */
		{
			if (dahdi_tc_sanitycheck(zth, rlen) && ((zth->srcfmt == DAHDI_FORMAT_G729A && rlen == G729_SAMPLES) || (zth->srcfmt == DAHDI_FORMAT_G723_1 && rlen == G723_SAMPLES))) {
				for (i = 0; i < rlen; i++)
					chars[i] = readchunk[i+54];

				zth->dstlen += rlen;
				zth->dstsamples = zth->dstlen;

			} else {
				ztc->errorstatus = -EOVERFLOW;
			}
			dahdi_transcoder_alert(ztc);
		}
		else if (rcodec == 0x08)	/* alaw */
		{
			if (dahdi_tc_sanitycheck(zth, rlen) && ((zth->srcfmt == DAHDI_FORMAT_G729A && rlen == G729_SAMPLES) || (zth->srcfmt == DAHDI_FORMAT_G723_1 && rlen == G723_SAMPLES))) {

				for (i = 0; i < rlen; i++)
					chars[i] = readchunk[i+54];

				zth->dstlen += rlen;
				zth->dstsamples = zth->dstlen;

			} else {
				ztc->errorstatus = -EOVERFLOW;
			}
			dahdi_transcoder_alert(ztc);
		}
		else if (rcodec == 0x04)	/* G.723.1 */
		{
			if (dahdi_tc_sanitycheck(zth, rlen) &&
			    ((rlen == G723_6K_BYTES) || (rlen == G723_5K_BYTES) || (rlen == G723_SID_BYTES)))
			{
				for (i = 0; i < rlen; i++)
					chars[i] = readchunk[i+54];

				zth->dstlen += rlen;
				zth->dstsamples += G723_SAMPLES;

			} else {
				ztc->errorstatus = -EOVERFLOW;
			}

			if (!(zth->dstsamples % G723_SAMPLES))
			{
				dahdi_transcoder_alert(ztc);
			} 
		}
		else if (rcodec == 0x12)	/* G.729a */
		{
			if (dahdi_tc_sanitycheck(zth, rlen) && (rlen == G729_BYTES))
			{
				for (i = 0; i < rlen; i++)
					chars[i] = readchunk[i+54];

				zth->dstlen += rlen;
				zth->dstsamples = zth->dstlen * 8;

			} else {
				ztc->errorstatus = -EOVERFLOW;
			}

			if (!(zth->dstsamples % G729_SAMPLES))
			{
				dahdi_transcoder_alert(ztc);
			} 
		}
	}
}





/* static inline int wcdte_check_descriptor(struct wcdte *wc) */
static int wcdte_check_descriptor(struct wcdte *wc)
{
	int o2 = 0;

	o2 += ERING_SIZE * 4;
	o2 += wc->rdbl * 4;

	if (!(le32_to_cpu(wc->descripchunk[o2]) & 0x80000000)) {
		wc->rxints++;
		wcdte_receiveprep(wc, wc->rdbl);
		wcdte_reinit_descriptor(wc, 0, wc->rdbl, "rxchk");
		wc->rdbl = (wc->rdbl + 1) % ERING_SIZE;

		return 1;
	}
	return 0;
}

static void wcdte_init_descriptors(struct wcdte *wc)
{
	volatile unsigned int *descrip;
	dma_addr_t descripdma;
	dma_addr_t writedma;
	dma_addr_t readdma;
	int x;
	
	descrip = wc->descripchunk;
	descripdma = wc->descripdma;
	writedma = wc->writedma;
	readdma = wc->readdma;

	for (x=0;x<ERING_SIZE;x++) {
		if (x < ERING_SIZE - 1)
			descripdma += 16;
		else
			descripdma = wc->descripdma;

		/* Transmit descriptor */
		descrip[0 ] = cpu_to_le32(0x00000000);
		descrip[1 ] = cpu_to_le32(0xe5800000 | (SFRAME_SIZE));
		descrip[2 ] = cpu_to_le32(writedma + x*SFRAME_SIZE);
		descrip[3 ] = cpu_to_le32(descripdma);

		/* Receive descriptor */
		descrip[0 + ERING_SIZE * 4] = cpu_to_le32(0x80000000);
		descrip[1 + ERING_SIZE * 4] = cpu_to_le32(0x01000000 | (SFRAME_SIZE));
		descrip[2 + ERING_SIZE * 4] = cpu_to_le32(readdma + x*SFRAME_SIZE);
		descrip[3 + ERING_SIZE * 4] = cpu_to_le32(descripdma + ERING_SIZE * 16);
	
		/* Advance descriptor */
		descrip += 4;
	}	
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
static void dte_wque_run(struct work_struct *work)
{
	struct wcdte *wc = container_of(work, struct wcdte, dte_work);
#else
static void dte_wque_run(void *work_data)
{
	struct wcdte *wc = work_data;
#endif
	int res;

	do {
		res = wcdte_check_descriptor(wc);
	} while(res);
	
	transmit_demand(wc);
}

DAHDI_IRQ_HANDLER(wcdte_interrupt)
{
	struct wcdte *wc = dev_id;
	unsigned int ints;

	/* Read and clear interrupts */
	ints = wcdte_getctl(wc, 0x0028);
	wcdte_setctl(wc, 0x0028, ints);

	if (!ints)
		return IRQ_NONE;
	ints &= wc->intmask;

	if (ints & 0x00000041) {
		wc->wqueints = ints;
		queue_work(wc->dte_wq, &wc->dte_work);
	}
		
	if ((ints & 0x00008000) && debug)
		printk("wcdte: Abnormal Interrupt: ");

	if ((ints & 0x00002000) && debug)
		printk("wcdte: Fatal Bus Error INT\n");

	if ((ints & 0x00000100) && debug)
		printk("wcdte: Receive Stopped INT\n");

	if ((ints & 0x00000080) && debug)
		printk("wcdte: Receive Desciptor Unavailable INT\n");

	if ((ints & 0x00000020) && debug)
		printk("wcdte: Transmit Under-flow INT\n");

	if ((ints & 0x00000008) && debug)
		printk("wcdte: Jabber Timer Time-out INT\n");

	if ((ints & 0x00000004) && debug)
		printk("wcdte: Transmit Descriptor Unavailable INT\n");

	if ((ints & 0x00000002) && debug)
		printk("wcdte: Transmit Processor Stopped INT\n");

	return IRQ_RETVAL(1);
	
}

static int wcdte_hardware_init(struct wcdte *wc)
{
	/* Hardware stuff */
	unsigned int reg;
	unsigned long newjiffies;

	/* Initialize descriptors */
	wcdte_init_descriptors(wc);
	
	/* Enable I/O Access */
	pci_read_config_dword(wc->dev, 0x0004, &reg);
	reg |= 0x00000007;
	pci_write_config_dword(wc->dev, 0x0004, reg);

	wcdte_setctl(wc, 0x0000, 0xFFF88001);

	newjiffies = jiffies + HZ/10;
	while(((reg = wcdte_getctl(wc,0x0000)) & 0x00000001) && (newjiffies > jiffies));

	wcdte_setctl(wc, 0x0000, 0xFFFA0000);
	
	/* Configure watchdogs, access, etc */
	wcdte_setctl(wc, 0x0030, 0x00280048);
	wcdte_setctl(wc, 0x0078, 0x00000013 /* | (1 << 28) */);

	reg = wcdte_getctl(wc, 0x00fc);
	wcdte_setctl(wc, 0x00fc, (reg & ~0x7) | 0x7);

	reg = wcdte_getctl(wc, 0x00fc);

	return 0;
}

static void wcdte_setintmask(struct wcdte *wc, unsigned int intmask)
{
	wc->intmask = intmask;
	wcdte_setctl(wc, 0x0038, intmask);
}

static void wcdte_enable_interrupts(struct wcdte *wc)
{
	/* Enable interrupts */
	if (!debug)
		wcdte_setintmask(wc, 0x00010041);
	else
		wcdte_setintmask(wc, 0x0001A1EB);
}

static void wcdte_start_dma(struct wcdte *wc)
{
	unsigned int reg;
	wmb();
	wcdte_setctl(wc, 0x0020, wc->descripdma);
	wcdte_setctl(wc, 0x0018, wc->descripdma + (16 * ERING_SIZE));
	/* Start receiver/transmitter */
	reg = wcdte_getctl(wc, 0x0030);
	wcdte_setctl(wc, 0x0030, reg | 0x00002002);		/* Start XMT and RCD */
	wcdte_setctl(wc, 0x0010, 0x00000000);			/* Receive Poll Demand */
	reg = wcdte_getctl(wc, 0x0028);
	wcdte_setctl(wc, 0x0028, reg);

}

static void wcdte_stop_dma(struct wcdte *wc)
{
	/* Disable interrupts and reset */
	unsigned int reg;
	/* Disable interrupts */
	wcdte_setintmask(wc, 0x00000000);
	wcdte_setctl(wc, 0x0084, 0x00000000);
	wcdte_setctl(wc, 0x0048, 0x00000000);
	/* Reset the part to be on the safe side */
	reg = wcdte_getctl(wc, 0x0000);
	reg |= 0x00000001;
	wcdte_setctl(wc, 0x0000, reg);
}

static void wcdte_disable_interrupts(struct wcdte *wc)	
{
	/* Disable interrupts */
	wcdte_setintmask(wc, 0x00000000);
	wcdte_setctl(wc, 0x0084, 0x00000000);
}

static int wcdte_waitfor_csmencaps(struct wcdte *wc, unsigned int mask, int wait_mode)
{
	int ret;


	if (wait_mode == 1)
		ret = wait_event_interruptible_timeout(wc->regq, (wc->rcvflags == mask), wc->timeout);
	else if (wait_mode == 2)
		ret = wait_event_timeout(wc->regq, (wc->rcvflags == mask), wc->timeout);
	else {
		if (!debug_notimeout) {
			ret = wait_event_timeout(wc->regq, ((wc->last_rcommand == wc->last_command_sent) && (wc->last_seqno == wc->last_rseqno) && (wc->rcvflags == mask)), wc->timeout);
		}
		else {
			ret = wait_event_interruptible(wc->regq, ((wc->last_rcommand == wc->last_command_sent) && (wc->last_seqno == wc->last_rseqno) && (wc->rcvflags == mask)));
			if (ret == 0)
				ret = 1;
		}
	}
	wc->rcvflags = 0;
	wc->last_rcommand = 0;
	wc->last_seqno = 0;

	if (ret < 0)
	{
		if (debug)
			printk("wcdte error: Wait interrupted, need to stop boot (ret = %d)\n", ret);
		return(1);
	}
	if (ret == 0)
	{
		if (debug)
			printk("wcdte error: Waitfor CSMENCAPS response timed out (ret = %d) (cmd_snt = %04X)\n", ret, wc->last_command_sent);
		if (debug_des) {
			down(&wc->cmdqsem);
			__dump_descriptors(wc);
			up(&wc->cmdqsem);
		}
		return(2);
	}
	if (wait_mode == 0)
		wc->last_command_sent = 999;
	wc->last_rseqno = 999;
	return(0);
}


static int wcdte_read_phy(struct wcdte *wc, int location)
{
	int i;
	long mdio_addr = 0x0048;
	int read_cmd = (0xf6 << 10) | (1 << 5) | location;
	int retval = 0;

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1);
		wcdte_getctl(wc, mdio_addr);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}
	/* Shift the read command bits out. */
	for (i = 17; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		wcdte_setctl(wc, mdio_addr, MDIO_ENB | dataval);
		wcdte_getctl(wc, mdio_addr);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | dataval | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}

	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		wcdte_setctl(wc, mdio_addr, MDIO_ENB_IN);
		wcdte_getctl(wc, mdio_addr);
		retval = (retval << 1) | ((wcdte_getctl(wc, mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB_IN | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}
	retval = (retval>>1) & 0xffff;
	return retval;
}

void wcdte_write_phy(struct wcdte *wc, int location, int value)
{
	int i;
	int cmd = (0x5002 << 16) | (1 << 23) | (location<<18) | value;
	long mdio_addr = 0x0048;

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1);
		wcdte_getctl(wc, mdio_addr);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | dataval);
		wcdte_getctl(wc, mdio_addr);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB | dataval | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		wcdte_setctl(wc, mdio_addr, MDIO_ENB_IN);
		wcdte_getctl(wc, mdio_addr);
		wcdte_setctl(wc, mdio_addr, MDIO_ENB_IN | MDIO_SHIFT_CLK);
		wcdte_getctl(wc, mdio_addr);
	}
	return;
}

static int wcdte_boot_processor(struct wcdte *wc, const struct firmware *firmware, int full)
{
	int i, j, byteloc, last_byteloc, length, delay_count;
	unsigned int reg, ret;

#ifndef USE_TEST_HW
	/* Turn off auto negotiation */
	wcdte_write_phy(wc, 0, 0x2100);
	if (debug)
		printk("wcdte: PHY register 0 = %X", wcdte_read_phy(wc, 0));

	/* Set reset */
	wcdte_setctl(wc, 0x00A0, 0x04000000);

	/* Wait 1000msec to ensure processor reset */
	mdelay(4);

	/* Clear reset */
	wcdte_setctl(wc, 0x00A0, 0x04080000);

	/* Waitfor ethernet link */
	delay_count = 0;
	do
	{
		reg = wcdte_getctl(wc, 0x00fc);
		mdelay(2);
		delay_count++;

		if (delay_count >= 5000)
		{
			printk("wcdte error: Failed to link to DTE processor!\n");
			return(1);
		}
	} while ((reg & 0xE0000000) != 0xE0000000);


	/* Turn off booted LED */
	wcdte_setctl(wc, 0x00A0, 0x04084000);

	
#endif

	reg = wcdte_getctl(wc, 0x00fc);
	if (debug)
		printk("wcdte: LINK STATUS: reg(0xfc) = %X\n", reg);

	reg = wcdte_getctl(wc, 0x00A0);

	byteloc = 17;
	j = 0;
	do
	{
		last_byteloc = byteloc;

		length = (firmware->data[byteloc] << 8) |firmware->data[byteloc+1];
		byteloc += 2;
		
		down(&wc->cmdqsem);
		if ( (((wc->cmdq_wndx + 1) % MAX_COMMANDS) == wc->cmdq_rndx) && debug )
			printk("wcdte error: cmdq is full.\n");
		else
		{
			wc->cmdq[wc->cmdq_wndx].cmdlen = length;
			for (i = 0; i < length; i++)
				wc->cmdq[wc->cmdq_wndx].cmd[i] = firmware->data[byteloc++];
			wc->cmdq_wndx = (wc->cmdq_wndx + 1) % MAX_COMMANDS;
		}
	
		ret = __transmit_demand(wc);
  		up(&wc->cmdqsem);

		ret = wcdte_waitfor_csmencaps(wc, RCV_CSMENCAPS_ACK, 1);
		if (ret == 1)
			return(1);
		else if (ret == 2)		/* Retransmit if dte processor times out */
			byteloc = last_byteloc;
		j++;

		if (!full && (byteloc > 189)) { /* Quit if not fully booting */
			wcdte_setctl(wc, 0x00A0, 0x04080000);
			return 0;
		}


	} while (byteloc < firmware->size-20);
	wc->timeout = 10 * HZ;
	wc->last_command_sent = 0;
	if (wcdte_waitfor_csmencaps(wc, RCV_CSMENCAPS, 1))
		return(1);
	
	/* Turn on booted LED */
	wcdte_setctl(wc, 0x00A0, 0x04080000);
	if(debug)
		printk("wcdte: Successfully booted DTE processor.\n");

	return(0);
}

static int wcdte_create_channel(struct wcdte *wc, int simple, int complicated, int part1_id, int part2_id, unsigned int *dte_chan1, unsigned int *dte_chan2)
{
	int length = 0;
	unsigned char chan1, chan2;
	struct dahdi_transcoder_channel *ztc1, *ztc2;
	struct dte_state *st1, *st2;
	if(complicated == DTE_FORMAT_G729A)
		length = G729_LENGTH;
	else if (complicated == DTE_FORMAT_G723_1)
		length = G723_LENGTH;

	/* Create complex channel */
	dahdi_send_cmd(wc, CMD_MSG_CREATE_CHANNEL(wc->seq_num++, part1_id), CMD_MSG_CREATE_CHANNEL_LEN, 0x0010);
	dahdi_send_cmd(wc, CMD_MSG_QUERY_CHANNEL(wc->seq_num++, part1_id), CMD_MSG_QUERY_CHANNEL_LEN, 0x0010);
	chan1 = wc->last_rparm1;

	/* Create simple channel */
	dahdi_send_cmd(wc, CMD_MSG_CREATE_CHANNEL(wc->seq_num++, part2_id), CMD_MSG_CREATE_CHANNEL_LEN, 0x0010);
	dahdi_send_cmd(wc, CMD_MSG_QUERY_CHANNEL(wc->seq_num++, part2_id), CMD_MSG_QUERY_CHANNEL_LEN, 0x0010);
	chan2 = wc->last_rparm1;

	ztc1 = &(wc->uencode->channels[part1_id/2]);
	ztc2 = &(wc->udecode->channels[part2_id/2]);
	st1 = ztc1->pvt;
	st2 = ztc2->pvt;

	/* Configure complex channel */
	dahdi_send_cmd(wc, CMD_MSG_SET_IP_HDR_CHANNEL(st1->cmd_seqno++, chan1, part2_id, part1_id), CMD_MSG_SET_IP_HDR_CHANNEL_LEN, 0x9000);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VCEOPT(st1->cmd_seqno++, chan1, length, 0), CMD_MSG_VOIP_VCEOPT_LEN, 0x8001);

	/* Configure simple channel */
	dahdi_send_cmd(wc, CMD_MSG_SET_IP_HDR_CHANNEL(st2->cmd_seqno++, chan2, part1_id, part2_id), CMD_MSG_SET_IP_HDR_CHANNEL_LEN, 0x9000);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VCEOPT(st2->cmd_seqno++, chan2, length, 0), CMD_MSG_VOIP_VCEOPT_LEN, 0x8001);

#ifdef QUIET_DSP
	dahdi_send_cmd(wc, CMD_MSG_VOIP_TONECTL(st1->cmd_seqno++, chan1), CMD_MSG_VOIP_TONECTL_LEN, 0x805B);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_DTMFOPT(st1->cmd_seqno++, chan1), CMD_MSG_VOIP_DTMFOPT_LEN, 0x8002);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_TONECTL(st2->cmd_seqno++, chan2), CMD_MSG_VOIP_TONECTL_LEN, 0x805B);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_DTMFOPT(st2->cmd_seqno++, chan2), CMD_MSG_VOIP_DTMFOPT_LEN, 0x8002);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_INDCTRL(st1->cmd_seqno++, chan1), CMD_MSG_VOIP_INDCTRL_LEN, 0x8084);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_INDCTRL(st2->cmd_seqno++, chan2), CMD_MSG_VOIP_INDCTRL_LEN, 0x8084);
#endif

	dahdi_send_cmd(wc, CMD_MSG_TRANS_CONNECT(wc->seq_num++, 1, chan1, chan2, complicated, simple), CMD_MSG_TRANS_CONNECT_LEN, 0x9322);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VOPENA(st1->cmd_seqno++, chan1, complicated), CMD_MSG_VOIP_VOPENA_LEN, 0x8000);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VOPENA(st2->cmd_seqno++, chan2, simple), CMD_MSG_VOIP_VOPENA_LEN, 0x8000);

	*dte_chan1 = chan1;
	*dte_chan2 = chan2;

	return 1;
}

static int wcdte_destroy_channel(struct wcdte *wc, unsigned int chan1, unsigned int chan2)
{
	struct dahdi_transcoder_channel *ztc1, *ztc2;
	struct dte_state *st1, *st2;

	ztc1 = &(wc->uencode->channels[chan1/2]);
	ztc2 = &(wc->udecode->channels[chan2/2]);
	st1 = ztc1->pvt;
	st2 = ztc2->pvt;

	/* Turn off both channels */
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VOPENA_CLOSE(st1->cmd_seqno++, chan1), CMD_MSG_VOIP_VOPENA_CLOSE_LEN, 0x8000);
	dahdi_send_cmd(wc, CMD_MSG_VOIP_VOPENA_CLOSE(st2->cmd_seqno++, chan2), CMD_MSG_VOIP_VOPENA_CLOSE_LEN, 0x8000);
	
	/* Disconnect the channels */
	dahdi_send_cmd(wc, CMD_MSG_TRANS_CONNECT(wc->seq_num++, 0, chan1, chan2, 0, 0), CMD_MSG_TRANS_CONNECT_LEN, 0x9322);

	/* Remove the channels */
	dahdi_send_cmd(wc, CMD_MSG_DESTROY_CHANNEL(wc->seq_num++, chan1), CMD_MSG_DESTROY_CHANNEL_LEN, 0x0011);
	dahdi_send_cmd(wc, CMD_MSG_DESTROY_CHANNEL(wc->seq_num++, chan2), CMD_MSG_DESTROY_CHANNEL_LEN, 0x0011);

	return 1;
}


static int __wcdte_setup_channels(struct wcdte *wc)
{
	wc->seq_num = 6;

#ifndef USE_TEST_HW
	dahdi_send_cmd(wc, CMD_MSG_SET_ARM_CLK(wc->seq_num++), CMD_MSG_SET_ARM_CLK_LEN, 0x0411);
	dahdi_send_cmd(wc, CMD_MSG_SET_SPU_CLK(wc->seq_num++), CMD_MSG_SET_SPU_CLK_LEN, 0x0412);
#endif

#ifdef USE_TDM_CONFIG
	dahdi_send_cmd(wc, CMD_MSG_TDM_SELECT_BUS_MODE(wc->seq_num++), CMD_MSG_TDM_SELECT_BUS_MODE_LEN, 0x0417);
	dahdi_send_cmd(wc, CMD_MSG_TDM_ENABLE_BUS(wc->seq_num++), CMD_MSG_TDM_ENABLE_BUS_LEN, 0x0405);
	dahdi_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x03, 0x20, 0x00), CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN, 0x0407);
	dahdi_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x04, 0x80, 0x04), CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN, 0x0407);
	dahdi_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x05, 0x20, 0x08), CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN, 0x0407);
	dahdi_send_cmd(wc, CMD_MSG_SUPVSR_SETUP_TDM_PARMS(wc->seq_num++, 0x06, 0x80, 0x0C), CMD_MSG_SUPVSR_SETUP_TDM_PARMS_LEN, 0x0407);
#endif

	dahdi_send_cmd(wc, CMD_MSG_SET_ETH_HEADER(wc->seq_num++), CMD_MSG_SET_ETH_HEADER_LEN, 0x0100);
	dahdi_send_cmd(wc, CMD_MSG_IP_SERVICE_CONFIG(wc->seq_num++), CMD_MSG_IP_SERVICE_CONFIG_LEN, 0x0302);
	dahdi_send_cmd(wc, CMD_MSG_ARP_SERVICE_CONFIG(wc->seq_num++), CMD_MSG_ARP_SERVICE_CONFIG_LEN, 0x0105);
	dahdi_send_cmd(wc, CMD_MSG_ICMP_SERVICE_CONFIG(wc->seq_num++), CMD_MSG_ICMP_SERVICE_CONFIG_LEN, 0x0304);

#ifdef USE_TDM_CONFIG
	dahdi_send_cmd(wc, CMD_MSG_DEVICE_SET_COUNTRY_CODE(wc->seq_num++), CMD_MSG_DEVICE_SET_COUNTRY_CODE_LEN, 0x041B);
#endif

	dahdi_send_cmd(wc, CMD_MSG_SPU_FEATURES_CONTROL(wc->seq_num++, 0x02), CMD_MSG_SPU_FEATURES_CONTROL_LEN, 0x0013);
	dahdi_send_cmd(wc, CMD_MSG_IP_OPTIONS(wc->seq_num++), CMD_MSG_IP_OPTIONS_LEN, 0x0306);
	dahdi_send_cmd(wc, CMD_MSG_SPU_FEATURES_CONTROL(wc->seq_num++, 0x04), CMD_MSG_SPU_FEATURES_CONTROL_LEN, 0x0013);

#ifdef USE_TDM_CONFIG
	dahdi_send_cmd(wc, CMD_MSG_TDM_OPT(wc->seq_num++), CMD_MSG_TDM_OPT_LEN, 0x0435);
#endif

	wc->timeout = HZ/10 + 1; 	/* 100msec */

	return(0);
}

static int wcdte_setup_channels(struct wcdte *wc)
{
	down(&wc->chansem);
	__wcdte_setup_channels(wc);
	up(&wc->chansem);

	return 0;
}

static int __devinit wcdte_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int res, reg;
	struct wcdte *wc;
	struct wcdte_desc *d = (struct wcdte_desc *)ent->driver_data;
	int x;
	static int initd_ifaces=0;
	unsigned char g729_numchannels, g723_numchannels, min_numchannels, dte_firmware_ver, dte_firmware_ver_minor;
	unsigned int complexfmts;

	struct firmware embedded_firmware;
	const struct firmware *firmware = &embedded_firmware;
#if !defined(HOTPLUG_FIRMWARE)
	extern void _binary_dahdi_fw_tc400m_bin_size;
	extern u8 _binary_dahdi_fw_tc400m_bin_start[];
#else
	static const char tc400m_firmware[] = "dahdi-fw-tc400m.bin";
#endif

	if (!initd_ifaces) {
		memset((void *)ifaces,0,(sizeof(struct wcdte *))*WC_MAX_IFACES);
		initd_ifaces=1;
	}
	for (x=0;x<WC_MAX_IFACES;x++)
		if (!ifaces[x]) break;
	if (x >= WC_MAX_IFACES) {
		printk("wcdte: Too many interfaces\n");
		return -EIO;
	}

	if (pci_enable_device(pdev)) {
		res = -EIO;
	} else {

		wc = vmalloc(sizeof(struct wcdte));
		if (wc) {
			ifaces[x] = wc;
			memset(wc, 0, sizeof(struct wcdte));
			spin_lock_init(&wc->reglock);
			sema_init(&wc->chansem, 1);
			sema_init(&wc->cmdqsem, 1);
			wc->cards = NUM_CARDS;
			wc->iobase = pci_resource_start(pdev, 0);
			wc->dev = pdev;
			wc->pos = x;
			wc->variety = d->name;

			wc->tdbl = 0;
			wc->rdbl = 0;
			wc->rcvflags = 0;
			wc->last_seqno = 999;
			wc->last_command_sent = 0;
			wc->last_rcommand = 0;
			wc->last_rparm1 = 0;
			wc->cmdq_wndx = 0;
			wc->cmdq_rndx = 0;
			wc->seq_num = 6;
			wc->timeout = 1 * HZ;		/* 1 sec */
			wc->dsp_crashed = 0;
			wc->dumping = 0;
			wc->ztsnd_rtx = 0;
			wc->ztsnd_0010_rtx = 0;
			
			/* Keep track of whether we need to free the region */
			if (request_region(wc->iobase, 0xff, "wcdte")) 
				wc->freeregion = 1;

			/* Allocate enought memory for all TX buffers, RX buffers, and descriptors */
			wc->writechunk = (int *)pci_alloc_consistent(pdev, PCI_WINDOW_SIZE, &wc->writedma);
			if (!wc->writechunk) {
				printk("wcdte error: Unable to allocate DMA-able memory\n");
				if (wc->freeregion)
					release_region(wc->iobase, 0xff);
				return -ENOMEM;
			}

			wc->readchunk = wc->writechunk + (SFRAME_SIZE * ERING_SIZE) / 4;	/* in doublewords */
			wc->readdma = wc->writedma + (SFRAME_SIZE * ERING_SIZE);		/* in bytes */

			wc->descripchunk = wc->readchunk + (SFRAME_SIZE * ERING_SIZE) / 4;	/* in doublewords */
			wc->descripdma = wc->readdma + (SFRAME_SIZE * ERING_SIZE);		/* in bytes */

			/* Initialize Write/Buffers to all blank data */
			memset((void *)wc->writechunk,0x00, SFRAME_SIZE * 2);
			memset((void *)wc->readchunk, 0x00, SFRAME_SIZE * 2);

			init_waitqueue_head(&wc->regq);
		
			/* Initialize the work queue */
			wc->dte_wq = create_singlethread_workqueue("tc400b");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
			INIT_WORK(&wc->dte_work, dte_wque_run);
#else
			INIT_WORK(&wc->dte_work, dte_wque_run, wc);
#endif

#if defined(HOTPLUG_FIRMWARE)
			if ((request_firmware(&firmware, tc400m_firmware, &wc->dev->dev) != 0) ||
			    !firmware) {
				printk("TC400M: firmware %s not available from userspace\n", tc400m_firmware);
				return -EIO;
			}
#else
			embedded_firmware.data = _binary_dahdi_fw_tc400m_bin_start;
			embedded_firmware.size = (size_t) &_binary_dahdi_fw_tc400m_bin_size;
#endif

			dte_firmware_ver = firmware->data[0];
			dte_firmware_ver_minor = firmware->data[16];
			g729_numchannels = firmware->data[1];
			g723_numchannels = firmware->data[2];

			if (g723_numchannels < g729_numchannels)
				min_numchannels = g723_numchannels;
			else
				min_numchannels = g729_numchannels;

			/* Setup Encoders and Decoders */

			if (!mode || strlen(mode) < 4) {
				sprintf(wc->complexname, "G.729a / G.723.1");
 				complexfmts = DAHDI_FORMAT_G729A | DAHDI_FORMAT_G723_1;
				wc->numchannels = min_numchannels;
			} else if (mode[3] == '9') {	/* "G.729" */
				sprintf(wc->complexname, "G.729a");
				complexfmts = DAHDI_FORMAT_G729A;
				wc->numchannels = g729_numchannels;
			} else if (mode[3] == '3') {	/* "G.723.1" */
				sprintf(wc->complexname, "G.723.1");
				complexfmts = DAHDI_FORMAT_G723_1;
				wc->numchannels = g723_numchannels;
			} else {
				sprintf(wc->complexname, "G.729a / G.723.1");
				complexfmts = DAHDI_FORMAT_G729A | DAHDI_FORMAT_G723_1;
				wc->numchannels = min_numchannels;
			}
			
			uencode = dahdi_transcoder_alloc(wc->numchannels);
			udecode = dahdi_transcoder_alloc(wc->numchannels);
			encoders = vmalloc(sizeof(struct dte_state) * wc->numchannels);
			decoders = vmalloc(sizeof(struct dte_state) * wc->numchannels);
			memset(encoders, 0, sizeof(struct dte_state) * wc->numchannels);
			memset(decoders, 0, sizeof(struct dte_state) * wc->numchannels);
			if (!uencode || !udecode || !encoders || !decoders) {
				if (uencode)
					dahdi_transcoder_free(uencode);
				if (udecode)
					dahdi_transcoder_free(udecode);
				if (encoders)
					vfree(encoders);
				if (decoders)
					vfree(decoders);
				return -ENOMEM;
			}
			sprintf(udecode->name, "DTE Decoder");
			sprintf(uencode->name, "DTE Encoder");
	
			udecode->srcfmts = uencode->dstfmts = complexfmts;
			udecode->dstfmts = uencode->srcfmts = DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW;
			
			udecode->operation = uencode->operation = dte_operation;

			for (x=0;x<wc->numchannels;x++) {
				dte_init_state(encoders + x, 1, x, wc);
				encoders[x].encoder = 1;
				decoders[x].encoder = 0;
				dte_init_state(decoders + x, 0, x, wc);
				uencode->channels[x].pvt = encoders + x;
				udecode->channels[x].pvt = decoders + x;
			}

			wc->uencode = uencode;
			wc->udecode = udecode;
		
			dahdi_transcoder_register(uencode);
			dahdi_transcoder_register(udecode);

			printk("DAHDI DTE (%s) Transcoder support LOADED (firm ver = %d.%d)\n", wc->complexname, dte_firmware_ver, dte_firmware_ver_minor);


			/* Enable bus mastering */
			pci_set_master(pdev);

			/* Keep track of which device we are */
			pci_set_drvdata(pdev, wc);

			if (request_irq(pdev->irq, wcdte_interrupt, DAHDI_IRQ_SHARED, "tc400b", wc)) {
				printk("wcdte error: Unable to request IRQ %d\n", pdev->irq);
				if (wc->freeregion)
					release_region(wc->iobase, 0xff);
				pci_free_consistent(pdev, PCI_WINDOW_SIZE, (void *)wc->writechunk, wc->writedma);
				pci_set_drvdata(pdev, NULL);
				vfree(wc);
				return -EIO;
			}


			if (wcdte_hardware_init(wc)) {
				/* Set Reset Low */
				wcdte_stop_dma(wc);
				/* Free Resources */
				free_irq(pdev->irq, wc);
				if (wc->freeregion)
					release_region(wc->iobase, 0xff);
				pci_free_consistent(pdev, PCI_WINDOW_SIZE, (void *)wc->writechunk, wc->writedma);
				pci_set_drvdata(pdev, NULL);
				vfree(wc);
				return -EIO;

			}

			/* Enable interrupts */
			wcdte_enable_interrupts(wc);

			/* Start DMA */
			wcdte_start_dma(wc);

			if (wcdte_boot_processor(wc,firmware,1)) {
				if (firmware != &embedded_firmware)
					release_firmware(firmware);

				/* Set Reset Low */
				wcdte_stop_dma(wc);
				/* Free Resources */
				free_irq(pdev->irq, wc);
				if (wc->freeregion)
					release_region(wc->iobase, 0xff);
				pci_free_consistent(pdev, PCI_WINDOW_SIZE, (void *)wc->writechunk, wc->writedma);
				pci_set_drvdata(pdev, NULL);
				vfree(wc);
				return -EIO;
			}
			if (firmware != &embedded_firmware)
				release_firmware(firmware);

			if (wcdte_setup_channels(wc)) {
				/* Set Reset Low */
				wcdte_stop_dma(wc);
				/* Free Resources */
				free_irq(pdev->irq, wc);
				if (wc->freeregion)
					release_region(wc->iobase, 0xff);
				pci_free_consistent(pdev, PCI_WINDOW_SIZE, (void *)wc->writechunk, wc->writedma);
				pci_set_drvdata(pdev, NULL);
				vfree(wc);
				return -EIO;
			}

			reg = wcdte_getctl(wc, 0x00fc);
			if (debug)
				printk("wcdte debug: (post-boot) Reg fc is %08x\n", reg);
			
			printk("Found and successfully installed a Wildcard TC: %s \n", wc->variety);
			if (debug) {
				printk("TC400B operating in DEBUG mode\n");
				printk("debug_des = %d, debug_des_cnt = %d, force_alert = %d,\n debug_notimeout = %d, debug_packets = %d\n", 
					debug_des, debug_des_cnt, force_alert, debug_notimeout, debug_packets);
			}
			res = 0;
		} else
			res = -ENOMEM;
	}
	return res;
}

static void wcdte_release(struct wcdte *wc)
{
	if (wc->freeregion)
		release_region(wc->iobase, 0xff);
	vfree(wc);
}

static void __devexit wcdte_remove_one(struct pci_dev *pdev)
{
	int i;
	struct wcdte *wc = pci_get_drvdata(pdev);
	struct dahdi_transcoder_channel *ztc_en, *ztc_de;
	struct dte_state *st_en, *st_de;

	if (wc) {
		if (debug)
		{
			printk("wcdte debug: wc->ztsnd_rtx = %d\n", wc->ztsnd_rtx);
			printk("wcdte debug: wc->ztsnd_0010_rtx = %d\n", wc->ztsnd_0010_rtx);
						
			for(i = 0; i < wc->numchannels; i++)
			{
				ztc_en = &(wc->uencode->channels[i]);
				st_en = ztc_en->pvt;
			
				ztc_de = &(wc->udecode->channels[i]);
				st_de = ztc_de->pvt;

				printk("wcdte debug: en[%d] snt = %d, rcv = %d [%d]\n", i, st_en->packets_sent, st_en->packets_received, st_en->packets_sent - st_en->packets_received);
				printk("wcdte debug: de[%d] snt = %d, rcv = %d [%d]\n", i, st_de->packets_sent, st_de->packets_received, st_de->packets_sent - st_de->packets_received);
			}
		}

		dahdi_transcoder_unregister(wc->udecode);
		dahdi_transcoder_unregister(wc->uencode);
		dahdi_transcoder_free(wc->uencode);
		dahdi_transcoder_free(wc->udecode);
		vfree(wc->uencode->channels[0].pvt);
		vfree(wc->udecode->channels[0].pvt);
	
		/* Stop any DMA */
		wcdte_stop_dma(wc);

		/* In case hardware is still there */
		wcdte_disable_interrupts(wc);

		/* Kill workqueue */
		destroy_workqueue(wc->dte_wq);
		
		/* Immediately free resources */
		pci_free_consistent(pdev, PCI_WINDOW_SIZE, (void *)wc->writechunk, wc->writedma);
		free_irq(pdev->irq, wc);

		/* Release span, possibly delayed */
		wcdte_release(wc);
	}
}

static struct pci_device_id wcdte_pci_tbl[] = {
#ifndef USE_TEST_HW
  	{ 0xd161, 0x3400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctc400p }, /* Digium board */
  	{ 0xd161, 0x8004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctce400 }, /* Digium board */
#else
	{ 0x1317, 0x0985, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctc400p }, /* reference board */
#endif
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, wcdte_pci_tbl);

static struct pci_driver wcdte_driver = {
	name: 	"wctc4xxp",
	probe: 	wcdte_init_one,
	remove:	__devexit_p(wcdte_remove_one),
	suspend: NULL,
	resume:	NULL,
	id_table: wcdte_pci_tbl,
};

int ztdte_init(void)
{
	int res;

	res = dahdi_pci_module(&wcdte_driver);
	if (res)
		return -ENODEV;
	return 0;
}

void ztdte_cleanup(void)
{
	pci_unregister_driver(&wcdte_driver);
}

module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(debug_des, int, S_IRUGO | S_IWUSR);
module_param(debug_des_cnt, int, S_IRUGO | S_IWUSR);
module_param(debug_notimeout, int, S_IRUGO | S_IWUSR);
module_param(force_alert, int, S_IRUGO | S_IWUSR);
module_param(mode, charp, S_IRUGO | S_IWUSR);
MODULE_DESCRIPTION("Wildcard TC400P+TC400M Driver");
MODULE_AUTHOR("John Sloan <jsloan@digium.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(ztdte_init);
module_exit(ztdte_cleanup);
