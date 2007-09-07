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

#include <config.h>

#include <isc/buffer.h>
#include <isc/mem.h>
#include <isc/region.h>
#include <isc/string.h>
#include <isc/util.h>

void
isc__buffer_init(isc_buffer_t *b, void *base, unsigned int length) {
	/*
	 * Make 'b' refer to the 'length'-byte region starting at base.
	 */

	REQUIRE(b != NULL);

	ISC__BUFFER_INIT(b, base, length);
}

void
isc__buffer_invalidate(isc_buffer_t *b) {
	/*
	 * Make 'b' an invalid buffer.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(!ISC_LINK_LINKED(b, link));
	REQUIRE(b->mctx == NULL);
	
	ISC__BUFFER_INVALIDATE(b);
}

void
isc__buffer_region(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_REGION(b, r);
}

void
isc__buffer_usedregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the used region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_USEDREGION(b, r);
}

void
isc__buffer_availableregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the available region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_AVAILABLEREGION(b, r);
}

void
isc__buffer_add(isc_buffer_t *b, unsigned int n) {
	/*
	 * Increase the 'used' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + n <= b->length);

	ISC__BUFFER_ADD(b, n);
}

void
isc__buffer_subtract(isc_buffer_t *b, unsigned int n) {
	/*
	 * Decrease the 'used' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used >= n);

	ISC__BUFFER_SUBTRACT(b, n);
}

void
isc__buffer_clear(isc_buffer_t *b) {
	/*
	 * Make the used region empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	ISC__BUFFER_CLEAR(b);
}

void
isc__buffer_consumedregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the consumed region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_CONSUMEDREGION(b, r);
}

void
isc__buffer_remainingregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the remaining region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_REMAININGREGION(b, r);
}

void
isc__buffer_activeregion(isc_buffer_t *b, isc_region_t *r) {
	/*
	 * Make 'r' refer to the active region of 'b'.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	ISC__BUFFER_ACTIVEREGION(b, r);
}

void
isc__buffer_setactive(isc_buffer_t *b, unsigned int n) {
	/*
	 * Sets the end of the active region 'n' bytes after current.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->current + n <= b->used);

	ISC__BUFFER_SETACTIVE(b, n);
}

void
isc__buffer_first(isc_buffer_t *b) {
	/*
	 * Make the consumed region empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	ISC__BUFFER_FIRST(b);
}

void
isc__buffer_forward(isc_buffer_t *b, unsigned int n) {
	/*
	 * Increase the 'consumed' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->current + n <= b->used);

	ISC__BUFFER_FORWARD(b, n);
}

void
isc__buffer_back(isc_buffer_t *b, unsigned int n) {
	/*
	 * Decrease the 'consumed' region of 'b' by 'n' bytes.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(n <= b->current);

	ISC__BUFFER_BACK(b, n);
}

void
isc_buffer_compact(isc_buffer_t *b) {
	unsigned int length;
	void *src;

	/*
	 * Compact the used region by moving the remaining region so it occurs
	 * at the start of the buffer.  The used region is shrunk by the size
	 * of the consumed region, and the consumed region is then made empty.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));

	src = (unsigned char *)b->base + b->current;
	length = b->used - b->current;
	(void)memmove(b->base, src, (size_t)length);

	if (b->active > b->current)
		b->active -= b->current;
	else
		b->active = 0;
	b->current = 0;
	b->used = length;
}

isc_uint8_t
isc_buffer_getuint8(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint8_t result;

	/*
	 * Read an unsigned 8-bit integer from 'b' and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 1);

	cp = b->base;
	cp += b->current;
	b->current += 1;
	result = ((unsigned int)(cp[0]));

	return (result);
}

void
isc__buffer_putuint8(isc_buffer_t *b, isc_uint8_t val)
{
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 1 <= b->length);

	ISC__BUFFER_PUTUINT8(b, val);
}

isc_uint16_t
isc_buffer_getuint16(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint16_t result;

	/*
	 * Read an unsigned 16-bit integer in network byte order from 'b',
	 * convert it to host byte order, and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 2);

	cp = b->base;
	cp += b->current;
	b->current += 2;
	result = ((unsigned int)(cp[0])) << 8;
	result |= ((unsigned int)(cp[1]));

	return (result);
}

void
isc__buffer_putuint16(isc_buffer_t *b, isc_uint16_t val)
{
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 2 <= b->length);

	ISC__BUFFER_PUTUINT16(b, val);
}

isc_uint32_t
isc_buffer_getuint32(isc_buffer_t *b) {
	unsigned char *cp;
	isc_uint32_t result;

	/*
	 * Read an unsigned 32-bit integer in network byte order from 'b',
	 * convert it to host byte order, and return it.
	 */

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used - b->current >= 4);

	cp = b->base;
	cp += b->current;
	b->current += 4;
	result = ((unsigned int)(cp[0])) << 24;
	result |= ((unsigned int)(cp[1])) << 16;
	result |= ((unsigned int)(cp[2])) << 8;
	result |= ((unsigned int)(cp[3]));

	return (result);
}

void
isc__buffer_putuint32(isc_buffer_t *b, isc_uint32_t val)
{
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + 4 <= b->length);

	ISC__BUFFER_PUTUINT32(b, val);
}

void
isc__buffer_putmem(isc_buffer_t *b, unsigned char *base, unsigned int length) {
	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(b->used + length <= b->length);

	ISC__BUFFER_PUTMEM(b, base, length);
}	

void
isc_buffer_putstr(isc_buffer_t *b, const char *source) {
	unsigned int l;
	unsigned char *cp;

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(source != NULL);

	l = strlen(source);

	REQUIRE(l <= isc_buffer_availablelength(b));

	cp = isc_buffer_used(b);
	memcpy(cp, source, l);
	b->used += l;
}

isc_result_t
isc_buffer_copyregion(isc_buffer_t *b, isc_region_t *r) {
	unsigned char *base;
	unsigned int available;

	REQUIRE(ISC_BUFFER_VALID(b));
	REQUIRE(r != NULL);

	base = (unsigned char *)b->base + b->used;
	available = b->length - b->used;
        if (r->length > available)
		return (ISC_R_NOSPACE);
	memcpy(base, r->base, r->length);
	b->used += r->length;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_buffer_allocate(isc_mem_t *mctx, isc_buffer_t **dynbuffer,
		    unsigned int length)
{
	isc_buffer_t *dbuf;

	REQUIRE(dynbuffer != NULL);
	REQUIRE(*dynbuffer == NULL);

	dbuf = isc_mem_get(mctx, length + sizeof(isc_buffer_t));
	if (dbuf == NULL)
		return (ISC_R_NOMEMORY);

	isc_buffer_init(dbuf, ((unsigned char *)dbuf) + sizeof(isc_buffer_t),
			length);
	dbuf->mctx = mctx;

	*dynbuffer = dbuf;

	return (ISC_R_SUCCESS);
}

void
isc_buffer_free(isc_buffer_t **dynbuffer)
{
	unsigned int real_length;
	isc_buffer_t *dbuf;
	isc_mem_t *mctx;

	REQUIRE(dynbuffer != NULL);
	REQUIRE(ISC_BUFFER_VALID(*dynbuffer));
	REQUIRE((*dynbuffer)->mctx != NULL);

	dbuf = *dynbuffer;
	*dynbuffer = NULL;	/* destroy external reference */

	real_length = dbuf->length + sizeof(isc_buffer_t);
	mctx = dbuf->mctx;
	dbuf->mctx = NULL;
	isc_buffer_invalidate(dbuf);

	isc_mem_put(mctx, dbuf, real_length);
}