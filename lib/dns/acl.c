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

#include <isc/mem.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/acl.h>

isc_result_t
dns_acl_create(isc_mem_t *mctx, int n, dns_acl_t **target) {
	isc_result_t result;
	dns_acl_t *acl;

	/*
	 * Work around silly limitation of isc_mem_get().
	 */
	if (n == 0)
		n = 1;
	
	acl = isc_mem_get(mctx, sizeof(*acl));
	if (acl == NULL)
		return (ISC_R_NOMEMORY);
	acl->mctx = mctx;
	acl->name = NULL;
	acl->refcount = 1;
	acl->elements = NULL;
	acl->alloc = 0;
	acl->length = 0;
	
	ISC_LINK_INIT(acl, nextincache);
	/*
	 * Must set magic early because we use dns_acl_detach() to clean up.
	 */
	acl->magic = DNS_ACL_MAGIC; 

	acl->elements = isc_mem_get(mctx, n * sizeof(dns_aclelement_t));
	if (acl->elements == NULL) {
		result = ISC_R_NOMEMORY;
		goto cleanup;
	}
	acl->alloc = n;
	memset(acl->elements, 0, n * sizeof(dns_aclelement_t));
	*target = acl;
	return (ISC_R_SUCCESS);

 cleanup:
	dns_acl_detach(&acl);
	return (result);
}

isc_result_t
dns_acl_appendelement(dns_acl_t *acl, dns_aclelement_t *elt) {
	if (acl->length + 1 > acl->alloc) {
		/*
		 * Resize the ACL.
		 */
		unsigned int newalloc;
		void *newmem;

		newalloc = acl->alloc * 2;
		if (newalloc < 4)
			newalloc = 4;
		newmem = isc_mem_get(acl->mctx,
				     newalloc * sizeof(dns_aclelement_t));
		if (newmem == NULL)
			return (ISC_R_NOMEMORY);
		memcpy(newmem, acl->elements,
		       acl->length * sizeof(dns_aclelement_t));
		isc_mem_put(acl->mctx, acl->elements,
			    acl->alloc * sizeof(dns_aclelement_t));
		acl->elements = newmem;
		acl->alloc = newalloc;
	}
	/*
	 * Append the new element.
	 */
	acl->elements[acl->length++] = *elt;
	
	return (ISC_R_SUCCESS);
}
		       
static isc_result_t
dns_acl_anyornone(isc_mem_t *mctx, isc_boolean_t neg, dns_acl_t **target) {
	isc_result_t result;
	dns_acl_t *acl = NULL;
	result = dns_acl_create(mctx, 1, &acl);
	if (result != ISC_R_SUCCESS)
		return (result);
	acl->elements[0].negative = neg;
	acl->elements[0].type = dns_aclelementtype_any;
	acl->length = 1;
	*target = acl;
	return (result);
}

isc_result_t
dns_acl_any(isc_mem_t *mctx, dns_acl_t **target) {
	return (dns_acl_anyornone(mctx, ISC_FALSE, target));
}

isc_result_t
dns_acl_none(isc_mem_t *mctx, dns_acl_t **target) {
	return (dns_acl_anyornone(mctx, ISC_TRUE, target));
}

isc_result_t
dns_acl_match(isc_netaddr_t *reqaddr,
	      dns_name_t *reqsigner,
	      dns_acl_t *acl,
	      dns_aclenv_t *env,
	      int *match,
	      dns_aclelement_t **matchelt)
{
	isc_result_t result;
	unsigned int i;
	int indirectmatch;

	REQUIRE(reqaddr != NULL);
	REQUIRE(matchelt == NULL || *matchelt == NULL);
	
	for (i = 0; i < acl->length; i++) {
		dns_aclelement_t *e = &acl->elements[i];
		dns_acl_t *inner = NULL;
		
		switch (e->type) {
		case dns_aclelementtype_ipprefix:
			if (isc_netaddr_eqprefix(reqaddr,
						 &e->u.ip_prefix.address, 
						 e->u.ip_prefix.prefixlen))
				goto matched;
			break;
			
		case dns_aclelementtype_keyname:
			if (reqsigner != NULL &&
			    dns_name_equal(reqsigner, &e->u.keyname))
			    goto matched;
			break;
		
		case dns_aclelementtype_nestedacl:
			inner = e->u.nestedacl;
		nested:
			result = dns_acl_match(reqaddr, reqsigner,
					       inner,
					       env,
					       &indirectmatch, matchelt);
			if (result != ISC_R_SUCCESS)
				return (result);
			/*
			 * Treat negative matches in indirect ACLs as
			 * "no match".
			 * That way, a negated indirect ACL will never become 
			 * a surprise positive match through double negation.
			 * XXXDCL this should be documented.
			 */
			if (indirectmatch > 0)
				goto matched;

			/*
			 * A negative indirect match may have set *matchelt,
			 * but we don't want it set when we return.
			 */
			if (matchelt != NULL)
				*matchelt = NULL;
			break;

		case dns_aclelementtype_any:
		matched:
			*match = e->negative ? -(i+1) : (i+1);
			if (matchelt != NULL)
				*matchelt = e;
			return (ISC_R_SUCCESS);

		case dns_aclelementtype_localhost:
			if (env != NULL && env->localhost != NULL) {
				inner = env->localhost;
				goto nested;
			} else {
				break;
			}
			
		case dns_aclelementtype_localnets:
			if (env != NULL && env->localnets != NULL) {
				inner = env->localnets;
				goto nested;
			} else {
				break;
			}
			
		default:
			INSIST(0);
			break;
		}
	}
	/* No match. */
	*match = 0;
	return (ISC_R_SUCCESS);
}

