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

/* $Id: txt_16.c,v 1.26 2000/05/22 12:38:00 marka Exp $ */

/* Reviewed: Thu Mar 16 15:40:00 PST 2000 by bwelling */

#ifndef RDATA_GENERIC_TXT_16_C
#define RDATA_GENERIC_TXT_16_C

#define RRTYPE_TXT_ATTRIBUTES (0)

static inline isc_result_t
fromtext_txt(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	     isc_lex_t *lexer, dns_name_t *origin,
	     isc_boolean_t downcase, isc_buffer_t *target)
{
	isc_token_t token;

	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(downcase);

	REQUIRE(type == 16);

	for(;;) {
		RETERR(gettoken(lexer, &token, isc_tokentype_qstring,
				ISC_TRUE));
		if (token.type != isc_tokentype_qstring &&
		    token.type != isc_tokentype_string)
			break;
		RETERR(txt_fromtext(&token.value.as_textregion, target));
	}
	/* Let upper layer handle eol/eof. */
	isc_lex_ungettoken(lexer, &token);
	return (ISC_R_SUCCESS);
}

static inline isc_result_t
totext_txt(dns_rdata_t *rdata, dns_rdata_textctx_t *tctx, 
	   isc_buffer_t *target) 
{
	isc_region_t region;

	UNUSED(tctx);

	REQUIRE(rdata->type == 16);

	dns_rdata_toregion(rdata, &region);

	while (region.length > 0) {
		RETERR(txt_totext(&region, target));
		if (region.length > 0)
			RETERR(str_totext(" ", target));
	}

	return (ISC_R_SUCCESS);
}

static inline isc_result_t
fromwire_txt(dns_rdataclass_t rdclass, dns_rdatatype_t type,
	     isc_buffer_t *source, dns_decompress_t *dctx,
	     isc_boolean_t downcase, isc_buffer_t *target)
{
	isc_result_t result;

	UNUSED(dctx);
	UNUSED(rdclass);
	UNUSED(downcase);

	REQUIRE(type == 16);

	while (!buffer_empty(source)) {
		result = txt_fromwire(source, target);
		if (result != ISC_R_SUCCESS)
			return (result);
	}
	return (ISC_R_SUCCESS);
}

static inline isc_result_t
towire_txt(dns_rdata_t *rdata, dns_compress_t *cctx, isc_buffer_t *target) {
	isc_region_t region;

	REQUIRE(rdata->type == 16);

	UNUSED(cctx);

	isc_buffer_availableregion(target, &region);
	if (region.length < rdata->length)
		return (ISC_R_NOSPACE);

	memcpy(region.base, rdata->data, rdata->length);
	isc_buffer_add(target, rdata->length);
	return (ISC_R_SUCCESS);
}

static inline int
compare_txt(dns_rdata_t *rdata1, dns_rdata_t *rdata2) {
	isc_region_t r1;
	isc_region_t r2;
	
	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == 16);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return (compare_region(&r1, &r2));
}

static inline isc_result_t
fromstruct_txt(dns_rdataclass_t rdclass, dns_rdatatype_t type, void *source,
	       isc_buffer_t *target)
{
	dns_rdata_txt_t *txt = source;
	isc_region_t region;
	isc_uint8_t length;

	REQUIRE(type == 16);
	REQUIRE(source != NULL);
	REQUIRE(txt->common.rdtype == type);
	REQUIRE(txt->common.rdclass == rdclass);
	REQUIRE((txt->txt == NULL && txt->txt_len == 0) ||
		(txt->txt != NULL && txt->txt_len != 0));

	region.base = txt->txt;
	region.length = txt->txt_len;
	while (region.length > 0) {
		length = uint8_fromregion(&region);
		isc_region_consume(&region, 1);
		if (region.length <= length)
			return (ISC_R_UNEXPECTEDEND);
		isc_region_consume(&region, length);
	}

	return (mem_tobuffer(target, txt->txt, txt->txt_len));
}

static inline isc_result_t
tostruct_txt(dns_rdata_t *rdata, void *target, isc_mem_t *mctx) {
	dns_rdata_txt_t *txt = target;
	isc_region_t r;

	REQUIRE(rdata->type == 16);
	REQUIRE(target != NULL);

	txt->common.rdclass = rdata->rdclass;
	txt->common.rdtype = rdata->type;
	ISC_LINK_INIT(&txt->common, link);

	dns_rdata_toregion(rdata, &r);
	txt->txt_len = r.length;
	if (txt->txt_len != 0) {
		txt->txt = mem_maybedup(mctx, r.base, r.length);
		if (txt->txt == NULL)
			return (ISC_R_NOMEMORY);
	} else
		txt->txt = NULL;

	txt->offset = 0;
	txt->mctx = mctx;
	return (ISC_R_SUCCESS);
}

static inline void
freestruct_txt(void *source) {
	dns_rdata_txt_t *txt = source;

	REQUIRE(source != NULL);
	REQUIRE(txt->common.rdtype == 16);

	if (txt->mctx == NULL)
		return;

	if (txt->txt != NULL)
		isc_mem_free(txt->mctx, txt->txt);
	txt->mctx = NULL;
}

static inline isc_result_t
additionaldata_txt(dns_rdata_t *rdata, dns_additionaldatafunc_t add,
		   void *arg)
{
	REQUIRE(rdata->type == 16);

	UNUSED(rdata);
	UNUSED(add);
	UNUSED(arg);
	
	return (ISC_R_SUCCESS);
}

static inline isc_result_t
digest_txt(dns_rdata_t *rdata, dns_digestfunc_t digest, void *arg) {
	isc_region_t r;

	REQUIRE(rdata->type == 16);

	dns_rdata_toregion(rdata, &r);

	return ((digest)(arg, &r));
}

#endif	/* RDATA_GENERIC_TXT_16_C */