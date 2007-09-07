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

#ifndef ISC_UTIL_H
#define ISC_UTIL_H 1

#include <isc/error.h>

/*
 * NOTE:
 *
 * This file is not to be included from any <isc/???.h> (or other) library
 * files.
 *
 * Including this file puts several macros in your name space that are
 * not protected (as all the other ISC functions/macros do) by prepending
 * ISC_ or isc_ to the name.
 */

/***
 *** General Macros.
 ***/

/*
 * Use this to hide unused function arguments.
 *
 * int
 * foo(char *bar)
 * {
 *	UNUSED(bar);
 * }
 */
#define UNUSED(x)      (void)(x)

#define ISC_MAX(a, b)  ((a) > (b) ? (a) : (b))
#define ISC_MIN(a, b)  ((a) < (b) ? (a) : (b))

/*
 * We use macros instead of calling the routines directly because
 * the capital letters make the locking stand out.
 *
 * We RUNTIME_CHECK for success since in general there's no way
 * for us to continue if they fail.
 */

#ifdef ISC_UTIL_TRACEON
#define ISC_UTIL_TRACE(a) a
#include <stdio.h>
#else
#define ISC_UTIL_TRACE(a)
#endif

#define LOCK(lp) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "LOCKING %p %s %d\n", (lp), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_mutex_lock((lp)) == ISC_R_SUCCESS); \
	ISC_UTIL_TRACE(fprintf(stderr, "LOCKED %p %s %d\n", (lp), __FILE__, __LINE__)); \
	} while (0)
#define UNLOCK(lp) do { \
	RUNTIME_CHECK(isc_mutex_unlock((lp)) == ISC_R_SUCCESS); \
	ISC_UTIL_TRACE(fprintf(stderr, "UNLOCKED %p %s %d\n", (lp), __FILE__, __LINE__)); \
	} while (0)

#define BROADCAST(cvp) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "BROADCAST %p %s %d\n", (cvp), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_condition_broadcast((cvp)) == ISC_R_SUCCESS); \
	} while (0)
#define SIGNAL(cvp) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "SIGNAL %p %s %d\n", (cvp), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_condition_signal((cvp)) == ISC_R_SUCCESS); \
	} while (0)
#define WAIT(cvp, lp) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "WAIT %p LOCK %p %s %d\n", (cvp), (lp), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_condition_wait((cvp), (lp)) == ISC_R_SUCCESS); \
	ISC_UTIL_TRACE(fprintf(stderr, "WAITED %p LOCKED %p %s %d\n", (cvp), (lp), __FILE__, __LINE__)); \
	} while (0)

/*
 * isc_condition_waituntil can return ISC_R_TIMEDOUT, so we
 * don't RUNTIME_CHECK the result.
 *
 *  XXX Also, can't really debug this then...
 */

#define WAITUNTIL(cvp, lp, tp) \
	isc_condition_waituntil((cvp), (lp), (tp))

#define RWLOCK(lp, t) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "RWLOCK %p, %d %s %d\n", (lp), (t), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_rwlock_lock((lp), (t)) == ISC_R_SUCCESS); \
	ISC_UTIL_TRACE(fprintf(stderr, "RWLOCKED %p, %d %s %d\n", (lp), (t), __FILE__, __LINE__)); \
	} while (0)
#define RWUNLOCK(lp, t) do { \
	ISC_UTIL_TRACE(fprintf(stderr, "RWUNLOCK %p, %d %s %d\n", (lp), (t), __FILE__, __LINE__)); \
	RUNTIME_CHECK(isc_rwlock_unlock((lp), (t)) == ISC_R_SUCCESS); \
	} while (0)

/*
 * List Macros.
 *
 * These are provided as a temporary measure to ease the transition
 * to the renamed list macros in <isc/list.h>.
 */

#include <isc/list.h>

#define LIST(type)			ISC_LIST(type)
#define INIT_LIST(type)			ISC_LIST_INIT(type)
#define LINK(type)			ISC_LINK(type)
#define INIT_LINK(elt, link)		ISC_LINK_INIT(elt, link)
#define HEAD(list)			ISC_LIST_HEAD(list)
#define TAIL(list)			ISC_LIST_TAIL(list)
#define EMPTY(list)			ISC_LIST_EMPTY(list)
#define PREV(elt, link)			ISC_LIST_PREV(elt, link)
#define NEXT(elt, link)			ISC_LIST_NEXT(elt, link)
#define APPEND(list, elt, link)		ISC_LIST_APPEND(list, elt, link)
#define PREPEND(list, elt, link)	ISC_LIST_PREPEND(list, elt, link)
#define UNLINK(list, elt, link)		ISC_LIST_UNLINK(list, elt, link)
#define ENQUEUE(list, elt, link)	ISC_LIST_APPEND(list, elt, link)
#define DEQUEUE(list, elt, link)	ISC_LIST_UNLINK(list, elt, link)
#define INSERTBEFORE(li, b, e, ln)	ISC_LIST_INSERTBEFORE(li, b, e, ln)
#define INSERTAFTER(li, a, e, ln)	ISC_LIST_INSERTAFTER(li, a, e, ln)
#define APPENDLIST(list1, list2, link)	ISC_LIST_APPENDLIST(list1, list2, link)

#endif /* ISC_UTIL_H */