void
dns_acl_attach(dns_acl_t *source, dns_acl_t **target) {
	REQUIRE(DNS_ACL_VALID(source));
	INSIST(source->refcount > 0);
	source->refcount++;
	*target = source;
}

static void
destroy(dns_acl_t *dacl) {
	unsigned int i;
	for (i = 0; i < dacl->length; i++) {
		dns_aclelement_t *de = &dacl->elements[i];
		switch (de->type) {
		case dns_aclelementtype_keyname:
			dns_name_free(&de->u.keyname, dacl->mctx);
			break;
		case dns_aclelementtype_nestedacl:
			dns_acl_detach(&de->u.nestedacl);
			break;
		default:
			break;
		}
	}
	if (dacl->elements != NULL)
		isc_mem_put(dacl->mctx, dacl->elements,
			    dacl->alloc * sizeof(dns_aclelement_t));
	dacl->magic = 0;
	isc_mem_put(dacl->mctx, dacl, sizeof(*dacl));
}

void
dns_acl_detach(dns_acl_t **aclp) {
	dns_acl_t *acl = *aclp;
	REQUIRE(DNS_ACL_VALID(acl));
	INSIST(acl->refcount > 0);
	acl->refcount--;
	if (acl->refcount == 0)
		destroy(acl);
	*aclp = NULL;
}

isc_boolean_t
dns_aclelement_equal(dns_aclelement_t *ea, dns_aclelement_t *eb) {
	if (ea->type != eb->type)
		return (ISC_FALSE);
	switch (ea->type) {
	case dns_aclelementtype_ipprefix:
		if (ea->u.ip_prefix.prefixlen !=
		    eb->u.ip_prefix.prefixlen)
			return (ISC_FALSE);
		return (isc_netaddr_equal(&ea->u.ip_prefix.address,
					  &eb->u.ip_prefix.address));
	case dns_aclelementtype_keyname:
		return (dns_name_equal(&ea->u.keyname, &eb->u.keyname));
	case dns_aclelementtype_nestedacl:
		return (dns_acl_equal(ea->u.nestedacl, eb->u.nestedacl));
	case dns_aclelementtype_localhost:
	case dns_aclelementtype_localnets:
	case dns_aclelementtype_any:
		return (ISC_TRUE);
	default:
		INSIST(0);
		return (ISC_FALSE);
	}
}

isc_boolean_t
dns_acl_equal(dns_acl_t *a, dns_acl_t *b) {
	unsigned int i;
	if (a == b)
		return (ISC_TRUE);
	if (a->length != b->length)
		return (ISC_FALSE);
	for (i = 0; i < a->length; i++) {
		if (! dns_aclelement_equal(&a->elements[i],
					   &b->elements[i]))
			return (ISC_FALSE);
	}
	return (ISC_TRUE);
}

isc_result_t
dns_aclenv_init(isc_mem_t *mctx, dns_aclenv_t *env) {
	isc_result_t result;
	env->localhost = NULL;
	env->localnets = NULL;
	result = dns_acl_create(mctx, 0, &env->localhost);
	if (result != ISC_R_SUCCESS)
		goto cleanup_nothing;
	result = dns_acl_create(mctx, 0, &env->localnets);
	if (result != ISC_R_SUCCESS)
		goto cleanup_localhost;
	return (ISC_R_SUCCESS);

 cleanup_localhost:
	dns_acl_detach(&env->localhost);
 cleanup_nothing:
	return (result);
}

void
dns_aclenv_copy(dns_aclenv_t *t, dns_aclenv_t *s) {
	dns_acl_detach(&t->localhost);
	dns_acl_attach(s->localhost, &t->localhost);
	dns_acl_detach(&t->localnets);
	dns_acl_attach(s->localnets, &t->localnets);
}

void
dns_aclenv_destroy(dns_aclenv_t *env) {
	dns_acl_detach(&env->localhost);
	dns_acl_detach(&env->localnets);
}