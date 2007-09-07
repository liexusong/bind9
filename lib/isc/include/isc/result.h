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

#ifndef ISC_RESULT_H
#define ISC_RESULT_H 1

#include <isc/lang.h>
#include <isc/types.h>

#define ISC_R_SUCCESS			0
#define ISC_R_NOMEMORY			1
#define ISC_R_TIMEDOUT			2
#define ISC_R_NOTHREADS			3
#define ISC_R_ADDRNOTAVAIL		4
#define ISC_R_ADDRINUSE			5
#define ISC_R_NOPERM			6
#define ISC_R_NOCONN			7
#define ISC_R_NETUNREACH		8
#define ISC_R_HOSTUNREACH		9
#define ISC_R_NETDOWN			10
#define ISC_R_HOSTDOWN			11
#define ISC_R_CONNREFUSED		12
#define ISC_R_NORESOURCES		13	/* not enough resources */
#define ISC_R_EOF			14	/* end of file */
#define ISC_R_BOUND			15	/* already bound */
#define ISC_R_RELOAD			16
#define ISC_R_LOCKBUSY			17
#define ISC_R_EXISTS			18
#define ISC_R_NOSPACE			19	/* ran out of space */
#define ISC_R_CANCELED			20
#define ISC_R_NOTBOUND			21	/* socket is not bound */
#define ISC_R_SHUTTINGDOWN		22	/* shutting down */
#define ISC_R_NOTFOUND			23
#define ISC_R_UNEXPECTEDEND		24	/* unexpected end of input */
#define ISC_R_FAILURE			25	/* generic failure */
#define ISC_R_IOERROR			26
#define ISC_R_NOTIMPLEMENTED		27
#define ISC_R_UNBALANCED		28
#define ISC_R_NOMORE			29
#define ISC_R_INVALIDFILE		30
#define ISC_R_BADBASE64			31
#define ISC_R_UNEXPECTEDTOKEN		32
#define ISC_R_QUOTA			33
#define ISC_R_UNEXPECTED		34
#define ISC_R_ALREADYRUNNING		35
#define ISC_R_IGNORE			36
#define ISC_R_MASKNONCONTIG             37
#define ISC_R_FILENOTFOUND		38
#define ISC_R_FILEEXISTS		39
#define ISC_R_NOTCONNECTED		40	/* socket is not connected */
#define ISC_R_RANGE			41	/* out of range */

#define ISC_R_NRESULTS 			42	/* Number of results */

ISC_LANG_BEGINDECLS

char *
isc_result_totext(isc_result_t);
/*
 * Convert an isc_result_t into a string message describing the result.
 */

isc_result_t
isc_result_register(unsigned int base, unsigned int nresults, char **text,
		    isc_msgcat_t *msgcat, int set);

ISC_LANG_ENDDECLS

#endif /* ISC_RESULT_H */