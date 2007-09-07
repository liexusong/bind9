/*
 * Copyright (C) 2000  Internet Software Consortium.
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

#ifndef NAMED_LISTENLIST_H
#define NAMED_LISTENLIST_H 1

/*****
 ***** Module Info
 *****/

/*
 * "Listen lists", as in the "listen-on" configuration statement.
 */

/***
 *** Imports
 ***/
#include <isc/net.h>

#include <dns/types.h>

/***
 *** Types
 ***/

typedef struct ns_listenelt ns_listenelt_t;
typedef struct ns_listenlist ns_listenlist_t;

struct ns_listenelt {
	isc_mem_t *	       		mctx;
	in_port_t			port;
	dns_acl_t *	       		acl;
	ISC_LINK(ns_listenelt_t)	link;
};

struct ns_listenlist {
	isc_mem_t *			mctx;
	int				refcount;
	ISC_LIST(ns_listenelt_t)	elts;
};

/***
 *** Functions
 ***/

isc_result_t
ns_listenelt_create(isc_mem_t *mctx, in_port_t port,
		    dns_acl_t *acl, ns_listenelt_t **target);

void
ns_listenelt_destroy(ns_listenelt_t *elt);

isc_result_t
ns_listenlist_create(isc_mem_t *mctx, ns_listenlist_t **target);

void
ns_listenlist_attach(ns_listenlist_t *source, ns_listenlist_t **target);

void
ns_listenlist_detach(ns_listenlist_t **listp);

isc_result_t
ns_listenlist_default(isc_mem_t *mctx, in_port_t port,
		      ns_listenlist_t **target);
/*
 * Create a listen-on list with default contents, matching
 * all addresses with port 'port'.
 */

#endif /* NAMED_LISTENLIST_H */

