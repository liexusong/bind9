/*
 * Copyright (C) 1999, 2000  Internet Software Consortium.
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

#ifndef DNS_TSIG_H
#define DNS_TSIG_H 1

#include <isc/lang.h>
#include <isc/rwlock.h>
#include <isc/stdtime.h>

#include <dns/types.h>
#include <dns/name.h>

#include <dst/dst.h>

/*
 * Standard algorithm.
 */
#define DNS_TSIG_HMACMD5		"HMAC-MD5.SIG-ALG.REG.INT."
extern dns_name_t *dns_tsig_hmacmd5_name;
#define DNS_TSIG_HMACMD5_NAME		dns_tsig_hmacmd5_name

/*
 * Default fudge value.
 */
#define DNS_TSIG_FUDGE			300

struct dns_tsig_keyring {
	ISC_LIST(dns_tsigkey_t) keys;
	isc_rwlock_t lock;
	isc_mem_t *mctx;
}; 

struct dns_tsigkey {
	/* Unlocked */
	unsigned int		magic;		/* Magic number. */
	isc_mem_t		*mctx;
	dst_key_t		*key;		/* Key */
	dns_name_t		name;		/* Key name */
	dns_name_t		algorithm;	/* Algorithm name */
	dns_name_t		*creator;	/* name that created secret */
	isc_boolean_t		generated;	/* was this generated? */
	isc_stdtime_t		inception;	/* start of validity period */
	isc_stdtime_t		expire;		/* end of validity period */
	dns_tsig_keyring_t	*ring;		/* the enclosing keyring */
	isc_mutex_t		lock;
	/* Locked */
	isc_boolean_t		deleted;	/* has this been deleted? */
	isc_uint32_t		refs;		/* reference counter */
	/* Unlocked */
	ISC_LINK(dns_tsigkey_t)	link;
};

#define dns_tsigkey_empty(tsigkey) ((tsigkey)->key == NULL)
#define dns_tsigkey_identity(tsigkey) \
	((tsigkey)->generated ? ((tsigkey)->creator) : (&((tsigkey)->name)))

ISC_LANG_BEGINDECLS

isc_result_t
dns_tsigkey_create(dns_name_t *name, dns_name_t *algorithm,
		   unsigned char *secret, int length, isc_boolean_t generated,
		   dns_name_t *creator, isc_stdtime_t inception,
		   isc_stdtime_t expire, isc_mem_t *mctx,
		   dns_tsig_keyring_t *ring, dns_tsigkey_t **key);
/*
 *	Creates a tsig key structure and saves it in the keyring.  If key is
 *	not NULL, *key will contain a copy of the key.  The keys validity
 *	period is specified by (inception, expire), and will not expire if
 *	inception == expire.  If the key was generated, the creating identity,
 *	if there is one, should be in the creator parameter.
 *
 *	Requires:
 *		'name' is a valid dns_name_t
 *		'algorithm' is a valid dns_name_t
 *		'secret' is a valid pointer
 *		'length' is an integer greater than 0
 *		'creator' points to a valid dns_name_t or is NULL
 *		'mctx' is a valid memory context
 *		'ring' is a valid TSIG keyring
 *		'key' or '*key' must be NULL
 *
 *	Returns:
 *		ISC_R_SUCCESS
 *		ISC_R_EXISTS - a key with this name already exists
 *		ISC_R_NOTIMPLEMENTED - algorithm is not implemented
 *		ISC_R_NOMEMORY
 */

void
dns_tsigkey_free(dns_tsigkey_t **key);
/*
 *	Frees the tsig key structure pointed to by 'key'.
 *
 *	Requires:
 *		'key' is a valid TSIG key
 *		'ring' is a valid TSIG keyring containing the key
 */

void
dns_tsigkey_setdeleted(dns_tsigkey_t *key);
/*
 *	Marks this key as deleted.  It will be deleted when no references exist.
 *
 *	Requires:
 *		'key' is a valid TSIG key
 */

isc_result_t
dns_tsig_sign(dns_message_t *msg);
/*
 *	Generates a TSIG record for this message
 *
 *	Requires:
 *		'msg' is a valid message
 *		'msg->tsigkey' is a valid TSIG key
 *		'msg->tsig' is NULL
 *
 *	Returns:
 *		ISC_R_SUCCESS
 *		ISC_R_NOMEMORY
 *		ISC_R_NOSPACE
 *		DNS_R_EXPECTEDTSIG - this is a response & msg->querytsig is NULL
 */

isc_result_t
dns_tsig_verify(isc_buffer_t *source, dns_message_t *msg,
		dns_tsig_keyring_t *sring, dns_tsig_keyring_t *dring);
/*
 *	Verifies the TSIG record in this message
 *
 *	Requires:
 *		'source' is a valid buffer containing the unparsed message
 *		'msg' is a valid message
 *		'msg->tsigkey' is a valid TSIG key if this is a response
 *		'msg->tsig' is NULL
 *		'msg->querytsig' is not NULL if this is a response
 *		'sring' is a valid keyring or NULL
 *		'dring' is a valid keyring or NULL
 *
 *	Returns:
 *		ISC_R_SUCCESS
 *		ISC_R_NOMEMORY
 *		DNS_R_EXPECTEDTSIG - A TSIG was expected but not seen
 *		DNS_R_UNEXPECTEDTSIG - A TSIG was seen but not expected
 *		DNS_R_TSIGERRORSET - the TSIG verified but ->error was set
 *				     and this is a query
 *		DNS_R_TSIGVERIFYFAILURE - the TSIG failed to verify
 */

isc_result_t
dns_tsigkey_find(dns_tsigkey_t **tsigkey, dns_name_t *name,
		 dns_name_t *algorithm, dns_tsig_keyring_t *ring);
/*
 *	Returns the TSIG key corresponding to this name and (possibly)
 *	algorithm.  Also increments the key's reference counter.
 *
 *	Requires:
 *		'tsigkey' is not NULL
 *		'*tsigkey' is NULL
 *		'name' is a valid dns_name_t
 *		'algorithm' is a valid dns_name_t or NULL
 *		'ring' is a valid keyring
 *
 *	Returns:
 *		ISC_R_SUCCESS
 *		ISC_R_NOTFOUND
 */


isc_result_t
dns_tsigkeyring_create(isc_mem_t *mctx, dns_tsig_keyring_t **ring);
/*
 *	Create an empty TSIG key ring.
 *
 *	Requires:
 *		'mctx' is not NULL
 *		'ring' is not NULL, and '*ring' is NULL
 *
 *	Returns:
 *		ISC_R_SUCCESS
 *		ISC_R_NOMEMORY
 */


void
dns_tsigkeyring_destroy(dns_tsig_keyring_t **ring);
/*
 *	Destroy a TSIG key ring.
 *
 *	Requires:
 *		'ring' is not NULL
 */

ISC_LANG_ENDDECLS

#endif /* DNS_TSIG_H */