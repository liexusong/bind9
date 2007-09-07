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

/* $Id: null_10.c,v 1.24 2000/05/22 12:37:49 marka Exp $ */

/* Reviewed: Thu Mar 16 13:57:50 PST 2000 by explorer */

#ifndef RDATA_GENERIC_NULL_10_C
#define RDATA_GENERIC_NULL_10_C

#define RRTYPE_NULL_ATTRIBUTES (0)

static inline isc_result_t
fromtext_null(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	      isc_lex_t *lexer, dns_name_t *origin,
	      isc_boolean_t downcase, isc_buffer_t *target)
{
	UNUSED(rdclass);
	UNUSED(type);
	UNUSED(lexer);
	UNUSED(origin);
	UNUSED(downcase);
	UNUSED(target);

	REQUIRE(type == 10);

	return (DNS_R_SYNTAX);
}

static inline isc_result_t
totext_null(dns_rdata_t *rdata, dns_rdata_textctx_t *tctx, 
	    isc_buffer_t *target) 
{
	UNUSED(rdata);
	UNUSED(tctx);
	UNUSED(target);

	REQUIRE(rdata->type == 10);

	return (DNS_R_SYNTAX);
}

static inline isc_result_t
fromwire_null(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	      isc_buffer_t *source, dns_decompress_t *dctx,
	      isc_boolean_t downcase, isc_buffer_t *target)
{
	isc_region_t sr;

	UNUSED(rdclass);
	UNUSED(dctx);
	UNUSED(downcase);

	REQUIRE(type == 10);

	isc_buffer_activeregion(source, &sr);
	isc_buffer_forward(source, sr.length);
	return (mem_tobuffer(target, sr.base, sr.length));
}

static inline isc_result_t
towire_null(dns_rdata_t *rdata, dns_compress_t *cctx, isc_buffer_t *target)
{
	UNUSED(cctx);

	REQUIRE(rdata->type == 10);

	return (mem_tobuffer(target, rdata->data, rdata->length));
}

static inline int
compare_null(dns_rdata_t *rdata1, dns_rdata_t *rdata2)
{
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata1->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == 10);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return (compare_region(&r1, &r2));
}

static inline isc_result_t
fromstruct_null(dns_rdataclass_t rdclass, dns_rdatatype_t type, void *source,
	        isc_buffer_t *target)
{
	dns_rdata_null_t *null = source;

	REQUIRE(type == 10);
	REQUIRE(source != NULL);
	REQUIRE(null->common.rdtype == type);
	REQUIRE(null->common.rdclass == rdclass);
	REQUIRE((null->data != NULL && null->length != 0) ||
		(null->data == NULL && null->length == 0));

	return (mem_tobuffer(target, null->data, null->length));
}

static inline isc_result_t
tostruct_null(dns_rdata_t *rdata, void *target, isc_mem_t *mctx)
{
	dns_rdata_null_t *null = target;
	isc_region_t r;

	REQUIRE(rdata->type == 10);
	REQUIRE(target != NULL);

	null->common.rdclass = rdata->rdclass;
	null->common.rdtype = rdata->type;
	ISC_LINK_INIT(&null->common, link);

	dns_rdata_toregion(rdata, &r);
	null->length = r.length;
	if (null->length != 0) {
		null->data = mem_maybedup(mctx, r.base, r.length);
		if (null->data == NULL)
			return (ISC_R_NOMEMORY);
	} else
		null->data = NULL;

	null->mctx = mctx;
	return (ISC_R_SUCCESS);
}

static inline void
freestruct_null(void *source)
{
	dns_rdata_null_t *null = source;
	
	REQUIRE(source != NULL);
	REQUIRE(null->common.rdtype == 10);

	if (null->mctx == NULL)
		return;

	if (null->data != NULL)
		isc_mem_free(null->mctx, null->data);
	null->mctx = NULL;
}

static inline isc_result_t
additionaldata_null(dns_rdata_t *rdata, dns_additionaldatafunc_t add,
		    void *arg)
{
	UNUSED(rdata);
	UNUSED(add);
	UNUSED(arg);

	REQUIRE(rdata->type == 10);

	return (ISC_R_SUCCESS);
}

static inline isc_result_t
digest_null(dns_rdata_t *rdata, dns_digestfunc_t digest, void *arg)
{
	isc_region_t r;

	REQUIRE(rdata->type == 10);

	dns_rdata_toregion(rdata, &r);

	return ((digest)(arg, &r));
}

#endif	/* RDATA_GENERIC_NULL_10_C */