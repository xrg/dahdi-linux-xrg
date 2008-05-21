/*
 * ADT Line Echo Canceller Parameter Parsing
 *
 * Copyright (C) 2008 Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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

#ifndef _ADT_LEC_C
#define _ADT_LEC_C

#include <linux/ctype.h>

static inline void adt_lec_init_defaults(struct adt_lec_params *params, __u32 tap_length)
{
	memset(params, 0, sizeof(*params));
	params->tap_length = tap_length;
}

static int adt_lec_parse_params(struct adt_lec_params *params, struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p)
{
	unsigned int x;
	char *c;

	for (x = 0; x < ecp->param_count; x++) {
		for (c = p[x].name; *c; c++)
			*c = tolower(*c);
		if (!strcmp(p[x].name, "nlp_type")) {
			switch (p[x].value) {
			case ADT_LEC_NLP_OFF:
			case ADT_LEC_NLP_MUTE:
			case ADT_LEC_RANDOM_NOISE:
			case ADT_LEC_HOTH_NOISE:
			case ADT_LEC_SUPPRESS:
				params->nlp_type = p[x].value;
				break;
			default:
				return -EINVAL;
			}
		} else if (!strcmp(p[x].name, "nlp_thresh")) {
			params->nlp_threshold = p[x].value;
		} else if (!strcmp(p[x].name, "nlp_suppress")) {
			params->nlp_max_suppress = p[x].value;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

#endif /* _ADT_LEC_C */
