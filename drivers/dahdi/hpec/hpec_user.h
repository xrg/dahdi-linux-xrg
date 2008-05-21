/*
 * Zapata Telephony Interface to Digium High-Performance Echo Canceller
 *
 * Copyright (C) 2006 Digium, Inc.
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

#if !defined(_HPEC_USER_H)
#define _HPEC_USER_H

struct hpec_challenge {
	__u8 challenge[16];
};

struct hpec_license {
	__u32 numchannels;
        __u8 userinfo[256];
	__u8 response[16];
};

#define DAHDI_EC_LICENSE_CHALLENGE _IOR(DAHDI_CODE, 60, struct hpec_challenge)
#define DAHDI_EC_LICENSE_RESPONSE  _IOW(DAHDI_CODE, 61, struct hpec_license)

#endif /* !defined(_HPEC_USER_H) */

