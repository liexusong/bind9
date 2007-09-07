/*
 * Copyright (C) 1998, 1999, 2000  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

 /* $Id: soa_6.h,v 1.19 2000/02/03 23:43:07 halley Exp $ */

#include <dns/name.h>

typedef struct dns_rdata_soa {
	dns_rdatacommon_t	common;
	isc_mem_t		*mctx;
	dns_name_t		origin;
	dns_name_t		mname;
	isc_uint32_t		serial;		/* host order */
	isc_uint32_t		refresh;	/* host order */
	isc_uint32_t		retry;		/* host order */
	isc_uint32_t		expire;		/* host order */
	isc_uint32_t		minimum;	/* host order */
} dns_rdata_soa_t;

