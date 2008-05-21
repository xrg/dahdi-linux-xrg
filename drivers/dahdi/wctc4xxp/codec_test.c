/*
 * Wilcard TC400B Digium Transcoder Engine Interface Driver for Zapata Telephony interface test tool.
 *
 * Written by Matt O'Gorman <mogorman@digium.com>
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



#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <dahdi/kernel.h>
#include <dahdi/user.h>

#define MAX_CARDS_TO_TEST	6
#define MAX_CHANNELS_PER_CARD   96

#define AST_FORMAT_ULAW		(1 << 2)
#define AST_FORMAT_G729A	(1 << 8)

#define AST_FRIENDLY_OFFSET 	64

static int debug = 0;

static unsigned char ulaw_slin_ex[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char g729a_expected[] = {
	0xE9, 0x88, 0x4C, 0xA0, 0x00, 0xFA, 0xDD, 0xA2,	0x06, 0x2D,
 	0x69, 0x88, 0x00, 0x60, 0x68, 0xD5, 0x9E, 0x20, 0x80, 0x50
};


struct format_map {
        unsigned int map[32][32];
};


struct tcpvt {
	int fd;
	int fake;
	int inuse;
	struct zt_transcode_header *hdr;
// 	struct ast_frame f;
};

struct tctest_info {
	int numcards;
	int numchans[MAX_CARDS_TO_TEST];
	int total_chans;
	int errors;
	int overcnt_error;	/* Too many cards found */
	int undercnt_error;	/* Too few cards found */
	int timeout_error[MAX_CARDS_TO_TEST];
	int data_error[MAX_CARDS_TO_TEST];
	int numcards_werrors;
};


static int find_transcoders(struct tctest_info *tctest_info)
{
	struct zt_transcode_info info = { 0, };
	int fd, res;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0) {
	        printf("Warning: No Zaptel transcoder support!\n");
	        return 0;
	}

	tctest_info->total_chans = 0;
	info.op = ZT_TCOP_GETINFO;
	for (info.tcnum = 0; !(res = ioctl(fd, ZT_TRANSCODE_OP, &info)); info.tcnum++) {
		if (debug)
			printf("Found transcoder %d, '%s' with %d channels.\n", info.tcnum, info.name, info.numchannels);
		if ((info.tcnum % 2) == 0)
		{
			tctest_info->numchans[info.tcnum/2] = info.numchannels;
			tctest_info->total_chans += info.numchannels;
		}
	}
	tctest_info->numcards = info.tcnum / 2;

	close(fd);
	if (!info.tcnum)
		printf("No hardware transcoders found.\n");
	return 0;
}


static int open_transcoder(struct tcpvt *ztp, int dest, int source)
{
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct zt_transcode_header *hdr;
	int flags;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return -1;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			printf("Could not set non-block mode!\n");
	}

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		printf("Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return -1;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		printf("Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return -1;
	}

	hdr->srcfmt = source;
	hdr->dstfmt = dest;

	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		printf("Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return -1;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return 0;
}


static void close_transcoder(struct tcpvt *ztp)
{
	unsigned int x;

	x = ZT_TCOP_RELEASE;
	if (ioctl(ztp->fd, ZT_TRANSCODE_OP, &x))
		printf("Failed to release transcoder channel: %s\n", strerror(errno));
				
	munmap(ztp->hdr, sizeof(*ztp->hdr));
	close(ztp->fd);
}


static int encode_packet(struct tcpvt *ztp, unsigned char *packet_in, unsigned char *packet_out)
{
	struct zt_transcode_header *hdr = ztp->hdr;
	unsigned int x;

	hdr->srcoffset = 0;

	memcpy(hdr->srcdata + hdr->srcoffset + hdr->srclen, packet_in, 160);
	hdr->srclen += 160;


	hdr->dstoffset = AST_FRIENDLY_OFFSET;
	x = ZT_TCOP_TRANSCODE;
	if (ioctl(ztp->fd, ZT_TRANSCODE_OP, &x))
		printf("Failed to transcode: %s\n", strerror(errno));

	usleep(20000);
	if (hdr->dstlen)
	{
		memcpy(packet_out, hdr->dstdata + hdr->dstoffset, hdr->dstlen);
		return 0;
	}
	else
		return -1;
}


