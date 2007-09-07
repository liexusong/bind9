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

#include <isc/string.h>

#include <named/log.h>
#include <named/logconf.h>

#define CHECK(op) \
	do { result = (op); 				  	 \
	       if (result != ISC_R_SUCCESS) goto cleanup; 	 \
	} while (0)

/*
 * Set up a logging category according to the named.conf data
 * in 'ccat' and add it to 'lctx'.
 */
static isc_result_t
category_fromconf(dns_c_logcat_t *ccat, isc_logconfig_t *lctx) {
	isc_result_t result;
	unsigned int i;
	isc_logcategory_t *category;
	isc_logmodule_t *module;

	category = isc_log_categorybyname(ns_g_lctx, ccat->catname);
#ifdef notyet
	module = isc_log_modulebyname(ns_g_lctx, ccat->modname);
#else
	module = NULL;
#endif

	for (i = 0; i < ccat->nextcname; i++) {
		char *channelname = ccat->channel_names[i];

		result = isc_log_usechannel(lctx, channelname, category,
					    module);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(ns_g_lctx, DNS_LOGCATEGORY_CONFIG,
				      NS_LOGMODULE_SERVER, ISC_LOG_ERROR,
				      "logging channel '%s': %s", channelname,
				      isc_result_totext(result));
			return (result);
		}
	}
	return (ISC_R_SUCCESS);
}

/*
 * Set up a logging channel according to the named.conf data
 * in 'cchan' and add it to 'lctx'.
 */
static isc_result_t
channel_fromconf(dns_c_logchan_t *cchan, isc_logconfig_t *lctx) {
	isc_result_t result;
	isc_logdestination_t dest;
	unsigned int type;
	int flags = 0;
	int level;
	
	type = ISC_LOG_TONULL;
	switch (cchan->ctype) {
	case dns_c_logchan_file:
		type = ISC_LOG_TOFILE;
		{
			const char *path = NULL;
			isc_int32_t versions = ISC_LOG_ROLLNEVER; 
			/*
			 * XXXDCL should be isc_offset_t, but that
			 * is incompatible with dns_c_logchan_getsize.
			 */
			isc_uint32_t size = 0;
			(void)dns_c_logchan_getpath(cchan, &path);
			if (path == NULL) {
				isc_log_write(ns_g_lctx,
					      DNS_LOGCATEGORY_CONFIG,
					      NS_LOGMODULE_SERVER,
					      ISC_LOG_ERROR,
					      "file log channel has "
					      "no file name");
				return (ISC_R_UNEXPECTED);
			}
			(void)dns_c_logchan_getversions(cchan,
							(isc_uint32_t *)
							&versions);
			(void)dns_c_logchan_getsize(cchan, &size);
			dest.file.stream = NULL;
			dest.file.name = cchan->u.filec.path;
			dest.file.versions = versions;
			dest.file.maximum_size = size;
		}
		break;

	case dns_c_logchan_syslog:
		type = ISC_LOG_TOSYSLOG;
		{
			int facility = LOG_DAEMON;
			(void)dns_c_logchan_getfacility(cchan, &facility);
			dest.facility = facility;
		}
		break;

	case dns_c_logchan_null:
		break;
	}

	/*
	 * Munge flags.
	 */
	{
		isc_boolean_t printcat = ISC_FALSE;
		isc_boolean_t printsev = ISC_FALSE;
		isc_boolean_t printtime = ISC_FALSE;

		(void)dns_c_logchan_getprintcat(cchan, &printcat);
		(void)dns_c_logchan_getprintsev(cchan, &printsev);
		(void)dns_c_logchan_getprinttime(cchan, &printtime);

		if (printcat)
			flags |= ISC_LOG_PRINTCATEGORY;
		if (printtime)
			flags |= ISC_LOG_PRINTTIME;
		if (printsev)
			flags |= ISC_LOG_PRINTLEVEL;
		/* XXX ISC_LOG_PRINTMODULE */
	}
	
	level = ISC_LOG_INFO;
	(void)dns_c_logchan_getdebuglevel(cchan, &level);
	
	result = isc_log_createchannel(lctx, cchan->name,
				       type, level, &dest, flags);
	return (result);
}

isc_result_t
ns_log_configure(isc_logconfig_t *lcctx, dns_c_logginglist_t *clog) {
	isc_result_t result;
	dns_c_logchan_t *cchan;
	dns_c_logcat_t *ccat;
	isc_boolean_t default_set = ISC_FALSE;

	CHECK(ns_log_setdefaultchannels(lcctx));
		
	for (cchan = ISC_LIST_HEAD(clog->channels);
	     cchan != NULL;
	     cchan = ISC_LIST_NEXT(cchan, next)) {
		CHECK(channel_fromconf(cchan, lcctx));
	}

	for (ccat = ISC_LIST_HEAD(clog->categories);
	     ccat != NULL;
	     ccat = ISC_LIST_NEXT(ccat, next)) {
		CHECK(category_fromconf(ccat, lcctx));
		if (! default_set)
			default_set =
				ISC_TF(strcmp(ccat->catname, "default") == 0);
	}

	if (! default_set)
		CHECK(ns_log_setdefaultcategory(lcctx));

	return (ISC_R_SUCCESS);

 cleanup:
	if (lcctx != NULL)
		isc_logconfig_destroy(&lcctx);
	return (result);
}