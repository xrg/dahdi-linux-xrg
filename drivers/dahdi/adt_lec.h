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

#ifndef _ADT_LEC_H
#define _ADT_LEC_H

enum adt_lec_nlp_type {
	ADT_LEC_NLP_OFF = 0,
	ADT_LEC_NLP_MUTE,
	ADT_LEC_RANDOM_NOISE,
	ADT_LEC_HOTH_NOISE,
	ADT_LEC_SUPPRESS,
};

struct adt_lec_params {
	__u32 tap_length;
	enum adt_lec_nlp_type nlp_type;
	__u32 nlp_threshold;
	__u32 nlp_max_suppress;
};

#endif /* _ADT_LEC_H */
