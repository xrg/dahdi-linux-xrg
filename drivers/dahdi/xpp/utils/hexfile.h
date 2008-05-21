/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2006, Xorcom
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

#ifndef	PARSE_HEXFILE_H
#define	PARSE_HEXFILE_H

#include <stdarg.h>

/*
 * Some portability workarounds
 */
#ifdef	_WINDOWS

#include <windows.h>	/* for UCHAR USHORT */
typedef UCHAR	uint8_t;
typedef USHORT	uint16_t;
#define	PACKED
#define	sscanf	sscanf_s
#define	ZERO_SIZE	1

/* From /usr/include/syslog.h */
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

#ifdef  __cplusplus
# define __BEGIN_DECLS  extern "C" {
# define __END_DECLS    }
#else
# define __BEGIN_DECLS
# define __END_DECLS
#endif

#elif	__GNUC__

#include <stdint.h>
#include <syslog.h>
#define	PACKED	__attribute__((packed))
#define	ZERO_SIZE	1

#else

#error "Cannot compile on this platform"

#endif

/* Record types in hexfile */
enum {
	TT_DATA		= 0,
	TT_EOF		= 1,
	TT_EXT_SEG	= 2,
	TT_START_SEG	= 3,
	TT_EXT_LIN	= 4,
	TT_START_LIN	= 5,
	TT_NO_SUCH_TT
};

#pragma pack(1)
struct hexline {
	union {
		uint8_t		raw[ZERO_SIZE];
		struct content {
			struct header {
				uint8_t		ll;	/* len */
				uint16_t	offset;	/* offset */
				uint8_t		tt;	/* type */
			} PACKED header;
			struct tt_data {
				uint8_t		data[ZERO_SIZE];
			} tt_data;
		} PACKED content;
	} d;
} PACKED;
#pragma pack()

struct hexdata {
	unsigned int		maxlines;
	unsigned int		last_line;
	int			got_eof;
	char			version_info[BUFSIZ];
	struct hexline		*lines[ZERO_SIZE];
};


__BEGIN_DECLS

typedef void (*parse_hexfile_report_func_t)(int level, const char *msg, ...);

parse_hexfile_report_func_t parse_hexfile_set_reporting(parse_hexfile_report_func_t rf);
void free_hexdata(struct hexdata *hexdata);
struct hexdata *parse_hexfile(const char *fname, unsigned int maxlines);
int dump_hexfile(struct hexdata *hexdata, const char *outfile);
int dump_hexfile2(struct hexdata *hexdata, const char *outfile, uint8_t maxwidth);
void dump_binary(struct hexdata *hexdata, const char *outfile);
void gen_hexline(const uint8_t *data, uint16_t addr, size_t len, FILE *output);
int bsd_checksum(struct hexdata *hexdata);
__END_DECLS

#endif