static void print_failed()
{
	printf("______    ___    _____   _       _____  ______\n");
	printf("|  ___|  / _ \\  |_   _| | |     |  ___| |  _  \\\n");
	printf("| |_    / /_\\ \\   | |   | |     | |__   | | | |\n");
	printf("|  _|   |  _  |   | |   | |     |  __|  | | | |\n");
	printf("| |     | | | |  _| |_  | |____ | |___  | |/ /\n");
	printf("\\_|     \\_| |_/  \\___/  \\_____/ \\____/  |___/ \n");
}


int main(int argc, char *argv[])
{
	int arg1, arg2, i, j, card_testing, chan_testing;
	struct tcpvt ztp[MAX_CHANNELS_PER_CARD * MAX_CARDS_TO_TEST];
	unsigned char packet_out[200];
	struct tctest_info tctest_info;

	memset(&tctest_info, 0, sizeof(tctest_info));

	if ((argc < 2) || (argc > 3))
	{
		printf("codec_test requires one argument.\n");
		printf("     arg1 = number of cards to test\n");
		return -1;
	}

	if (argc == 2)
		sscanf(argv[1], "%d", &arg1);
	else if (argc == 3)
	{
		sscanf(argv[1], "%d %d", &arg1, &arg2);
		debug = arg2;
	}

	printf("Beginning test of %d TC400B cards\n", arg1);


	/* Search for TC400Bs */
	find_transcoders(&tctest_info);

	if (tctest_info.numcards > arg1)
	{
		tctest_info.errors++;
		tctest_info.overcnt_error = 1;
	}
	if (tctest_info.numcards < arg1)
	{
		tctest_info.errors++;
		tctest_info.undercnt_error = 1;
	}

	if (tctest_info.errors == 0)
	{
		/* Begin testing transcoder channels */
		for (card_testing = 0; card_testing < tctest_info.numcards; card_testing++)
		{
			tctest_info.data_error[card_testing] = 0;
			tctest_info.timeout_error[card_testing] = 0;
			for (chan_testing = 0; chan_testing < tctest_info.numchans[card_testing]; chan_testing++)
			{
				i = chan_testing;
				for(j = 0; j < card_testing; j++)
					i += tctest_info.numchans[j];
 
 				open_transcoder(&ztp[i], AST_FORMAT_G729A, AST_FORMAT_ULAW);

				if ((tctest_info.timeout_error[card_testing] = encode_packet(&ztp[i], ulaw_slin_ex, packet_out) == -1))
					tctest_info.errors++;

				if (memcmp(g729a_expected, packet_out, 20) != 0)
				{
					tctest_info.errors++;
					tctest_info.data_error[card_testing] += 1;
				}
			}
			if ( (tctest_info.data_error[card_testing]) || (tctest_info.timeout_error[card_testing]) )
				tctest_info.numcards_werrors++;
		}

		for (i = 0; i < tctest_info.total_chans; i++)
			close_transcoder(&ztp[i]);
	}

	if (debug)
	{
		printf("\n\n");
		printf("tctest_info.errors = %d\n", tctest_info.errors);
		printf("tctest_info.overcnt_error = %d\n", tctest_info.overcnt_error);
		printf("tctest_info.undercnt_error = %d\n", tctest_info.undercnt_error);
		printf("tctest_info.numcards_werrors = %d\n", tctest_info.numcards_werrors);

		for (i = 0; i < tctest_info.numcards; i++)
		{
			printf("tctest_info.data_error[%d] = %d\n", i, tctest_info.data_error[i]);
			printf("tctest_info.timeout_error[%d] = %d\n", i, tctest_info.timeout_error[i]);
		}
	}

	if (tctest_info.errors)
	{
		printf("\n\n\n");
		if (tctest_info.numcards_werrors)
			printf("%d of %d cards\n", tctest_info.numcards_werrors, tctest_info.numcards);	
		print_failed();
		if (tctest_info.overcnt_error)
			printf("\n%d cards found, %d expected\n", tctest_info.numcards, arg1);
		if (tctest_info.undercnt_error)
			printf("\n%d cards found, %d expected\n", tctest_info.numcards, arg1);
		printf("\n\n\n");
	}
	else
		printf("%d of %d cards PASSED\n", tctest_info.numcards - tctest_info.numcards_werrors, tctest_info.numcards);	

	return 0;
}
