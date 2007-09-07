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

#include <config.h>

#include <stdlib.h>

#include <isc/commandline.h>
#include <isc/mem.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/dnssec.h>
#include <dns/keyvalues.h>
#include <dns/log.h>
#include <dns/nxt.h>
#include <dns/rdata.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rdatastruct.h>
#include <dns/rdatatype.h>
#include <dns/result.h>
#include <dns/secalg.h>
#include <dns/time.h>

#include <dst/result.h>

#define PROGRAM "dnssec-signzone"

/*#define USE_ZONESTATUS*/

#define BUFSIZE 2048

typedef struct signer_key_struct signer_key_t;
typedef struct signer_array_struct signer_array_t;

struct signer_key_struct {
	dst_key_t *key;
	isc_boolean_t isdefault;
	ISC_LINK(signer_key_t) link;
};

struct signer_array_struct {
	unsigned char array[BUFSIZE];
	ISC_LINK(signer_array_t) link;
};

static ISC_LIST(signer_key_t) keylist;
static isc_stdtime_t starttime = 0, endtime = 0, now;
static int cycle = -1;
static int verbose;
static isc_boolean_t tryverify = ISC_FALSE;

static isc_mem_t *mctx = NULL;

static void
fatal(char *format, ...) {
	va_list args;

	fprintf(stderr, "%s: ", PROGRAM);
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

static inline void
check_result(isc_result_t result, char *message) {
	if (result != ISC_R_SUCCESS) {
		fprintf(stderr, "%s: %s: %s\n", PROGRAM, message,
			isc_result_totext(result));
		exit(1);
	}
}

static void
vbprintf(int level, const char *fmt, ...) {
	va_list ap;
	if (level > verbose)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "%s: ", PROGRAM);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* Not thread-safe! */
static char *
nametostr(dns_name_t *name) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;
	static char data[1025];

	isc_buffer_init(&b, data, sizeof(data));
	result = dns_name_totext(name, ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	return (char *) r.base;
}

/* Not thread-safe! */
static char *
typetostr(const dns_rdatatype_t type) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;
	static char data[10];

	isc_buffer_init(&b, data, sizeof(data));
	result = dns_rdatatype_totext(type, &b);
	check_result(result, "dns_rdatatype_totext()");
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	return (char *) r.base;
}

/* Not thread-safe! */
static char *
algtostr(const dns_secalg_t alg) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;
	static char data[10];

	isc_buffer_init(&b, data, sizeof(data));
	result = dns_secalg_totext(alg, &b);
	check_result(result, "dns_secalg_totext()");
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	return (char *) r.base;
}

static inline void
set_bit(unsigned char *array, unsigned int index, unsigned int bit) {
	unsigned int byte, shift, mask;

	byte = array[index / 8];
	shift = 7 - (index % 8);
	mask = 1 << shift;

	if (bit)
		array[index / 8] |= mask;
	else
		array[index / 8] &= (~mask & 0xFF);
}

static void
signwithkey(dns_name_t *name, dns_rdataset_t *rdataset, dns_rdata_t *rdata,
	    dst_key_t *key, isc_buffer_t *b)
{
	isc_result_t result;

	dns_rdata_init(rdata);
	result = dns_dnssec_sign(name, rdataset, key, &starttime, &endtime,
				 mctx, b, rdata);
	if (result != ISC_R_SUCCESS)
		fatal("key '%s/%s/%d' failed to sign data: %s",
		      dst_key_name(key), algtostr(dst_key_alg(key)),
		      dst_key_id(key), isc_result_totext(result));

	if (tryverify) {
		result = dns_dnssec_verify(name, rdataset, key,
					   ISC_TRUE, mctx, rdata);
		if (result == ISC_R_SUCCESS)
			vbprintf(3, "\tsignature verified\n");
		else
			vbprintf(3, "\tsignature failed to verify\n");
	}
}

static inline isc_boolean_t
issigningkey(signer_key_t *key) {
	return (key->isdefault);
}

