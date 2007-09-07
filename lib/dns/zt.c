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

#include <isc/assertions.h>
#include <isc/magic.h>
#include <isc/rwlock.h>
#include <isc/result.h>
#include <isc/util.h>

#include <dns/zt.h>
#include <dns/zone.h>

struct dns_zt {
	/* Unlocked. */
        unsigned int		magic;
	isc_mem_t		*mctx;
	dns_rdataclass_t	rdclass;
	isc_rwlock_t		rwlock;
	/* Locked by lock. */
	isc_uint32_t		references;
	dns_rbt_t		*table; 
};

#define ZTMAGIC			0x5a54626cU	/* ZTbl */
#define VALID_ZT(zt) 		ISC_MAGIC_VALID(zt, ZTMAGIC)

static void auto_detach(void *, void *);
static isc_result_t load(dns_zone_t *zone, void *uap);

isc_result_t
dns_zt_create(isc_mem_t *mctx, dns_rdataclass_t rdclass, dns_zt_t **ztp) {
	dns_zt_t *zt;
	isc_result_t result;

	REQUIRE(ztp != NULL && *ztp == NULL);

	zt = isc_mem_get(mctx, sizeof *zt);
	if (zt == NULL)
		return (DNS_R_NOMEMORY);

	zt->table = NULL;
	result = dns_rbt_create(mctx, auto_detach, NULL, &zt->table);
	if (result != ISC_R_SUCCESS)
		goto cleanup_zt;

	result = isc_rwlock_init(&zt->rwlock, 0, 0);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_rwlock_init() failed: %s",
				 isc_result_totext(result));
		result = ISC_R_UNEXPECTED;
		goto cleanup_rbt;
	}

	zt->mctx = mctx;
	zt->references = 1;
	zt->rdclass = rdclass;
	zt->magic = ZTMAGIC;
	*ztp = zt;

	return (ISC_R_SUCCESS);

   cleanup_rbt:
	dns_rbt_destroy(&zt->table);

   cleanup_zt:
	isc_mem_put(mctx, zt, sizeof *zt);

	return (result);
}

isc_result_t
dns_zt_mount(dns_zt_t *zt, dns_zone_t *zone) {
	isc_result_t result;
	dns_zone_t *dummy = NULL;
	dns_name_t *name;

	REQUIRE(VALID_ZT(zt));

	name = dns_zone_getorigin(zone);

	RWLOCK(&zt->rwlock, isc_rwlocktype_write);

	result = dns_rbt_addname(zt->table, name, zone);
	if (result == ISC_R_SUCCESS)
		dns_zone_attach(zone, &dummy);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_write);

	return (result);
}

isc_result_t
dns_zt_unmount(dns_zt_t *zt, dns_zone_t *zone) {
	isc_result_t result;
	dns_name_t *name;

	REQUIRE(VALID_ZT(zt));

	name = dns_zone_getorigin(zone);

	RWLOCK(&zt->rwlock, isc_rwlocktype_write);

	result = dns_rbt_deletename(zt->table, name, ISC_FALSE);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_write);

	return (result);
}

isc_result_t
dns_zt_find(dns_zt_t *zt, dns_name_t *name, dns_name_t *foundname,
	    dns_zone_t **zonep)
{
	isc_result_t result;
	dns_zone_t *dummy = NULL;

	REQUIRE(VALID_ZT(zt));

	RWLOCK(&zt->rwlock, isc_rwlocktype_read);

	result = dns_rbt_findname(zt->table, name, foundname, (void **)&dummy);
	if (result == DNS_R_SUCCESS || result == DNS_R_PARTIALMATCH)
		dns_zone_attach(dummy, zonep);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_read);

	return (result);
}

void
dns_zt_attach(dns_zt_t *zt, dns_zt_t **ztp) {

	REQUIRE(VALID_ZT(zt));
	REQUIRE(ztp != NULL && *ztp == NULL);

	RWLOCK(&zt->rwlock, isc_rwlocktype_write);

	INSIST(zt->references > 0);
	zt->references++;
	INSIST(zt->references != 0);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_write);

	*ztp = zt;
}

