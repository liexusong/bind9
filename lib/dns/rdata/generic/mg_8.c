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

/* $Id: mg_8.c,v 1.27 2000/05/22 12:37:43 marka Exp $ */

/* reviewed: Wed Mar 15 17:49:21 PST 2000 by brister */

#ifndef RDATA_GENERIC_MG_8_C
#define RDATA_GENERIC_MG_8_C

#define RRTYPE_MG_ATTRIBUTES (0)

static inline isc_result_t
fromtext_mg(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	    isc_lex_t *lexer, dns_name_t *origin,
	    isc_boolean_t downcase, isc_buffer_t *target)
{
	isc_token_t token;
	dns_name_t name;
	isc_buffer_t buffer;

	REQUIRE(type == 8);

	UNUSED(rdclass);
	
	RETERR(gettoken(lexer, &token, isc_tokentype_string, ISC_FALSE));

	dns_name_init(&name, NULL);
	buffer_fromregion(&buffer, &token.value.as_region);
	origin = (origin != NULL) ? origin : dns_rootname;
	return (dns_name_fromtext(&name, &buffer, origin, downcase, target));
}

static inline isc_result_t
totext_mg(dns_rdata_t *rdata, dns_rdata_textctx_t *tctx, 
	  isc_buffer_t *target) 
{
	isc_region_t region;
	dns_name_t name;
	dns_name_t prefix;
	isc_boolean_t sub;

	REQUIRE(rdata->type == 8);

	dns_name_init(&name, NULL);
	dns_name_init(&prefix, NULL);

	dns_rdata_toregion(rdata, &region);
	dns_name_fromregion(&name, &region);

	sub = name_prefix(&name, tctx->origin, &prefix);

	return (dns_name_totext(&prefix, sub, target));
}

static inline isc_result_t
fromwire_mg(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	    isc_buffer_t *source, dns_decompress_t *dctx,
	    isc_boolean_t downcase, isc_buffer_t *target)
{
        dns_name_t name;

	REQUIRE(type == 8);

	UNUSED(rdclass);

	dns_decompress_setmethods(dctx, DNS_COMPRESS_GLOBAL14);
        
        dns_name_init(&name, NULL);
        return (dns_name_fromwire(&name, source, dctx, downcase, target));
}

static inline isc_result_t
towire_mg(dns_rdata_t *rdata, dns_compress_t *cctx, isc_buffer_t *target)
{
	dns_name_t name;
	isc_region_t region;

	REQUIRE(rdata->type == 8);

	dns_compress_setmethods(cctx, DNS_COMPRESS_GLOBAL14);

	dns_name_init(&name, NULL);
	dns_rdata_toregion(rdata, &region);
	dns_name_fromregion(&name, &region);

	return (dns_name_towire(&name, cctx, target));
}

static inline int
compare_mg(dns_rdata_t *rdata1, dns_rdata_t *rdata2)
{
	dns_name_t name1;
	dns_name_t name2;
	isc_region_t region1;
	isc_region_t region2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == 8);

	dns_name_init(&name1, NULL);
	dns_name_init(&name2, NULL);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);

	return (dns_name_rdatacompare(&name1, &name2));
}

static inline isc_result_t
fromstruct_mg(dns_rdataclass_t rdclass, dns_rdatatype_t type, void *source,
	      isc_buffer_t *target)
{
	dns_rdata_mg_t *mg = source;
	isc_region_t region;

	REQUIRE(type == 8);
	REQUIRE(source != NULL);
	REQUIRE(mg->common.rdtype == type);
	REQUIRE(mg->common.rdclass == rdclass);

	dns_name_toregion(&mg->mg, &region);
	return (isc_buffer_copyregion(target, &region));
}

static inline isc_result_t
tostruct_mg(dns_rdata_t *rdata, void *target, isc_mem_t *mctx)
{
	isc_region_t region;
	dns_rdata_mg_t *mg = target;
	dns_name_t name;

	REQUIRE(rdata->type == 8);
	REQUIRE(target != NULL);

	mg->common.rdclass = rdata->rdclass;
	mg->common.rdtype = rdata->type;
	ISC_LINK_INIT(&mg->common, link);

	dns_name_init(&name, NULL);
	dns_rdata_toregion(rdata, &region);
	dns_name_fromregion(&name, &region);
	dns_name_init(&mg->mg, NULL);
	RETERR(name_duporclone(&name, mctx, &mg->mg));
	mg->mctx = mctx;
	return (ISC_R_SUCCESS);
}

static inline void
freestruct_mg(void *source)
{
	dns_rdata_mg_t *mg = source;

	REQUIRE(source != NULL);
	REQUIRE(mg->common.rdtype == 8);
	
	if (mg->mctx == NULL)
		return;
	dns_name_free(&mg->mg, mg->mctx);
	mg->mctx = NULL;
}

static inline isc_result_t
additionaldata_mg(dns_rdata_t *rdata, dns_additionaldatafunc_t add,
		  void *arg)
{
	REQUIRE(rdata->type == 8);

	UNUSED(add);
	UNUSED(arg);
	UNUSED(rdata);

	return (ISC_R_SUCCESS);
}

static inline isc_result_t
digest_mg(dns_rdata_t *rdata, dns_digestfunc_t digest, void *arg)
{
	isc_region_t r;
	dns_name_t name;

	REQUIRE(rdata->type == 8);

	dns_rdata_toregion(rdata, &r);
	dns_name_init(&name, NULL);
	dns_name_fromregion(&name, &r);

	return (dns_name_digest(&name, digest, arg));
}

#endif	/* RDATA_GENERIC_MG_8_C */