static inline isc_boolean_t
iszonekey(signer_key_t *key, dns_db_t *db) {
	char origin[1024];
	isc_buffer_t b;
	isc_result_t result;

	isc_buffer_init(&b, origin, sizeof(origin));
	result = dns_name_totext(dns_db_origin(db), ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");

	return (ISC_TF(strcasecmp(dst_key_name(key->key), origin) == 0 &&
		(dst_key_flags(key->key) & DNS_KEYFLAG_OWNERMASK) ==
		 DNS_KEYOWNER_ZONE));
}

/*
 * Finds the key that generated a SIG, if possible.  First look at the keys
 * that we've loaded already, and then see if there's a key on disk.
 */
static signer_key_t *
keythatsigned(dns_rdata_sig_t *sig) {
	char *keyname;
	isc_result_t result;
	dst_key_t *pubkey = NULL, *privkey = NULL;
	signer_key_t *key;

	keyname = nametostr(&sig->signer);

	key = ISC_LIST_HEAD(keylist);
	while (key != NULL) {
		if (sig->keyid == dst_key_id(key->key) &&
		    sig->algorithm == dst_key_alg(key->key) &&
		    strcasecmp(keyname, dst_key_name(key->key)) == 0)
			return key;
		key = ISC_LIST_NEXT(key, link);
	}

	result = dst_key_fromfile(keyname, sig->keyid, sig->algorithm,
				  DST_TYPE_PUBLIC, mctx, &pubkey);
	if (result != ISC_R_SUCCESS)
		return (NULL);

	key = isc_mem_get(mctx, sizeof(signer_key_t));
	if (key == NULL)
		fatal("out of memory");

	result = dst_key_fromfile(keyname, sig->keyid, sig->algorithm,
				  DST_TYPE_PRIVATE, mctx, &privkey);
	if (result == ISC_R_SUCCESS) {
		key->key = privkey;
		dst_key_free(&pubkey);
	}
	else
		key->key = pubkey;
	key->isdefault = ISC_FALSE;
	ISC_LIST_APPEND(keylist, key, link);
	return key;
}

/*
 * Check to see if we expect to find a key at this name.  If we see a SIG
 * and can't find the signing key that we expect to find, we drop the sig.
 * I'm not sure if this is completely correct, but it seems to work.
 */
static isc_boolean_t
expecttofindkey(dns_name_t *name, dns_db_t *db, dns_dbversion_t *version) {
	unsigned int options = DNS_DBFIND_NOWILD;
	dns_fixedname_t fname;
	isc_result_t result;

	dns_fixedname_init(&fname);
	result = dns_db_find(db, name, version, dns_rdatatype_key, options,
			     0, NULL, dns_fixedname_name(&fname), NULL, NULL);
	switch (result) {
		case ISC_R_SUCCESS:
		case DNS_R_NXDOMAIN:
		case DNS_R_NXRRSET:
			return ISC_TRUE;
		case DNS_R_DELEGATION:
		case DNS_R_CNAME:
		case DNS_R_DNAME:
			return ISC_FALSE;
		default:
			fatal("failure looking for '%s KEY' in database: %s",
			      nametostr(name), isc_result_totext(result));
			return ISC_FALSE; /* removes a warning */
	}
}

static inline isc_boolean_t
setverifies(dns_name_t *name, dns_rdataset_t *set, signer_key_t *key,
	    dns_rdata_t *sig)
{
	isc_result_t result;
	result = dns_dnssec_verify(name, set, key->key, ISC_FALSE, mctx, sig);
	return (ISC_TF(result == ISC_R_SUCCESS));
}

#define allocbufferandrdata \
	isc_buffer_t b; \
	trdata = isc_mem_get(mctx, sizeof(dns_rdata_t)); \
	tdata = isc_mem_get(mctx, sizeof(signer_array_t)); \
	ISC_LIST_APPEND(arraylist, tdata, link); \
	if (trdata == NULL || tdata == NULL) \
		fatal("out of memory"); \
	isc_buffer_init(&b, tdata->array, sizeof(tdata->array));

/*
 * Signs a set.  Goes through contortions to decide if each SIG should
 * be dropped or retained, and then determines if any new SIGs need to
 * be generated.
 */
static void
signset(dns_db_t *db, dns_dbversion_t *version, dns_dbnode_t *node,
	 dns_name_t *name, dns_rdataset_t *set)
{
	dns_rdatalist_t siglist;
	dns_rdataset_t sigset, oldsigset;
	dns_rdata_t oldsigrdata;
	dns_rdata_t *trdata;
	dns_rdata_sig_t sig;
	signer_key_t *key;
	isc_result_t result;
	isc_boolean_t notsigned = ISC_TRUE, nosigs = ISC_FALSE;
	isc_boolean_t wassignedby[256], nowsignedby[256];
	signer_array_t *tdata;
	ISC_LIST(signer_array_t) arraylist;
	int i;

	ISC_LIST_INIT(siglist.rdata);
	ISC_LIST_INIT(arraylist);

	for (i = 0; i < 256; i++)
		wassignedby[i] = nowsignedby[i] = ISC_FALSE;

	dns_rdataset_init(&oldsigset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_sig,
				     set->type, 0, &oldsigset, NULL);
	if (result == ISC_R_NOTFOUND) {
		result = ISC_R_SUCCESS;
		nosigs = ISC_TRUE;
	}
	if (result != ISC_R_SUCCESS)
		fatal("failed while looking for '%s SIG %s': %s",
		      nametostr(name), typetostr(set->type),
		      isc_result_totext(result));

	vbprintf(1, "%s/%s:\n", nametostr(name), typetostr(set->type));

	if (!nosigs) {
		result = dns_rdataset_first(&oldsigset);
		while (result == ISC_R_SUCCESS) {
			isc_boolean_t expired, future;
			isc_boolean_t keep = ISC_FALSE, resign = ISC_FALSE;

			dns_rdataset_current(&oldsigset, &oldsigrdata);

			result = dns_rdata_tostruct(&oldsigrdata, &sig, mctx);
			check_result(result, "dns_rdata_tostruct");

			expired = ISC_TF(now + cycle > sig.timeexpire);
			future = ISC_TF(now < sig.timesigned);

			key = keythatsigned(&sig);

			if (sig.timesigned > sig.timeexpire) {
				/* sig is dropped and not replaced */
				vbprintf(2, "\tsig by %s/%s/%d dropped - "
					 "invalid validity period\n",
					 nametostr(&sig.signer),
					 algtostr(sig.algorithm),
					 sig.keyid);
			}
			else if (key == NULL && !future &&
				 expecttofindkey(&sig.signer, db, version))
			{
				/* sig is dropped and not replaced */
				vbprintf(2, "\tsig by %s/%s/%d dropped - "
					 "private key not found\n",
					 nametostr(&sig.signer),
					 algtostr(sig.algorithm),
					 sig.keyid);
			}
			else if (key == NULL || future) {
				vbprintf(2, "\tsig by %s/%s/%d %s - "
					 "key not found\n",
					 expired ? "retained" : "dropped",
					 nametostr(&sig.signer),
					 algtostr(sig.algorithm),
					 sig.keyid);
				if (!expired)
					keep = ISC_TRUE;
			}
			else if (issigningkey(key)) {
				if (!expired &&
				    setverifies(name, set, key, &oldsigrdata))
				{
					vbprintf(2,
						"\tsig by %s/%s/%d retained\n",
						 nametostr(&sig.signer),
						 algtostr(sig.algorithm),
						 sig.keyid);
					keep = ISC_TRUE;
					wassignedby[sig.algorithm] = ISC_TRUE;
				}
				else {
					vbprintf(2,
						 "\tsig by %s/%s/%d dropped - "
						 "%s\n",
						 nametostr(&sig.signer),
						 algtostr(sig.algorithm),
						 sig.keyid,
						 expired ? "expired" :
							   "failed to verify");
					wassignedby[sig.algorithm] = ISC_TRUE;
					resign = ISC_TRUE;
				}
			}
			else if (iszonekey(key, db)) {
				if (!expired &&
				    setverifies(name, set, key, &oldsigrdata))
				{
					vbprintf(2,
						"\tsig by %s/%s/%d retained\n",
						 nametostr(&sig.signer),
						 algtostr(sig.algorithm),
						 sig.keyid);
					keep = ISC_TRUE;
					wassignedby[sig.algorithm] = ISC_TRUE;
					nowsignedby[sig.algorithm] = ISC_TRUE;
				}
				else {
					vbprintf(2,
						 "\tsig by %s/%s/%d "
						 "dropped - %s\n",
						 nametostr(&sig.signer),
						 algtostr(sig.algorithm),
						 sig.keyid,
						 expired ? "expired" :
							   "failed to verify");
					wassignedby[sig.algorithm] = ISC_TRUE;
					if (dst_key_isprivate(key->key))
						resign = ISC_TRUE;
				}
			}
			else if (!expired) {
				vbprintf(2, "\tsig by %s/%s/%d retained\n",
					 nametostr(&sig.signer),
					 algtostr(sig.algorithm),
					 sig.keyid);
				keep = ISC_TRUE;
			}
			else {
				vbprintf(2, "\tsig by %s/%s/%d expired\n",
					 nametostr(&sig.signer),
					 algtostr(sig.algorithm),
					 sig.keyid);
			}

			if (keep) {
				allocbufferandrdata;
				result = dns_rdata_fromstruct(trdata,
							      set->rdclass,
							     dns_rdatatype_sig,
							      &sig, &b);
				nowsignedby[sig.algorithm] = ISC_TRUE;
				ISC_LIST_APPEND(siglist.rdata, trdata, link);
			}
			else if (resign) {
				allocbufferandrdata;
				vbprintf(1, "\tresigning with key %s/%s/%d\n",
				       dst_key_name(key->key),
				       algtostr(dst_key_alg(key->key)),
				       dst_key_id(key->key));
				signwithkey(name, set, trdata, key->key, &b);
				nowsignedby[sig.algorithm] = ISC_TRUE;
				ISC_LIST_APPEND(siglist.rdata, trdata, link);
			}

			dns_rdata_freestruct(&sig);
			result = dns_rdataset_next(&oldsigset);
		}
		if (result == ISC_R_NOMORE)
			result = ISC_R_SUCCESS;
		check_result(result, "dns_db_dns_rdataset_first()/next()");
		dns_rdataset_disassociate(&oldsigset);
	}

	for (i = 0; i < 256; i++)
		if (wassignedby[i] != 0) {
			notsigned = ISC_FALSE;
			break;
		}

	key = ISC_LIST_HEAD(keylist);
	while (key != NULL) {
		int alg = dst_key_alg(key->key);
		if (key->isdefault &&
		    (notsigned || (wassignedby[alg] && !nowsignedby[alg])))
		{
			allocbufferandrdata;
			vbprintf(1, "\tsigning with key %s/%s/%d\n",
			       dst_key_name(key->key),
			       algtostr(dst_key_alg(key->key)),
			       dst_key_id(key->key));
			signwithkey(name, set, trdata, key->key, &b);
			ISC_LIST_APPEND(siglist.rdata, trdata, link);
		}
		key = ISC_LIST_NEXT(key, link);
	}

	if (!ISC_LIST_EMPTY(siglist.rdata)) {
		siglist.rdclass = set->rdclass;
		siglist.type = dns_rdatatype_sig;
		siglist.covers = set->type;
		if (endtime - starttime < set->ttl)
			siglist.ttl = endtime - starttime;
		else
			siglist.ttl = set->ttl;
		dns_rdataset_init(&sigset);
		result = dns_rdatalist_tordataset(&siglist, &sigset);
		check_result(result, "dns_rdatalist_tordataset");
		result = dns_db_addrdataset(db, node, version, 0, &sigset,
					    0, NULL);
		if (result == DNS_R_UNCHANGED)
			result = ISC_R_SUCCESS;
		check_result(result, "dns_db_addrdataset");
		dns_rdataset_disassociate(&sigset);
	}
	else if (!nosigs) {
#if 0
		/*
		 * If this is compiled in, running a signed set through the
		 * signer with no private keys causes DNS_R_BADDB to occur
		 * later.  This is bad.
		 */
		result = dns_db_deleterdataset(db, node, version,
					       dns_rdatatype_sig, set->type);
		if (result == ISC_R_NOTFOUND)
			result = ISC_R_SUCCESS;
		check_result(result, "dns_db_deleterdataset");
#endif
		fatal("File is currently signed but no private keys were "
		      "found.  This won't work.");
	}

	trdata = ISC_LIST_HEAD(siglist.rdata);
	while (trdata != NULL) {
		dns_rdata_t *next = ISC_LIST_NEXT(trdata, link);
		isc_mem_put(mctx, trdata, sizeof(dns_rdata_t));
		trdata = next;
	}

	tdata = ISC_LIST_HEAD(arraylist);
	while (tdata != NULL) {
		signer_array_t *next = ISC_LIST_NEXT(tdata, link);
		isc_mem_put(mctx, tdata, sizeof(signer_array_t));
		tdata = next;
	}
}

#ifndef USE_ZONESTATUS
/* Determine if a KEY set contains a null key */
static isc_boolean_t
hasnullkey(dns_rdataset_t *rdataset) {
	isc_result_t result;
	dns_rdata_t rdata;
	isc_boolean_t found = ISC_FALSE;

	result = dns_rdataset_first(rdataset);
	while (result == ISC_R_SUCCESS) {
		dst_key_t *key = NULL;

		dns_rdataset_current(rdataset, &rdata);
		result = dns_dnssec_keyfromrdata(dns_rootname,
						 &rdata, mctx, &key);
		if (result != ISC_R_SUCCESS)
			fatal("could not convert KEY into internal format");
		if (dst_key_isnullkey(key))
			found = ISC_TRUE;
                dst_key_free(&key);
		if (found == ISC_TRUE)
			return (ISC_TRUE);
                result = dns_rdataset_next(rdataset);
        }
        if (result != ISC_R_NOMORE)
                fatal("failure looking for null keys");
        return (ISC_FALSE);
}
#endif

/*
 * Looks for signatures of the zone keys by the parent, and imports them
 * if found.
 */
static void
importparentsig(dns_db_t *db, dns_dbversion_t *version, dns_dbnode_t *node,
		dns_name_t *name, dns_rdataset_t *set)
{
	unsigned char filename[256];
	isc_buffer_t b;
	isc_region_t r;
	dns_db_t *newdb = NULL;
	dns_dbnode_t *newnode = NULL;
	dns_rdataset_t newset, sigset;
	dns_rdata_t rdata, newrdata;
	isc_result_t result;

	isc_buffer_init(&b, filename, sizeof(filename) - 10);
	result = dns_name_totext(name, ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");
	isc_buffer_usedregion(&b, &r);
	strcpy((char *)r.base + r.length, "signedkey");
	result = dns_db_create(mctx, "rbt", name, ISC_FALSE, dns_db_class(db),
			       0, NULL, &newdb);
	check_result(result, "dns_db_create()");
	result = dns_db_load(newdb, (char *)filename);
	if (result != ISC_R_SUCCESS)
		goto failure;
	result = dns_db_findnode(newdb, name, ISC_FALSE, &newnode);
	if (result != ISC_R_SUCCESS)
		goto failure;
	dns_rdataset_init(&newset);
	dns_rdataset_init(&sigset);
	result = dns_db_findrdataset(newdb, newnode, NULL, dns_rdatatype_key,
				     0, 0, &newset, &sigset);
	if (result != ISC_R_SUCCESS)
		goto failure;

	if (dns_rdataset_count(set) != dns_rdataset_count(&newset))
		goto failure;

	dns_rdata_init(&rdata);
	dns_rdata_init(&newrdata);

	result = dns_rdataset_first(set);
	check_result(result, "dns_rdataset_first()");
	for (; result == ISC_R_SUCCESS; result = dns_rdataset_next(set)) {
		dns_rdataset_current(set, &rdata);
		result = dns_rdataset_first(&newset);
		check_result(result, "dns_rdataset_first()");
		for (;
		     result == ISC_R_SUCCESS;
		     result = dns_rdataset_next(&newset))
		{
			dns_rdataset_current(&newset, &newrdata);
			if (dns_rdata_compare(&rdata, &newrdata) == 0)
				break;
		}
		if (result != ISC_R_SUCCESS)
			break;
	}
	if (result != ISC_R_NOMORE) 
		goto failure;

	vbprintf(2, "found the parent's signature of our zone key\n");

	result = dns_db_addrdataset(db, node, version, 0, &sigset, 0, NULL);
	check_result(result, "dns_db_addrdataset");
	dns_rdataset_disassociate(&newset);
	dns_rdataset_disassociate(&sigset);

 failure:
	if (newnode != NULL)
		dns_db_detachnode(newdb, &newnode);
	if (newdb != NULL)
		dns_db_detach(&newdb);
}

/*
 * Looks for our signatures of child keys.  If present, inform the caller,
 * who will set the zone status (KEY) bit in the NXT record.
 */
static isc_boolean_t
haschildkey(dns_db_t *db, dns_name_t *name) {
	unsigned char filename[256];
	isc_buffer_t b;
	isc_region_t r;
	dns_db_t *newdb = NULL;
	dns_dbnode_t *newnode = NULL;
	dns_rdataset_t set, sigset;
	dns_rdata_t sigrdata;
	isc_result_t result;
	isc_boolean_t found = ISC_FALSE;
	dns_rdata_sig_t sig;
	signer_key_t *key;

	isc_buffer_init(&b, filename, sizeof(filename) - 10);
	result = dns_name_totext(name, ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");
	isc_buffer_usedregion(&b, &r);
	strcpy((char *)r.base + r.length, "signedkey");
	result = dns_db_create(mctx, "rbt", name, ISC_FALSE, dns_db_class(db),
			       0, NULL, &newdb);
	check_result(result, "dns_db_create()");
	result = dns_db_load(newdb, (char *)filename);
	if (result != ISC_R_SUCCESS)
		goto failure;
	result = dns_db_findnode(newdb, name, ISC_FALSE, &newnode);
	if (result != ISC_R_SUCCESS)
		goto failure;
	dns_rdataset_init(&set);
	dns_rdataset_init(&sigset);
	result = dns_db_findrdataset(newdb, newnode, NULL, dns_rdatatype_key,
				     0, 0, &set, &sigset);
	if (result != ISC_R_SUCCESS)
		goto failure;

	if (!dns_rdataset_isassociated(&set) ||
	    !dns_rdataset_isassociated(&sigset))
		goto disfail;

	result = dns_rdataset_first(&sigset);
	check_result(result, "dns_rdataset_first()");
	dns_rdata_init(&sigrdata);
	for (; result == ISC_R_SUCCESS; result = dns_rdataset_next(&sigset)) {
		dns_rdataset_current(&sigset, &sigrdata);
		result = dns_rdata_tostruct(&sigrdata, &sig, mctx);
		if (result != ISC_R_SUCCESS)
			goto disfail;
		key = keythatsigned(&sig);
		dns_rdata_freestruct(&sig);
		if (key == NULL)
			goto disfail;
		result = dns_dnssec_verify(name, &set, key->key,
					   ISC_FALSE, mctx, &sigrdata);
		if (result == ISC_R_SUCCESS) {
			found = ISC_TRUE;
			break;
		}
	}

 disfail:
	if (dns_rdataset_isassociated(&set))
		dns_rdataset_disassociate(&set);
	if (dns_rdataset_isassociated(&sigset))
		dns_rdataset_disassociate(&sigset);

 failure:
	if (newnode != NULL)
		dns_db_detachnode(newdb, &newnode);
	if (newdb != NULL)
		dns_db_detach(&newdb);

	return (found);
}

/*
 * Signs all records at a name.  This mostly just signs each set individually,
 * but also adds the SIG bit to any NXTs generated earlier, deals with
 * parent/child KEY signatures, and handles other exceptional cases.
 */
static void
signname(dns_db_t *db, dns_dbversion_t *version, dns_dbnode_t *node,
	 dns_name_t *name, isc_boolean_t atorigin)
{
	isc_result_t result;
	dns_rdata_t rdata;
	dns_rdataset_t rdataset;
	dns_rdatasetiter_t *rdsiter;
	isc_boolean_t isdelegation = ISC_FALSE;
	isc_boolean_t childkey = ISC_FALSE;
	static int warnwild = 0;

	if (dns_name_iswildcard(name)) {
		if (warnwild++ == 0) {
			fprintf(stderr, "%s: warning: BIND 9 doesn't properly "
				"handle wildcards in secure zones:\n", PROGRAM);
			fprintf(stderr, "\t- wildcard nonexistence proof is "
				"not generated by the server\n");
			fprintf(stderr, "\t- wildcard nonexistence proof is "
				"not required by the resolver\n");
		}
		fprintf(stderr, "%s: warning: wildcard name seen: %s\n",
			PROGRAM, nametostr(name));
	}
	if (!atorigin) {
		dns_rdataset_t nsset;

		dns_rdataset_init(&nsset);
		result = dns_db_findrdataset(db, node, version,
					     dns_rdatatype_ns, 0, 0, &nsset,
					     NULL);
		/* Is this a delegation point? */
		if (result == ISC_R_SUCCESS) {
			isdelegation = ISC_TRUE;
			dns_rdataset_disassociate(&nsset);
		}
	}
	dns_rdataset_init(&rdataset);
	rdsiter = NULL;
	result = dns_db_allrdatasets(db, node, version, 0, &rdsiter);
	check_result(result, "dns_db_allrdatasets()");
	result = dns_rdatasetiter_first(rdsiter);
	while (result == ISC_R_SUCCESS) {
		dns_rdatasetiter_current(rdsiter, &rdataset);

		/* If this is a SIG set, skip it. */
		if (rdataset.type == dns_rdatatype_sig)
			goto skip;

		/*
		 * If this is a KEY set at the apex, look for a signedkey file.
		 */
		if (rdataset.type == dns_rdatatype_key && atorigin) {
			importparentsig(db, version, node, name, &rdataset);
			goto skip;
		}

		/*
		 * If this name is a delegation point, skip all records
		 * except an NXT set, unless we're using null keys, in
		 * which case we need to check for a null key and add one
		 * if it's not present.
		 */
		if (isdelegation) {
			switch (rdataset.type) {
			case dns_rdatatype_nxt:
				childkey = haschildkey(db, name);
				break;
#ifndef USE_ZONESTATUS
			case dns_rdatatype_key:
				if (hasnullkey(&rdataset))
					break;
				goto skip;
#endif
			default:
				goto skip;
			}

		}

		/*
		 * There probably should be a dns_nxtsetbit, but it can get
		 * complicated if we need to extend the length of the
		 * bit set.  In this case, since the NXT bit is set and
		 * SIG < NXT and KEY < NXT, the easy way works.
		 */
		if (rdataset.type == dns_rdatatype_nxt) {
			unsigned char *nxt_bits;
			dns_name_t nxtname;
			isc_region_t r, r2;
			unsigned char keydata[4];
			dst_key_t *dstkey;
			isc_buffer_t b;

			result = dns_rdataset_first(&rdataset);
			check_result(result, "dns_rdataset_first()");
			dns_rdataset_current(&rdataset, &rdata);
			dns_rdata_toregion(&rdata, &r);
			dns_name_init(&nxtname, NULL);
			dns_name_fromregion(&nxtname, &r);
			dns_name_toregion(&nxtname, &r2);
			nxt_bits = r.base + r2.length;
			set_bit(nxt_bits, dns_rdatatype_sig, 1);
#ifdef USE_ZONESTATUS
			if (isdelegation && childkey) {
				set_bit(nxt_bits, dns_rdatatype_key, 1);
				vbprintf(2, "found a child key for %s, "
					 "setting KEY bit in NXT\n",
					 nametostr(name));
			}

#else
			if (isdelegation && !childkey) {
				dns_rdataset_t keyset;
				dns_rdatalist_t keyrdatalist;
				dns_rdata_t keyrdata;

				dns_rdataset_init(&keyset);
				result = dns_db_findrdataset(db, node, version,
							     dns_rdatatype_key,
							     0, 0, &keyset,
							     NULL);
				if (result == ISC_R_SUCCESS &&
				    hasnullkey(&keyset))
					goto alreadyhavenullkey;

				if (result == ISC_R_NOTFOUND)
					result = ISC_R_SUCCESS;
				if (result != ISC_R_SUCCESS)
					fatal("failure looking for null key "
					      "at '%s': %s", nametostr(name),
					      isc_result_totext(result));

				if (dns_rdataset_isassociated(&keyset))
					dns_rdataset_disassociate(&keyset);

				vbprintf(2, "no child key for %s, "
					 "adding null key\n",
					 nametostr(name));
				dns_rdatalist_init(&keyrdatalist);
				dstkey = NULL;
				
				result = dst_key_generate("", DNS_KEYALG_DSA,
							  0, 0,
							  DNS_KEYTYPE_NOKEY |
							  DNS_KEYOWNER_ZONE,
							  DNS_KEYPROTO_DNSSEC,
							  mctx, &dstkey);
				if (result != ISC_R_SUCCESS)
					fatal("failed to generate null key");
				isc_buffer_init(&b, keydata, sizeof keydata);
				result = dst_key_todns(dstkey, &b);
				dst_key_free(&dstkey);
				isc_buffer_usedregion(&b, &r);
				dns_rdata_fromregion(&keyrdata,
						     rdataset.rdclass,
						     dns_rdatatype_key, &r);
				
				ISC_LIST_APPEND(keyrdatalist.rdata, &keyrdata,
						link);
				keyrdatalist.rdclass = rdataset.rdclass;
				keyrdatalist.type = dns_rdatatype_key;
				keyrdatalist.covers = 0;
				keyrdatalist.ttl = rdataset.ttl;
				result =
					dns_rdatalist_tordataset(&keyrdatalist,
								 &keyset);
				check_result(result,
					     "dns_rdatalist_tordataset");
				dns_db_addrdataset(db, node, version, 0,
						   &keyset, DNS_DBADD_MERGE,
						   NULL);
				set_bit(nxt_bits, dns_rdatatype_key, 1);
				signset(db, version, node, name, &keyset);

				dns_rdataset_disassociate(&keyset);

 alreadyhavenullkey:
				;
			}
#endif
		}

		signset(db, version, node, name, &rdataset);

 skip:
		dns_rdataset_disassociate(&rdataset);
		result = dns_rdatasetiter_next(rdsiter);
	}
	if (result != ISC_R_NOMORE)
		fatal("rdataset iteration for name '%s' failed: %s",
		      nametostr(name), isc_result_totext(result));
	dns_rdatasetiter_destroy(&rdsiter);
}

static inline isc_boolean_t
active_node(dns_db_t *db, dns_dbversion_t *version, dns_dbnode_t *node) {
	dns_rdatasetiter_t *rdsiter;
	isc_boolean_t active = ISC_FALSE;
	isc_result_t result;
	dns_rdataset_t rdataset;

	dns_rdataset_init(&rdataset);
	rdsiter = NULL;
	result = dns_db_allrdatasets(db, node, version, 0, &rdsiter);
	check_result(result, "dns_db_allrdatasets()");
	result = dns_rdatasetiter_first(rdsiter);
	while (result == ISC_R_SUCCESS) {
		dns_rdatasetiter_current(rdsiter, &rdataset);
		if (rdataset.type != dns_rdatatype_nxt)
			active = ISC_TRUE;
		dns_rdataset_disassociate(&rdataset);
		if (!active)
			result = dns_rdatasetiter_next(rdsiter);
		else
			result = ISC_R_NOMORE;
	}
	if (result != ISC_R_NOMORE)
		fatal("rdataset iteration failed: %s",
		      isc_result_totext(result));
	dns_rdatasetiter_destroy(&rdsiter);

	if (!active) {
		/*
		 * Make sure there is no NXT record for this node.
		 */
		result = dns_db_deleterdataset(db, node, version,
					       dns_rdatatype_nxt, 0);
		if (result == DNS_R_UNCHANGED)
			result = ISC_R_SUCCESS;
		check_result(result, "dns_db_deleterdataset");
	}

	return (active);
}

static inline isc_result_t
next_active(dns_db_t *db, dns_dbversion_t *version, dns_dbiterator_t *dbiter,
	    dns_name_t *name, dns_dbnode_t **nodep)
{
	isc_result_t result;
	isc_boolean_t active;

	do {
		active = ISC_FALSE;
		result = dns_dbiterator_current(dbiter, nodep, name);
		if (result == ISC_R_SUCCESS) {
			active = active_node(db, version, *nodep);
			if (!active) {
				dns_db_detachnode(db, nodep);
				result = dns_dbiterator_next(dbiter);
			}
		}
	} while (result == ISC_R_SUCCESS && !active);

	return (result);
}

static inline isc_result_t
next_nonglue(dns_db_t *db, dns_dbversion_t *version, dns_dbiterator_t *dbiter,
	    dns_name_t *name, dns_dbnode_t **nodep, dns_name_t *origin,
	    dns_name_t *lastcut)
{
	isc_result_t result;

	do {
		result = next_active(db, version, dbiter, name, nodep);
		if (result == ISC_R_SUCCESS) {
			if (dns_name_issubdomain(name, origin) &&
			    (lastcut == NULL ||
			     !dns_name_issubdomain(name, lastcut)))
				return (ISC_R_SUCCESS);
			dns_db_detachnode(db, nodep);
			result = dns_dbiterator_next(dbiter);
		}
	} while (result == ISC_R_SUCCESS);
	return (result);
}

/*
 * Generates NXTs and SIGs for each non-glue name in the zone.
 */
static void
signzone(dns_db_t *db, dns_dbversion_t *version) {
	isc_result_t result, nxtresult;
	dns_dbnode_t *node, *nextnode, *curnode;
	dns_fixedname_t fname, fnextname, fcurname;
	dns_name_t *name, *nextname, *target, *curname, *lastcut;
	dns_dbiterator_t *dbiter;
	isc_boolean_t atorigin = ISC_TRUE;
	dns_name_t *origin;
	dns_rdataset_t soaset;
	dns_rdata_t soarr;
	dns_rdata_soa_t soa;
	dns_ttl_t zonettl;

	dns_fixedname_init(&fname);
	name = dns_fixedname_name(&fname);
	dns_fixedname_init(&fnextname);
	nextname = dns_fixedname_name(&fnextname);
	dns_fixedname_init(&fcurname);
	curname = dns_fixedname_name(&fcurname);

	origin = dns_db_origin(db);

	dns_rdataset_init(&soaset);
	result = dns_db_find(db, origin, version, dns_rdatatype_soa,
			     0, 0, NULL, name, &soaset, NULL);
	if (result != ISC_R_SUCCESS)
		fatal("failed to find '%s SOA' in the zone: %s",
		      nametostr(name), isc_result_totext(result));
	result = dns_rdataset_first(&soaset);
	check_result(result, "dns_rdataset_first()");
	dns_rdataset_current(&soaset, &soarr);
	result = dns_rdata_tostruct(&soarr, &soa, mctx);
	check_result(result, "dns_rdataset_tostruct()");
	zonettl = soa.minimum;
	dns_rdata_freestruct(&soa);
	dns_rdataset_disassociate(&soaset);

	lastcut = NULL;
	dbiter = NULL;
	result = dns_db_createiterator(db, ISC_FALSE, &dbiter);
	check_result(result, "dns_db_createiterator()");
	result = dns_dbiterator_first(dbiter);
	node = NULL;
	dns_name_clone(origin, name);
	result = next_nonglue(db, version, dbiter, name, &node, origin,
			      lastcut);
	while (result == ISC_R_SUCCESS) {
		nextnode = NULL;
		curnode = NULL;
		dns_dbiterator_current(dbiter, &curnode, curname);
		if (!atorigin) {
			dns_rdatasetiter_t *rdsiter = NULL;
			dns_rdataset_t set;

			dns_rdataset_init(&set);
			result = dns_db_allrdatasets(db, curnode, version,
						     0, &rdsiter);
			check_result(result, "dns_db_allrdatasets");
			result = dns_rdatasetiter_first(rdsiter);
			while (result == ISC_R_SUCCESS) {
				dns_rdatasetiter_current(rdsiter, &set);
				if (set.type == dns_rdatatype_ns) {
					dns_rdataset_disassociate(&set);
					break;
				}
				dns_rdataset_disassociate(&set);
				result = dns_rdatasetiter_next(rdsiter);
			}
			if (result != ISC_R_SUCCESS && result != ISC_R_NOMORE)
				fatal("rdataset iteration failed: %s",
				      isc_result_totext(result));
			if (result == ISC_R_SUCCESS) {
				if (lastcut != NULL)
					dns_name_free(lastcut, mctx);
				else {
					lastcut = isc_mem_get(mctx,
							sizeof(dns_name_t));
					if (lastcut == NULL)
						fatal("out of memory");
				}
				dns_name_init(lastcut, NULL);
				result = dns_name_dup(curname, mctx, lastcut);
				check_result(result, "dns_name_dup()");
			}
			dns_rdatasetiter_destroy(&rdsiter);
		}
		result = dns_dbiterator_next(dbiter);
		if (result == ISC_R_SUCCESS)
			result = next_nonglue(db, version, dbiter, nextname,
					      &nextnode, origin, lastcut);
		if (result == ISC_R_SUCCESS)
			target = nextname;
		else if (result == ISC_R_NOMORE)
			target = origin;
		else {
			target = NULL;	/* Make compiler happy. */
			fatal("iterating through the database failed: %s",
			      isc_result_totext(result));
		}
		nxtresult = dns_buildnxt(db, version, node, target, zonettl);
		check_result(nxtresult, "dns_buildnxt()");
		signname(db, version, node, curname, atorigin);
		atorigin = ISC_FALSE;
		dns_db_detachnode(db, &node);
		dns_db_detachnode(db, &curnode);
		node = nextnode;
	}
	if (result != ISC_R_NOMORE)
		fatal("iterating through the database failed: %s",
		      isc_result_totext(result));
	if (lastcut != NULL) {
		dns_name_free(lastcut, mctx);
		isc_mem_put(mctx, lastcut, sizeof(dns_name_t));
	}
	dns_dbiterator_destroy(&dbiter);
}

static void
loadzone(char *file, char *origin, dns_db_t **db) {
	isc_buffer_t b, b2;
	unsigned char namedata[1024];
	int len;
	dns_name_t name;
	isc_result_t result;

	len = strlen(origin);
	isc_buffer_init(&b, origin, len);
	isc_buffer_add(&b, len);

	isc_buffer_init(&b2, namedata, sizeof(namedata));

	dns_name_init(&name, NULL);
	result = dns_name_fromtext(&name, &b, dns_rootname, ISC_FALSE, &b2);
	if (result != ISC_R_SUCCESS)
		fatal("failed converting name '%s' to dns format: %s",
		      origin, isc_result_totext(result));

	result = dns_db_create(mctx, "rbt", &name, ISC_FALSE,
			       dns_rdataclass_in, 0, NULL, db);
	check_result(result, "dns_db_create()");

	result = dns_db_load(*db, file);
	if (result != ISC_R_SUCCESS)
		fatal("failed loading zone from '%s': %s",
		      file, isc_result_totext(result));
}

static void
getversion(dns_db_t *db, dns_dbversion_t **version) {
	isc_result_t result;

	result = dns_db_newversion(db, version);
	check_result(result, "dns_db_newversion()");
}

/*
 * Finds all public zone keys in the zone, and attempts to load the
 * private keys from disk.
 */
static void
loadzonekeys(dns_db_t *db, dns_dbversion_t *version) {
	dns_name_t *origin;
	dns_dbnode_t *node;
	isc_result_t result;
	dst_key_t *keys[20];
	unsigned int nkeys, i;

	origin = dns_db_origin(db);

	node = NULL;
	result = dns_db_findnode(db, origin, ISC_FALSE, &node);
	if (result != ISC_R_SUCCESS)
		fatal("failed to find the zone's origin: %s",
		      isc_result_totext(result));

	result = dns_dnssec_findzonekeys(db, version, node, origin, mctx,
					 20, keys, &nkeys);
	if (result == ISC_R_NOTFOUND)
		result = ISC_R_SUCCESS;
	if (result != ISC_R_SUCCESS)
		fatal("failed to find the zone keys: %s",
		      isc_result_totext(result));

	for (i = 0; i < nkeys; i++) {
		signer_key_t *key;

		key = isc_mem_get(mctx, sizeof(signer_key_t));
		if (key == NULL)
			fatal("out of memory");
		key->key = keys[i];
		key->isdefault = ISC_FALSE;

		ISC_LIST_APPEND(keylist, key, link);
	}
	dns_db_detachnode(db, &node);
}

static isc_stdtime_t
strtotime(char *str, isc_int64_t now, isc_int64_t base) {
	isc_int64_t val, offset;
	isc_result_t result;
	char *endp = "";

	if (str[0] == '+') {
		offset = strtol(str + 1, &endp, 0);
		val = base + offset;
	}
	else if (strncmp(str, "now+", 4) == 0) {
		offset = strtol(str + 4, &endp, 0);
		val = now + offset;
	}
	else {
		result = dns_time64_fromtext(str, &val);
		if (result != ISC_R_SUCCESS)
			fatal("time %s must be numeric", str);
	}
	if (*endp != '\0')
		fatal("time value %s is invalid", str);

	return ((isc_stdtime_t) val);
}

static void
usage() {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [options] zonefile [keys]\n", PROGRAM);

	fprintf(stderr, "\n");

	fprintf(stderr, "Options: (default value in parenthesis) \n");
	fprintf(stderr, "\t-s YYYYMMDDHHMMSS|+offset:\n");
	fprintf(stderr, "\t\tSIG start time - absolute|offset (now)\n");
	fprintf(stderr, "\t-e YYYYMMDDHHMMSS|+offset|\"now\"+offset]:\n");
	fprintf(stderr, "\t\tSIG end time  - absolute|from start|from now (now + 30 days)\n");
	fprintf(stderr, "\t-c ttl:\n");
	fprintf(stderr, "\t\tcycle period - regenerate "
				"if < cycle from end ( (end-start)/4 )\n");
	fprintf(stderr, "\t-v level:\n");
	fprintf(stderr, "\t\tverbose level (0)\n");
	fprintf(stderr, "\t-o origin:\n");
	fprintf(stderr, "\t\tzone origin (name of zonefile)\n");
	fprintf(stderr, "\t-f outfile:\n");
	fprintf(stderr, "\t\tfile the signed zone is written in " \
			"(zonefile + .signed)\n");
	fprintf(stderr, "\t-a:\n");
	fprintf(stderr, "\t\tverify generated signatures "
					"(if currently valid)\n");

	fprintf(stderr, "\n");

	fprintf(stderr, "Signing Keys: ");
	fprintf(stderr, "(default: all zone keys that have private keys)\n");
	fprintf(stderr, "\tkeyfile (Kname+alg+id)\n");
	exit(0);
}

static void
setup_logging(int level, isc_log_t **logp) {
	isc_result_t result;
	isc_logdestination_t destination;
	isc_logconfig_t *logconfig;
	isc_log_t *log = 0;
	
	RUNTIME_CHECK(isc_log_create(mctx, &log, &logconfig)
		      == ISC_R_SUCCESS);
	isc_log_setcontext(log);
	dns_log_init(log);
	dns_log_setcontext(log);
	
	/*
	 * Set up a channel similar to default_stderr except:
	 *  - the logging level is passed in
	 *  - the logging level is printed
	 *  - no time stamp is printed
	 */
	destination.file.stream = stderr;
	destination.file.name = NULL;
	destination.file.versions = ISC_LOG_ROLLNEVER;
	destination.file.maximum_size = 0;
	result = isc_log_createchannel(logconfig, "stderr",
				       ISC_LOG_TOFILEDESC,
				       level,
				       &destination,
				       ISC_LOG_PRINTLEVEL);
	check_result(result, "isc_log_createchannel()");

	RUNTIME_CHECK(isc_log_usechannel(logconfig, "stderr",
					 NULL, NULL) == ISC_R_SUCCESS);

	*logp = log;
}

int
main(int argc, char *argv[]) {
	int i, ch;
	char *startstr = NULL, *endstr = NULL;
	char *origin = NULL, *file = NULL, *output = NULL;
	char *endp;
	dns_db_t *db;
	dns_dbversion_t *version;
	signer_key_t *key;
	isc_result_t result;
	isc_log_t *log = NULL;
	int loglevel;

	dns_result_register();

	result = isc_mem_create(0, 0, &mctx);
	if (result != ISC_R_SUCCESS)
		fatal("out of memory");

	while ((ch = isc_commandline_parse(argc, argv, "s:e:c:v:o:f:ah"))
	       != -1) {
		switch (ch) {
		case 's':
			startstr = isc_mem_strdup(mctx,
						  isc_commandline_argument);
			if (startstr == NULL)
				fatal("out of memory");
			break;

		case 'e':
			endstr = isc_mem_strdup(mctx,
						isc_commandline_argument);
			if (endstr == NULL)
				fatal("out of memory");
			break;

		case 'c':
			endp = NULL;
			cycle = strtol(isc_commandline_argument, &endp, 0);
			if (*endp != '\0')
				fatal("cycle period must be numeric");
			break;

		case 'v':
			endp = NULL;
			verbose = strtol(isc_commandline_argument, &endp, 0);
			if (*endp != '\0')
				fatal("verbose level must be numeric");
			break;

		case 'o':
			origin = isc_mem_strdup(mctx,
						isc_commandline_argument);
			if (origin == NULL)
				fatal("out of memory");
			break;

		case 'f':
			output = isc_mem_strdup(mctx,
						isc_commandline_argument);
			if (output == NULL)
				fatal("out of memory");
			break;

		case 'a':
			tryverify = ISC_TRUE;
			break;

		case 'h':
		default:
			usage();

		}
	}

	isc_stdtime_get(&now);

	if (startstr != NULL) {
		starttime = strtotime(startstr, now, now);
		isc_mem_free(mctx, startstr);
	}
	else
		starttime = now;

	if (endstr != NULL) {
		endtime = strtotime(endstr, now, starttime);
		isc_mem_free(mctx, endstr);
	}
	else
		endtime = starttime + (30 * 24 * 60 * 60);

	if (cycle == -1) {
		cycle = (endtime - starttime) / 4;
	}

	switch (verbose) {
	case 0:
		/*
		 * We want to see warnings about things like out-of-zone
		 * data in the master file even when not verbose.
		 */
		loglevel = ISC_LOG_WARNING;
		break;
	case 1:
		loglevel = ISC_LOG_INFO;
		break;
	default:
		loglevel = ISC_LOG_DEBUG(verbose - 2 + 1);
		break;
	}
	setup_logging(loglevel, &log);
	
	argc -= isc_commandline_index;
	argv += isc_commandline_index;

	if (argc < 1)
		usage();

	file = isc_mem_strdup(mctx, argv[0]);
	if (file == NULL)
		fatal("out of memory");

	argc -= 1;
	argv += 1;

	if (output == NULL) {
		output = isc_mem_allocate(mctx,
					 strlen(file) + strlen(".signed") + 1);
		if (output == NULL)
			fatal("out of memory");
		sprintf(output, "%s.signed", file);
	}

	if (origin == NULL) {
		origin = isc_mem_allocate(mctx, strlen(file) + 2);
		if (origin == NULL)
			fatal("out of memory");
		strcpy(origin, file);
		if (file[strlen(file) - 1] != '.')
			strcat(origin, ".");
	}

	db = NULL;
	loadzone(file, origin, &db);

	version = NULL;
	getversion(db, &version);

	ISC_LIST_INIT(keylist);
	loadzonekeys(db, version);

	if (argc == 0) {
		signer_key_t *key;

		key = ISC_LIST_HEAD(keylist);
		while (key != NULL) {
			key->isdefault = ISC_TRUE;
			key = ISC_LIST_NEXT(key, link);
		}
	}
	else {
		for (i = 0; i < argc; i++) {
			isc_uint16_t id;
			int alg;
			char *namestr = NULL;
			isc_buffer_t b;

			isc_buffer_init(&b, argv[i], strlen(argv[i]));
			isc_buffer_add(&b, strlen(argv[i]));
			result = dst_key_parsefilename(&b, mctx, &namestr,
						       &id, &alg, NULL);
			if (result != ISC_R_SUCCESS)
				usage();

			key = ISC_LIST_HEAD(keylist);
			while (key != NULL) {
				dst_key_t *dkey = key->key;
				if (dst_key_id(dkey) == id &&
				    dst_key_alg(dkey) == alg &&
				    strcasecmp(namestr,
					       dst_key_name(dkey)) == 0)
				{
					key->isdefault = ISC_TRUE;
					if (!dst_key_isprivate(dkey))
						fatal("cannot sign zone with "
						      "non-private key "
						      "'%s/%s/%d'",
						      dst_key_name(dkey),
						      algtostr(dst_key_alg(dkey)),
						      dst_key_id(dkey));
					break;
				}
				key = ISC_LIST_NEXT(key, link);
			}
			if (key == NULL) {
				dst_key_t *dkey = NULL;
				result = dst_key_fromfile(namestr, id, alg,
							  DST_TYPE_PRIVATE,
							  mctx, &dkey);
				if (result != ISC_R_SUCCESS)
					fatal("failed to load key '%s/%s/%d' "
					      "from disk: %s", namestr,
					      algtostr(alg), id,
					      isc_result_totext(result));
				key = isc_mem_get(mctx, sizeof(signer_key_t));
				if (key == NULL)
					fatal("out of memory");
				key->key = dkey;
				key->isdefault = ISC_TRUE;
				ISC_LIST_APPEND(keylist, key, link);
			}
		isc_mem_put(mctx, namestr, strlen(namestr) + 1);
		}
	}

	signzone(db, version);

	/*
	 * Should we update the SOA serial?
	 */

	result = dns_db_dump(db, version, output);
	if (result != ISC_R_SUCCESS)
		fatal("failed to write new database to '%s': %s",
		      output, isc_result_totext(result));
	dns_db_closeversion(db, &version, ISC_TRUE);

	dns_db_detach(&db);

	key = ISC_LIST_HEAD(keylist);
	while (key != NULL) {
		signer_key_t *next = ISC_LIST_NEXT(key, link);
		dst_key_free(&key->key);
		isc_mem_put(mctx, key, sizeof(signer_key_t));
		key = next;
	}

	isc_mem_free(mctx, origin);
	isc_mem_free(mctx, file);
	isc_mem_free(mctx, output);

	if (log != NULL)
		isc_log_destroy(&log);
/*	isc_mem_stats(mctx, stdout);*/
	isc_mem_destroy(&mctx);

	return (0);
}