void
dns_zt_detach(dns_zt_t **ztp) {
	isc_boolean_t destroy = ISC_FALSE;
	dns_zt_t *zt;

	REQUIRE(ztp != NULL && VALID_ZT(*ztp));

	zt = *ztp;

	RWLOCK(&zt->rwlock, isc_rwlocktype_write);
	
	INSIST(zt->references > 0);
	zt->references--;
	if (zt->references == 0)
		destroy = ISC_TRUE;

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_write);

	if (destroy) {
		dns_rbt_destroy(&zt->table);
		isc_rwlock_destroy(&zt->rwlock);
		zt->magic = 0;
		isc_mem_put(zt->mctx, zt, sizeof *zt);
	}

	*ztp = NULL;
}

void
dns_zt_print(dns_zt_t *zt) {
	dns_rbtnode_t *node;
	dns_rbtnodechain_t chain;
	isc_result_t result;
	dns_zone_t *zone;

	REQUIRE(VALID_ZT(zt));

	RWLOCK(&zt->rwlock, isc_rwlocktype_read);

	dns_rbtnodechain_init(&chain, zt->mctx);
	result = dns_rbtnodechain_first(&chain, zt->table, NULL, NULL);
	while (result == DNS_R_NEWORIGIN || result == DNS_R_SUCCESS) {
		result = dns_rbtnodechain_current(&chain, NULL, NULL,
						  &node);
		if (result == DNS_R_SUCCESS) {
			zone = node->data;
			if (zone != NULL)
				(void)dns_zone_print(zone);
		}
		result = dns_rbtnodechain_next(&chain, NULL, NULL);
	}
	dns_rbtnodechain_invalidate(&chain);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_read);
}

isc_result_t
dns_zt_load(dns_zt_t *zt, isc_boolean_t stop) {
	return (dns_zt_apply(zt, stop, load, NULL));
}

static isc_result_t
load(dns_zone_t *zone, void *uap) {
	uap = uap;
	return (dns_zone_load(zone));
}

isc_result_t
dns_zt_apply(dns_zt_t *zt, isc_boolean_t stop,
	     isc_result_t (*action)(dns_zone_t *, void *), void *uap)
{
	dns_rbtnode_t *node;
	dns_rbtnodechain_t chain;
	isc_result_t result;
	dns_zone_t *zone;

	REQUIRE(VALID_ZT(zt));
	REQUIRE(action != NULL);

	RWLOCK(&zt->rwlock, isc_rwlocktype_read);

	dns_rbtnodechain_init(&chain, zt->mctx);
	result = dns_rbtnodechain_first(&chain, zt->table, NULL, NULL);
	if (result == DNS_R_NOTFOUND) {
		/*
		 * The tree is empty.
		 */
		result = DNS_R_NOMORE; 
	}
	while (result == DNS_R_NEWORIGIN || result == DNS_R_SUCCESS) {
		result = dns_rbtnodechain_current(&chain, NULL, NULL,
						  &node);
		if (result == DNS_R_SUCCESS) {
			zone = node->data;
			if (zone != NULL)
				result = (action)(zone, uap);
			if (result != DNS_R_SUCCESS && stop)
				goto cleanup;	/* don't break */
		}
		result = dns_rbtnodechain_next(&chain, NULL, NULL);
	}
	if (result == DNS_R_NOMORE)
		result = DNS_R_SUCCESS;

 cleanup:
	dns_rbtnodechain_invalidate(&chain);

	RWUNLOCK(&zt->rwlock, isc_rwlocktype_read);

	return (result);
}

/***
 *** Private
 ***/

static void
auto_detach(void *data, void *arg) {
	dns_zone_t *zone = data;

	(void)arg;

	dns_zone_detach(&zone);
}
