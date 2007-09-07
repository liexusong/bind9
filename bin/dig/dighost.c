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

/*
 * Notice to programmers:  Do not use this code as an example of how to
 * use the ISC library to perform DNS lookups.  Dig and Host both operate
 * on the request level, since they allow fine-tuning of output and are
 * intended as debugging tools.  As a result, they perform many of the
 * functions which could be better handled using the dns_resolver
 * functions in most applications.
 */

#include <config.h>

#include <stdlib.h>
#include <unistd.h>

extern int h_errno;

#include <isc/app.h>
#include <isc/netdb.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/timer.h>
#include <isc/util.h>

#include <dns/message.h>
#include <dns/rdata.h>
#include <dns/rdataclass.h>
#include <dns/rdataset.h>
#include <dns/rdatatype.h>
#include <dns/rdatalist.h>
#include <dns/result.h>

#include <dig/dig.h>

ISC_LIST(dig_lookup_t) lookup_list;
ISC_LIST(dig_server_t) server_list;
ISC_LIST(dig_searchlist_t) search_list;

isc_boolean_t have_ipv6 = ISC_FALSE, specified_source = ISC_FALSE,
	free_now = ISC_FALSE, show_details = ISC_FALSE, usesearch=ISC_TRUE,
	qr = ISC_FALSE;
#ifdef TWIDDLE
isc_boolean_t twiddle = ISC_FALSE;
#endif
in_port_t port = 53;
unsigned int timeout = 5;
isc_mem_t *mctx = NULL;
isc_taskmgr_t *taskmgr = NULL;
isc_task_t *task = NULL;
isc_timermgr_t *timermgr = NULL;
isc_socketmgr_t *socketmgr = NULL;
isc_sockaddr_t bind_address;
char *rootspace[BUFSIZE];
isc_buffer_t rootbuf;
int sendcount = 0;
int ndots = -1;
int tries = 3;
int lookup_counter = 0;
char fixeddomain[MXNAME]="";
int exitcode = 9;

static void
cancel_lookup(dig_lookup_t *lookup);

static int
count_dots(char *string) {
	char *s;
	int i=0;

	s = string;
	while (*s != 0) {
		if (*s == '.')
			i++;
		s++;
	}
	return (i);
}

static void
hex_dump(isc_buffer_t *b) {
	unsigned int len;
	isc_region_t r;

	isc_buffer_remainingregion(b, &r);

	printf("Printing a buffer with length %d\n", r.length);
	for (len = 0 ; len < r.length ; len++) {
		printf("%02x ", r.base[len]);
		if (len != 0 && len % 16 == 0)
			printf("\n");
	}
	if (len % 16 != 0)
		printf("\n");
}


void
fatal(char *format, ...) {
	va_list args;

	va_start(args, format);	
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	if (exitcode == 0)
		exitcode = 8;
#ifdef NEVER
	dighost_shutdown();
	free_lists(exitcode);
	if (mctx != NULL) {
#ifdef MEMDEBUG
		isc_mem_stats(mctx, stderr);
#endif
		isc_mem_destroy(&mctx);
	}
#endif
	exit(exitcode);
}

#ifdef DEBUG
void
debug(char *format, ...) {
	va_list args;

	va_start(args, format);	
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
}
#else
void
debug(char *format, ...) {
	va_list args;
	UNUSED(args);
	UNUSED(format);
}
#endif

void
check_result(isc_result_t result, char *msg) {
	if (result != ISC_R_SUCCESS) {
		exitcode = 1;
		fatal("%s: %s", msg, isc_result_totext(result));
	}
}

isc_boolean_t
isclass(char *text) {
	/* Tests if a field is a class, without needing isc libs
	 * initialized.  This list will have to be manually kept in 
	 * sync with what the libs support.
	 */
	const char *classlist[] = {"in", "hs"};
	const int numclasses = 2;
	int i;

	for (i = 0; i < numclasses; i++)
		if (strcasecmp(text, classlist[i]) == 0)
			return ISC_TRUE;

	return ISC_FALSE;
}

isc_boolean_t
istype(char *text) {
	/* Tests if a field is a type, without needing isc libs
	 * initialized.  This list will have to be manually kept in 
	 *  sync with what the libs support.
	 */
	const char *typelist[] = {"a", "ns", "md", "mf", "cname",
				  "soa", "mb", "mg", "mr", "null",
				  "wks", "ptr", "hinfo", "minfo",
				  "mx", "txt", "rp", "afsdb",
				  "x25", "isdn", "rt", "nsap",
				  "nsap_ptr", "sig", "key", "px",
				  "gpos", "aaaa", "loc", "nxt",
				  "srv", "naptr", "kx", "cert",
				  "a6", "dname", "opt", "unspec",
				  "tkey", "tsig", "axfr", "any"};
	const int numtypes = 42;
	int i;

	for (i = 0; i < numtypes; i++) {
		if (strcasecmp(text, typelist[i]) == 0)
			return ISC_TRUE;
	}
	return ISC_FALSE;
}


#ifdef TWIDDLE
void
twiddlebuf(isc_buffer_t buf) {
	isc_region_t r;
	int len, pos, bit;
	unsigned char bitfield;
	int i, tw;

	hex_dump(&buf);
	tw=TWIDDLE;
	printf ("Twiddling %d bits: ", tw);
	for (i=0;i<tw;i++) {
		isc_buffer_usedregion (&buf, &r);
		len = r.length;
		pos=(int)random();
		pos = pos%len;
		bit = (int)random()%8;
		bitfield = 1 << bit;
		printf ("%d@%03x ", bit, pos);
		r.base[pos] ^= bitfield;
	}
	puts ("");
	hex_dump(&buf);
}
#endif

dig_lookup_t
*requeue_lookup(dig_lookup_t *lookold, isc_boolean_t servers) {
	dig_lookup_t *looknew;
	dig_server_t *s, *srv;

	debug("requeue_lookup()");

	if (free_now)
		return(ISC_R_SUCCESS);

	lookup_counter++;
	if (lookup_counter > LOOKUP_LIMIT)
		fatal ("Too many lookups.");
	looknew = isc_mem_allocate
		(mctx, sizeof(struct dig_lookup));
	if (looknew == NULL)
		fatal ("Memory allocation failure in %s:%d",
		       __FILE__, __LINE__);
	looknew->pending = ISC_FALSE;
	strncpy (looknew->textname, lookold-> textname, MXNAME);
	strncpy (looknew->rttext, lookold-> rttext, 32);
	strncpy (looknew->rctext, lookold-> rctext, 32);
	looknew->namespace[0]=0;
	looknew->sendspace[0]=0;
	looknew->sendmsg=NULL;
	looknew->name=NULL;
	looknew->oname=NULL;
	looknew->timer = NULL;
	looknew->xfr_q = NULL;
	looknew->doing_xfr = lookold->doing_xfr;
	looknew->defname = lookold->defname;
	looknew->trace = lookold->trace;
	looknew->trace_root = lookold->trace_root;
	looknew->identify = lookold->identify;
	looknew->udpsize = lookold->udpsize;
	looknew->recurse = lookold->recurse;
	looknew->aaonly = lookold->aaonly;
	looknew->ns_search_only = lookold->ns_search_only;
	looknew->origin = NULL;
	looknew->retries = tries;
	looknew->nsfound = 0;
	looknew->tcp_mode = lookold->tcp_mode;
	looknew->comments = lookold->comments;
	looknew->stats = lookold->stats;
	looknew->section_question = lookold->section_question;
	looknew->section_answer = lookold->section_answer;
	looknew->section_authority = lookold->section_authority;
	looknew->section_additional = lookold->section_additional;
	ISC_LIST_INIT(looknew->my_server_list);
	ISC_LIST_INIT(looknew->q);

	looknew->use_my_server_list = ISC_FALSE;
	if (servers) {
		looknew->use_my_server_list = lookold->use_my_server_list;
		if (looknew->use_my_server_list) {
			s = ISC_LIST_HEAD(lookold->my_server_list);
			while (s != NULL) {
				srv = isc_mem_allocate (mctx, sizeof(struct
								dig_server));
				if (srv == NULL)
					fatal("Memory allocation failure "
					      "in %s:%d", __FILE__, __LINE__);
				strncpy(srv->servername, s->servername,
					MXNAME);
				ISC_LIST_ENQUEUE(looknew->my_server_list, srv,
						 link);
				s = ISC_LIST_NEXT(s, link);
			}
		}
	}
	debug ("Before insertion, init@%lx "
	       "-> %lx, new@%lx "
	       "-> %lx", (long int)lookold,
	       (long int)lookold->link.next,
	       (long int)looknew, (long int)looknew->
	       link.next);
	ISC_LIST_INSERTAFTER(lookup_list, lookold, looknew, link);
	debug ("After insertion, init -> "
	       "%lx, new = %lx, "
	       "new -> %lx", (long int)lookold,
	       (long int)looknew, (long int)looknew->
	       link.next);
	return (looknew);
}	

void
setup_system(void) {
	char rcinput[MXNAME];
	FILE *fp;
	char *ptr;
	dig_server_t *srv;
	dig_searchlist_t *search;
	dig_lookup_t *l;
	isc_boolean_t get_servers;


	if (fixeddomain[0]!=0) {
		search = isc_mem_allocate( mctx, sizeof(struct dig_server));
		if (search == NULL)
			fatal("Memory allocation failure in %s:%d",
			      __FILE__, __LINE__);
		strncpy(search->origin, fixeddomain, MXNAME - 1);
		ISC_LIST_PREPEND(search_list, search, link);
	}

	debug ("setup_system()");

	free_now = ISC_FALSE;
	get_servers = (server_list.head == NULL);
	fp = fopen (RESOLVCONF, "r");
	if (fp != NULL) {
		while (fgets(rcinput, MXNAME, fp) != 0) {
			ptr = strtok (rcinput, " \t\r\n");
			if (ptr != NULL) {
				if (get_servers &&
				    strcasecmp(ptr, "nameserver") == 0) {
					debug ("Got a nameserver line");
					ptr = strtok (NULL, " \t\r\n");
					if (ptr != NULL) {
						srv = isc_mem_allocate(mctx,
						   sizeof(struct dig_server));
						if (srv == NULL)
							fatal("Memory "
							      "allocation "
							      "failure in "
							      "%s:%d",
							      __FILE__,
							      __LINE__);
							strncpy((char *)srv->
								servername,
								ptr,
								MXNAME - 1);
							ISC_LIST_APPEND
								(server_list,
								 srv, link);
					}
				} else if (strcasecmp(ptr, "options") == 0) {
					ptr = strtok(NULL, " \t\r\n");
					if (ptr != NULL) {
						if ((strncasecmp(ptr, "ndots:",
							    6) == 0) &&
						    (ndots == -1)) {
							ndots = atoi(
							      &ptr[6]);
							debug ("ndots is "
							       "%d.",
							       ndots);
						}
					}
				} else if ((strcasecmp(ptr, "search") == 0)
					   && usesearch){
					while ((ptr = strtok(NULL, " \t\r\n"))
					       != NULL) {
						search = isc_mem_allocate(
						   mctx, sizeof(struct
								dig_server));
						if (search == NULL)
							fatal("Memory "
							      "allocation "
							      "failure in %s:"
							      "%d", __FILE__, 
							      __LINE__);
						strncpy(search->
							origin,
							ptr,
							MXNAME - 1);
						ISC_LIST_APPEND
							(search_list,
							 search,
							 link);
					}
				} else if ((strcasecmp(ptr, "domain") == 0) &&
					   (fixeddomain[0] == 0 )){
					while ((ptr = strtok(NULL, " \t\r\n"))
					       != NULL) {
						search = isc_mem_allocate(
						   mctx, sizeof(struct
								dig_server));
						if (search == NULL)
							fatal("Memory "
							      "allocation "
							      "failure in %s:"
							      "%d", __FILE__, 
							      __LINE__);
						strncpy(search->
							origin,
							ptr,
							MXNAME - 1);
						ISC_LIST_PREPEND
							(search_list,
							 search,
							 link);
					}
				}
						
			}
		}
		fclose (fp);
	}

	if (ndots == -1)
		ndots = 1;

	if (server_list.head == NULL) {
		srv = isc_mem_allocate(mctx, sizeof(dig_server_t));
		if (srv == NULL)
			fatal("Memory allocation failure");
		strcpy(srv->servername, "127.0.0.1");
		ISC_LIST_APPEND(server_list, srv, link);
	}

	for (l = ISC_LIST_HEAD(lookup_list) ;
	     l != NULL;
	     l = ISC_LIST_NEXT(l, link) ) {
	     l -> origin = ISC_LIST_HEAD(search_list);
	     }
}
	
void
setup_libs(void) {
	isc_result_t result;
	isc_buffer_t b;

	debug ("setup_libs()");

	/*
	 * Warning: This is not particularly good randomness.  We'll
	 * just use random() now for getting id values, but doing so
	 * does NOT insure that id's cann't be guessed.
	 */
	srandom (getpid() + (int)&setup_libs);

	result = isc_app_start();
	check_result(result, "isc_app_start");

	result = isc_net_probeipv4();
	check_result(result, "isc_net_probeipv4");

	result = isc_net_probeipv6();
	if (result == ISC_R_SUCCESS)
		have_ipv6=ISC_TRUE;

	result = isc_mem_create(0, 0, &mctx);
	check_result(result, "isc_mem_create");

	result = isc_taskmgr_create (mctx, 1, 0, &taskmgr);
	check_result(result, "isc_taskmgr_create");

	result = isc_task_create (taskmgr, 0, &task);
	check_result(result, "isc_task_create");

	result = isc_timermgr_create (mctx, &timermgr);
	check_result(result, "isc_timermgr_create");

	result = isc_socketmgr_create (mctx, &socketmgr);
	check_result(result, "isc_socketmgr_create");
	isc_buffer_init(&b, ".", 1);
	isc_buffer_add(&b, 1);
}

static void
add_opt (dns_message_t *msg, isc_uint16_t udpsize) {
	dns_rdataset_t *rdataset = NULL;
	dns_rdatalist_t *rdatalist = NULL;
	dns_rdata_t *rdata = NULL;
	isc_result_t result;

	debug ("add_opt()");
	result = dns_message_gettemprdataset(msg, &rdataset);
	check_result (result, "dns_message_gettemprdataset");
	dns_rdataset_init (rdataset);
	result = dns_message_gettemprdatalist(msg, &rdatalist);
	check_result (result, "dns_message_gettemprdatalist");
	result = dns_message_gettemprdata(msg, &rdata);
	check_result (result, "dns_message_gettemprdata");
	
	debug ("Setting udp size of %d", udpsize);
	rdatalist->type = dns_rdatatype_opt;
	rdatalist->covers = 0;
	rdatalist->rdclass = udpsize;
	rdatalist->ttl = 0;
	rdata->data = NULL;
	rdata->length = 0;
	ISC_LIST_INIT(rdatalist->rdata);
	ISC_LIST_APPEND(rdatalist->rdata, rdata, link);
	dns_rdatalist_tordataset(rdatalist, rdataset);
	result = dns_message_setopt(msg, rdataset);
	check_result (result, "dns_message_setopt");
}

static void
add_type(dns_message_t *message, dns_name_t *name, dns_rdataclass_t rdclass,
	 dns_rdatatype_t rdtype)
{
	dns_rdataset_t *rdataset;
	isc_result_t result;

	debug ("add_type()"); 
	rdataset = NULL;
	result = dns_message_gettemprdataset(message, &rdataset);
	check_result(result, "dns_message_gettemprdataset()");
	dns_rdataset_init(rdataset);
	dns_rdataset_makequestion(rdataset, rdclass, rdtype);
	ISC_LIST_APPEND(name->list, rdataset, link);
}

static void
check_next_lookup(dig_lookup_t *lookup) {
	dig_lookup_t *next;
	dig_query_t *query;
	isc_boolean_t still_working=ISC_FALSE;
	
	if (free_now)
		return;

	debug("check_next_lookup(%lx)", (long int)lookup);
	for (query = ISC_LIST_HEAD(lookup->q);
	     query != NULL;
	     query = ISC_LIST_NEXT(query, link)) {
		if (query->working) {
			debug("Still have a worker.", stderr);
			still_working=ISC_TRUE;
		}
	}
	if (still_working)
		return;

	debug ("Have %d retries left for %s",
	       lookup->retries-1, lookup->textname);
	debug ("Lookup %s pending", lookup->pending?"is":"is not");

	next = ISC_LIST_NEXT(lookup, link);
	
	if (lookup->tcp_mode) {
		if (next == NULL) {
			debug("Shutting Down.", stderr);
			dighost_shutdown();
			return;
		}
		if (next->sendmsg == NULL) {
			debug ("Setting up for TCP");
			setup_lookup(next);
			do_lookup(next);
		}
	} else {
		if (!lookup->pending) {
			if (next == NULL) {
				debug("Shutting Down.", stderr);
				dighost_shutdown();
				return;
			}
			if (next->sendmsg == NULL) {
				debug ("Setting up for UDP");
				setup_lookup(next);
				do_lookup(next);
			}
		} else {
			if (lookup->retries > 1) {
				debug ("Retrying");
				lookup->retries --;
				if (lookup->timer != NULL)
					isc_timer_detach(&lookup->timer);
				send_udp(lookup);
			} else {
				debug ("Cancelling");
				cancel_lookup(lookup);
			}
		}
	}
}


static void
followup_lookup(dns_message_t *msg, dig_query_t *query,
		dns_section_t section) {
	dig_lookup_t *lookup = NULL;
	dig_server_t *srv = NULL;
	dns_rdataset_t *rdataset = NULL;
	dns_rdata_t rdata;
	dns_name_t *name = NULL;
	isc_result_t result, loopresult;
	isc_buffer_t *b = NULL;
	isc_region_t r;
	isc_boolean_t success = ISC_FALSE;
	int len;

	debug ("followup_lookup()"); 
	if (free_now)
		return;
	result = dns_message_firstname (msg,section);
	if (result != ISC_R_SUCCESS) {
		debug ("Firstname returned %s",
			isc_result_totext(result));
		if ((section == DNS_SECTION_ANSWER) &&
		    query->lookup->trace)
			followup_lookup (msg, query, DNS_SECTION_AUTHORITY);
                return;
	}

	debug ("Following up %s", query->lookup->textname);

	for (;;) {
		name = NULL;
		dns_message_currentname(msg, section, &name);
		for (rdataset = ISC_LIST_HEAD(name->list);
		     rdataset != NULL;
		     rdataset = ISC_LIST_NEXT(rdataset, link)) {
			loopresult = dns_rdataset_first(rdataset);
			while (loopresult == ISC_R_SUCCESS) {
				dns_rdataset_current(rdataset, &rdata);
				debug ("Got rdata with type %d",
				       rdata.type);
				if ((rdata.type == dns_rdatatype_ns) &&
				    (!query->lookup->trace_root ||
				     (query->lookup->nsfound < ROOTNS)))
				{
					query->lookup->nsfound++;
					result = isc_buffer_allocate(mctx, &b,
								     BUFSIZE);
					check_result (result,
						      "isc_buffer_allocate");
					result = dns_rdata_totext (&rdata,
								   NULL,
								   b);
					check_result (result,
						      "dns_rdata_totext");
					isc_buffer_usedregion(b, &r);
					len = r.length-1;
					if (len >= MXNAME)
						len = MXNAME-1;
				/* Initialize lookup if we've not yet */
					debug ("Found NS %d %.*s",
						 (int)r.length, (int)r.length,
						 (char *)r.base);
					if (!success) {
						success = ISC_TRUE;
						lookup_counter++;
						lookup = requeue_lookup
							(query->lookup,
							 ISC_FALSE);
						lookup->doing_xfr = ISC_FALSE;
						lookup->defname = ISC_FALSE;
						lookup->use_my_server_list = 
							ISC_TRUE;
						if (section ==
						    DNS_SECTION_ANSWER)
							lookup->trace =
								ISC_FALSE;
						else
							lookup->trace =
								query->
								lookup->trace;
						lookup->trace_root = ISC_FALSE;
						ISC_LIST_INIT(lookup->
							      my_server_list);
					}
					srv = isc_mem_allocate (mctx,
								sizeof(
								struct
								dig_server));
					if (srv == NULL)
						fatal("Memory allocation "
						      "failure in %s:%d",
						      __FILE__, __LINE__);
					strncpy(srv->servername, 
						(char *)r.base, len);
					srv->servername[len]=0;
					debug ("Adding server %s",
					       srv->servername);
					ISC_LIST_APPEND
						(lookup->my_server_list,
						 srv, link);
					isc_buffer_free (&b);
				}
				loopresult = dns_rdataset_next(rdataset);
			}
		}
		result = dns_message_nextname (msg, section);
		if (result != ISC_R_SUCCESS)
			break;
	}
	if ((lookup == NULL) && (section == DNS_SECTION_ANSWER) &&
	    query->lookup->trace)
		followup_lookup(msg, query, DNS_SECTION_AUTHORITY);
}

static void
next_origin(dns_message_t *msg, dig_query_t *query) {
	dig_lookup_t *lookup;

	UNUSED (msg);

	debug ("next_origin()"); 
	if (free_now)
		return;
	debug ("Following up %s", query->lookup->textname);

	if (query->lookup->origin == NULL) { /*Then we just did rootorg;
					      there's nothing left. */
		debug ("Made it to the root whith nowhere to go.");
		return;
	}
	lookup = requeue_lookup(query->lookup, ISC_TRUE);
	lookup->defname = ISC_FALSE;
	lookup->origin = ISC_LIST_NEXT(query->lookup->origin, link);
}


void
setup_lookup(dig_lookup_t *lookup) {
	isc_result_t result, res2;
	int len;
	dns_rdatatype_t rdtype;
	dns_rdataclass_t rdclass;
	dig_server_t *serv;
	dig_query_t *query;
	isc_region_t r;
	isc_textregion_t tr;
	isc_buffer_t b;
	char store[MXNAME];
	
	REQUIRE (lookup != NULL);

	debug("setup_lookup(%lx)",(long int)lookup);

	if (free_now)
		return;

	debug("Setting up for looking up %s @%lx->%lx", 
		lookup->textname, (long int)lookup,
		(long int)lookup->link.next);

	result = dns_message_create(mctx, DNS_MESSAGE_INTENTRENDER,
				    &lookup->sendmsg);
	check_result(result, "dns_message_create");


	result = dns_message_gettempname(lookup->sendmsg, &lookup->name);
	check_result(result, "dns_message_gettempname");
	dns_name_init(lookup->name, NULL);

	isc_buffer_init(&lookup->namebuf, lookup->namespace, BUFSIZE);
	isc_buffer_init(&lookup->onamebuf, lookup->onamespace, BUFSIZE);

	if ((count_dots(lookup->textname) >= ndots) || lookup->defname)
		lookup->origin = NULL; /* Force root lookup */
	debug ("lookup->origin = %lx", (long int)lookup->origin);
	if (lookup->origin != NULL) {
		debug ("Trying origin %s", lookup->origin->origin);
		result = dns_message_gettempname(lookup->sendmsg,
						 &lookup->oname);
		check_result(result, "dns_message_gettempname");
		dns_name_init(lookup->oname, NULL);
		len=strlen(lookup->origin->origin);
		isc_buffer_init(&b, lookup->origin->origin, len);
		isc_buffer_add(&b, len);
		result = dns_name_fromtext(lookup->oname, &b, dns_rootname,
					   ISC_FALSE, &lookup->onamebuf);
		if (result != ISC_R_SUCCESS) {
		dns_message_puttempname(lookup->sendmsg,
						&lookup->name);
			dns_message_puttempname(lookup->sendmsg,
						&lookup->oname);
			fatal("Aborting: %s is not a legal name syntax. (%s)",
			      lookup->origin->origin,
			      dns_result_totext(result));
		}
		if (!lookup->trace_root) {
			len=strlen(lookup->textname);
			isc_buffer_init(&b, lookup->textname, len);
			isc_buffer_add(&b, len);
			result = dns_name_fromtext(lookup->name, &b,
						   lookup->oname, ISC_FALSE, 
						   &lookup->namebuf);
		} else {
			isc_buffer_init(&b, ". ", 1);
			isc_buffer_add(&b, 1);
			result = dns_name_fromtext(lookup->name, &b,
						   lookup->oname, ISC_FALSE, 
						   &lookup->namebuf);
		}			
		if (result != ISC_R_SUCCESS) {
			dns_message_puttempname(lookup->sendmsg,
						&lookup->name);
			dns_message_puttempname(lookup->sendmsg,
						&lookup->oname);
			fatal("Aborting: %s is not a legal name syntax. (%s)",
			      lookup->textname, dns_result_totext(result));
		}
		dns_message_puttempname(lookup->sendmsg, &lookup->oname);
	} else {
		debug ("Using root origin.");
		if (!lookup->trace_root) {
			len = strlen (lookup->textname);
			isc_buffer_init(&b, lookup->textname, len);
			isc_buffer_add(&b, len);
			result = dns_name_fromtext(lookup->name, &b,
						   dns_rootname,
						   ISC_FALSE,
						   &lookup->namebuf);
		} else {
			isc_buffer_init(&b, ". ", 1);
			isc_buffer_add(&b, 1);
			result = dns_name_fromtext(lookup->name, &b,
						   dns_rootname,
						   ISC_FALSE,
						   &lookup->namebuf);
		}
		if (result != ISC_R_SUCCESS) {
			dns_message_puttempname(lookup->sendmsg,
						&lookup->name);
			isc_buffer_init(&b, store, MXNAME);
			res2 = dns_name_totext(dns_rootname, ISC_FALSE, &b);
			check_result (res2, "dns_name_totext");
			isc_buffer_usedregion (&b, &r);
			fatal("Aborting: %s/%.*s is not a legal name syntax. "
			      "(%s)", lookup->textname, (int)r.length,
			      (char *)r.base, dns_result_totext(result));
		}
	}		
	isc_buffer_init (&b, store, MXNAME);
	dns_name_totext(lookup->name, ISC_FALSE, &b);
	isc_buffer_usedregion (&b, &r);
	trying((int)r.length, (char *)r.base, lookup);
#ifdef DEBUG
	if (dns_name_isabsolute(lookup->name))
		debug ("This is an absolute name.");
	else
		debug ("This is a relative name (which is wrong).");
#endif

	if (lookup->rctext[0] == 0)
		strcpy(lookup->rctext, "IN");
	if (lookup->rttext[0] == 0)
		strcpy(lookup->rttext, "A");

	lookup->sendmsg->id = random();
	lookup->sendmsg->opcode = dns_opcode_query;
	/* If this is a trace request, completely disallow recursion, since
	 * it's meaningless for traces */
	if (lookup->recurse && !lookup->trace) {
		debug ("Recursive query");
		lookup->sendmsg->flags |= DNS_MESSAGEFLAG_RD;
	}

	if (lookup->aaonly) {
		debug ("AA query");
		lookup->sendmsg->flags |= DNS_MESSAGEFLAG_AA;
	}

	dns_message_addname(lookup->sendmsg, lookup->name,
			    DNS_SECTION_QUESTION);
	
	
	if (lookup->trace_root) {
		tr.base="SOA";
		tr.length=3;
	} else {
		tr.base=lookup->rttext;
		tr.length=strlen(lookup->rttext);
	}
	result = dns_rdatatype_fromtext(&rdtype, &tr);
	check_result(result, "dns_rdatatype_fromtext");
	if (rdtype == dns_rdatatype_axfr) {
		lookup->doing_xfr = ISC_TRUE;
		/*
		 * Force TCP mode if we're doing an xfr.
		 */
		lookup->tcp_mode = ISC_TRUE;
	}
	if (lookup->trace_root) {
		tr.base="IN";
		tr.length=2;
	} else {
		tr.base=lookup->rctext;
		tr.length=strlen(lookup->rctext);
	}
	result = dns_rdataclass_fromtext(&rdclass, &tr);
	check_result(result, "dns_rdataclass_fromtext");
	add_type(lookup->sendmsg, lookup->name, rdclass, rdtype);

	isc_buffer_init(&lookup->sendbuf, lookup->sendspace, COMMSIZE);
	debug ("Starting to render the message");
	result = dns_message_renderbegin(lookup->sendmsg, &lookup->sendbuf);
	check_result(result, "dns_message_renderbegin");
	if (lookup->udpsize > 0) {
		add_opt(lookup->sendmsg, lookup->udpsize);
	}
	result = dns_message_rendersection(lookup->sendmsg,
					   DNS_SECTION_QUESTION, 0);
	check_result(result, "dns_message_rendersection");
	result = dns_message_renderend(lookup->sendmsg);
	check_result(result, "dns_message_renderend");
	debug ("Done rendering.");

	lookup->pending = ISC_FALSE;

	if (lookup->use_my_server_list)
		serv = ISC_LIST_HEAD(lookup->my_server_list);
	else
		serv = ISC_LIST_HEAD(server_list);
	for (; serv != NULL;
	     serv = ISC_LIST_NEXT(serv, link)) {
		query = isc_mem_allocate(mctx, sizeof(dig_query_t));
		if (query == NULL)
			fatal("Memory allocation failure in %s:%d", __FILE__, __LINE__);
		debug ("Create query %lx linked to lookup %lx",
		       (long int)query, (long int)lookup);
		query->lookup = lookup;
		query->working = ISC_FALSE;
		query->waiting_connect = ISC_FALSE;
		query->first_pass = ISC_TRUE;
		query->first_soa_rcvd = ISC_FALSE;
		query->servname = serv->servername;
		ISC_LIST_INIT(query->sendlist);
		ISC_LIST_INIT(query->recvlist);
		ISC_LIST_INIT(query->lengthlist);
		query->sock = NULL;

		isc_buffer_init(&query->recvbuf, query->recvspace, COMMSIZE);
		isc_buffer_init(&query->lengthbuf, query->lengthspace, 2);
		isc_buffer_init(&query->slbuf, query->slspace, 2);

		ISC_LIST_ENQUEUE(lookup->q, query, link);
	}
	if (!ISC_LIST_EMPTY(lookup->q) && qr) {
		printmessage (ISC_LIST_HEAD(lookup->q), lookup->sendmsg,
			      ISC_TRUE);
	}
}	

static void
send_done(isc_task_t *task, isc_event_t *event) {
	UNUSED(task);
	isc_event_free(&event);

	debug("send_done()");
}

static void
cancel_lookup(dig_lookup_t *lookup) {
	dig_query_t *query=NULL;

	debug("cancel_lookup()");
	for (query = ISC_LIST_HEAD(lookup->q);
	     query != NULL;
	     query = ISC_LIST_NEXT(query, link)) {
		if (query->working) {
			debug ("Cancelling a worker.");
			isc_socket_cancel(query->sock, task,
					  ISC_SOCKCANCEL_ALL);
		}
	}
	lookup->pending = ISC_FALSE;
	lookup->retries = 0;
	check_next_lookup(lookup);
}

static void
recv_done(isc_task_t *task, isc_event_t *event);

static void
connect_timeout(isc_task_t *task, isc_event_t *event);

void
send_udp(dig_lookup_t *lookup) {
	dig_query_t *query;
	isc_result_t result;

	debug ("send_udp()");

	isc_interval_set(&lookup->interval, timeout, 0);
	result = isc_timer_create(timermgr, isc_timertype_once, NULL,
				  &lookup->interval, task, connect_timeout,
				  lookup, &lookup->timer);
	check_result(result, "isc_timer_create");
	for (query = ISC_LIST_HEAD(lookup->q);
	     query != NULL;
	     query = ISC_LIST_NEXT(query, link)) {
		debug ("Working on lookup %lx, query %lx",
		       (long int)query->lookup, (long int)query);
		ISC_LIST_ENQUEUE(query->recvlist, &query->recvbuf, link);
		query->working = ISC_TRUE;
		debug ("recving with lookup=%lx, query=%lx",
		       (long int)query->lookup, (long int)query);
		result = isc_socket_recvv(query->sock, &query->recvlist, 1,
					  task, recv_done, query);
		check_result(result, "isc_socket_recvv");
		sendcount++;
		debug("Sent count number %d", sendcount);
#ifdef TWIDDLE
		if (twiddle) {
			twiddlebuf(lookup->sendbuf);
		}
#endif
		ISC_LIST_ENQUEUE(query->sendlist, &lookup->sendbuf, link);
		debug("Sending a request.");
		result = isc_time_now(&query->time_sent);
		check_result(result, "isc_time_now");
		result = isc_socket_sendtov(query->sock, &query->sendlist,
					    task, send_done, query,
					    &query->sockaddr, NULL);
		check_result(result, "isc_socket_sendtov");
	}
}

/* connect_timeout is used for both UDP recieves and TCP connects. */
static void
connect_timeout(isc_task_t *task, isc_event_t *event) {
	dig_lookup_t *lookup=NULL, *next=NULL;
	dig_query_t *q=NULL;
	isc_result_t result;
	isc_buffer_t *b=NULL;
	isc_region_t r;

	REQUIRE(event->ev_type == ISC_TIMEREVENT_IDLE);

	debug("connect_timeout()");
	lookup = event->ev_arg;

	debug ("Buffer Allocate connect_timeout");
	result = isc_buffer_allocate(mctx, &b, 256);
	check_result(result, "isc_buffer_allocate");
	for (q = ISC_LIST_HEAD(lookup->q);
	     q != NULL;
	     q = ISC_LIST_NEXT(q, link)) {
		if (q->working) {
			if (!free_now) {
				isc_buffer_clear(b);
				result = isc_sockaddr_totext(&q->sockaddr, b);
				check_result(result, "isc_sockaddr_totext");
				isc_buffer_usedregion(b, &r);
				if (q->lookup->retries > 1)
					printf(";; Connection to server %.*s "
					       "for %s timed out.  "
					       "Retrying %d.\n",
					       (int)r.length, r.base,
					       q->lookup->textname,
					       q->lookup->retries-1);
				else {
					if (lookup->tcp_mode) {
						printf(";; Connection to "
						       "server %.*s "
						       "for %s timed out.  "
						       "Giving up.\n",
						       (int)r.length, r.base,
						       q->lookup->textname);
					} else {
						printf(";; Connection to "
						       "server %.*s "
						       "for %s timed out.  "
						       "Trying TCP.\n",
						       (int)r.length, r.base,
						       q->lookup->textname);
						next = requeue_lookup
							(lookup,ISC_TRUE);
						next->tcp_mode = ISC_TRUE;
					}
				}
			}
			isc_socket_cancel(q->sock, task,
					  ISC_SOCKCANCEL_ALL);
		}
	}
	ENSURE(lookup->timer != NULL);
	isc_timer_detach(&lookup->timer);
	isc_buffer_free(&b);
	isc_event_free(&event);
	debug ("Done with connect_timeout()");
}

static void
tcp_length_done(isc_task_t *task, isc_event_t *event) { 
	isc_socketevent_t *sevent;
	isc_buffer_t *b=NULL;
	isc_region_t r;
	isc_result_t result;
	dig_query_t *query=NULL;
	isc_uint16_t length;

	REQUIRE(event->ev_type == ISC_SOCKEVENT_RECVDONE);

	UNUSED(task);

	debug("tcp_length_done()");

	if (free_now)
		return;

	sevent = (isc_socketevent_t *)event;	

	query = event->ev_arg;

	if (sevent->result == ISC_R_CANCELED) {
		query->working = ISC_FALSE;
		check_next_lookup(query->lookup);
		isc_event_free(&event);
		return;
	}
	if (sevent->result != ISC_R_SUCCESS) {
		debug ("Buffer Allocate connect_timeout");
		result = isc_buffer_allocate(mctx, &b, 256);
		check_result(result, "isc_buffer_allocate");
		result = isc_sockaddr_totext(&query->sockaddr, b);
		check_result(result, "isc_sockaddr_totext");
		isc_buffer_usedregion(b, &r);
		printf("%.*s: %s\n", (int)r.length, r.base,
		       isc_result_totext(sevent->result));
		isc_buffer_free(&b);
		query->working = ISC_FALSE;
		isc_socket_detach(&query->sock);
		check_next_lookup(query->lookup);
		isc_event_free(&event);
		return;
	}
	b = ISC_LIST_HEAD(sevent->bufferlist);
	ISC_LIST_DEQUEUE(sevent->bufferlist, &query->lengthbuf, link);
	length = isc_buffer_getuint16(b);
	if (length > COMMSIZE) {
		isc_event_free (&event);
		fatal ("Length of %X was longer than I can handle!",
		       length);
	}
	/*
	 * Even though the buffer was already init'ed, we need
	 * to redo it now, to force the length we want.
	 */
	isc_buffer_invalidate(&query->recvbuf);
	isc_buffer_init(&query->recvbuf, query->recvspace, length);
	ENSURE(ISC_LIST_EMPTY(query->recvlist));
	ISC_LIST_ENQUEUE(query->recvlist, &query->recvbuf, link);
	debug ("recving with lookup=%lx, query=%lx",
	       (long int)query->lookup, (long int)query);
	result = isc_socket_recvv(query->sock, &query->recvlist, length, task,
				  recv_done, query);
	check_result(result, "isc_socket_recvv");
	debug("Resubmitted recv request with length %d", length);
	isc_event_free(&event);
}

static void
launch_next_query(dig_query_t *query, isc_boolean_t include_question) {
	isc_result_t result;

	debug("launch_next_query()");

	if (free_now)
		return;

	if (!query->lookup->pending) {
		debug("Ignoring launch_next_query because !pending.");
		isc_socket_detach(&query->sock);
		query->working = ISC_FALSE;
		query->waiting_connect = ISC_FALSE;
		check_next_lookup(query->lookup);
		return;
	}

	isc_buffer_clear(&query->slbuf);
	isc_buffer_clear(&query->lengthbuf);
	isc_buffer_putuint16(&query->slbuf, query->lookup->sendbuf.used);
	ISC_LIST_ENQUEUE(query->sendlist, &query->slbuf, link);
	if (include_question) {
#ifdef TWIDDLE
		if (twiddle) {
			twiddlebuf(query->lookup->sendbuf);
		}
#endif
		ISC_LIST_ENQUEUE(query->sendlist, &query->lookup->sendbuf,
				 link);
	}
	ISC_LIST_ENQUEUE(query->lengthlist, &query->lengthbuf, link);

	result = isc_socket_recvv(query->sock, &query->lengthlist, 0, task,
				  tcp_length_done, query);
	check_result(result, "isc_socket_recvv");
	sendcount++;
	if (!query->first_soa_rcvd) {
		debug("Sending a request.");
		result = isc_time_now(&query->time_sent);
		check_result(result, "isc_time_now");
		result = isc_socket_sendv(query->sock, &query->sendlist, task,
					  send_done, query);
		check_result(result, "isc_socket_recvv");
	}
	query->waiting_connect = ISC_FALSE;
	check_next_lookup(query->lookup);
	return;
}
	
static void
connect_done(isc_task_t *task, isc_event_t *event) {
	isc_result_t result;
	isc_socketevent_t *sevent=NULL;
	dig_query_t *query=NULL;
	isc_buffer_t *b=NULL;
	isc_region_t r;

	UNUSED(task);

	REQUIRE(event->ev_type == ISC_SOCKEVENT_CONNECT);

	if (free_now)
		return;

	sevent = (isc_socketevent_t *)event;
	query = sevent->ev_arg;

	REQUIRE(query->waiting_connect);

	query->waiting_connect = ISC_FALSE;

	debug("connect_done()");
	if (sevent->result != ISC_R_SUCCESS) {
		debug ("Buffer Allocate connect_timeout");
		result = isc_buffer_allocate(mctx, &b, 256);
		check_result(result, "isc_buffer_allocate");
		result = isc_sockaddr_totext(&query->sockaddr, b);
		check_result(result, "isc_sockaddr_totext");
		isc_buffer_usedregion(b, &r);
		printf(";; Connection to server %.*s for %s failed: %s.\n",
		       (int)r.length, r.base, query->lookup->textname,
		       isc_result_totext(sevent->result));
		if (exitcode < 9)
			exitcode = 9;
		isc_buffer_free(&b);
		query->working = ISC_FALSE;
		query->waiting_connect = ISC_FALSE;
		check_next_lookup(query->lookup);
		isc_event_free(&event);
		return;
	}
	isc_event_free(&event);
	launch_next_query(query, ISC_TRUE);
}

static isc_boolean_t
msg_contains_soa(dns_message_t *msg, dig_query_t *query) {
	isc_result_t result;
	dns_name_t *name=NULL;

	debug("msg_contains_soa()");

	result = dns_message_findname(msg, DNS_SECTION_ANSWER,
				      query->lookup->name, dns_rdatatype_soa,
				      0, &name, NULL);
	if (result == ISC_R_SUCCESS) {
		debug("Found SOA", stderr);
		return (ISC_TRUE);
	} else {
		debug("Didn't find SOA, result=%d:%s",
			result, dns_result_totext(result));
		return (ISC_FALSE);
	}
	
}

static void
recv_done(isc_task_t *task, isc_event_t *event) {
	isc_socketevent_t *sevent = NULL;
	dig_query_t *query = NULL;
	isc_buffer_t *b = NULL;
	dns_message_t *msg = NULL;
	isc_result_t result;
	isc_buffer_t ab;
	char abspace[MXNAME];
	isc_region_t r;
	dig_lookup_t *n;
	
	UNUSED (task);

	if (free_now)
		return;

	query = event->ev_arg;
	debug("recv_done(lookup=%lx, query=%lx)",
	      (long int)query->lookup, (long int)query);

	if (free_now) {
		debug("Bailing out, since freeing now.");
		isc_event_free (&event);
		return;
	}

	sendcount--;
	debug("In recv_done, counter down to %d", sendcount);
	REQUIRE(event->ev_type == ISC_SOCKEVENT_RECVDONE);
	sevent = (isc_socketevent_t *)event;

	if (!query->lookup->pending && !query->lookup->ns_search_only) {

		debug("No longer pending.  Got %s",
			isc_result_totext(sevent->result));
		query->working = ISC_FALSE;
		query->waiting_connect = ISC_FALSE;
		
		cancel_lookup(query->lookup);
		isc_event_free(&event);
		return;
	}

	if (sevent->result == ISC_R_SUCCESS) {
		b = ISC_LIST_HEAD(sevent->bufferlist);
		ISC_LIST_DEQUEUE(sevent->bufferlist, &query->recvbuf, link);
		result = dns_message_create(mctx, DNS_MESSAGE_INTENTPARSE,
					    &msg);
		check_result(result, "dns_message_create");
		debug ("Before parse starts");
		result = dns_message_parse(msg, b, ISC_TRUE);
		if (result != ISC_R_SUCCESS) {
			printf (";; Got bad UDP packet:\n");
			hex_dump(b);
			isc_event_free(&event);
			query->working = ISC_FALSE;
			query->waiting_connect = ISC_FALSE;
			if (!query->lookup->tcp_mode) {
				printf (";; Retrying in TCP mode.\n");
				n = requeue_lookup(query->lookup, ISC_TRUE);
				n->tcp_mode = ISC_TRUE;
			}
			cancel_lookup(query->lookup);
			dns_message_destroy(&msg);
			return;
		}
		debug ("After parse has started");
		if (query->lookup->xfr_q == NULL)
			query->lookup->xfr_q = query;
		if (query->lookup->xfr_q == query) {
			if (query->lookup->trace) {
				if (show_details || ((dns_message_firstname
						  (msg, DNS_SECTION_ANSWER)==
						  ISC_R_SUCCESS) &&
						  !query->lookup->trace_root)) {
					printmessage(query, msg, ISC_TRUE);
				}
				if ((msg->rcode != 0) &&
				    (query->lookup->origin != NULL)) {
					next_origin(msg, query);
				} else {
					result = dns_message_firstname
						(msg,DNS_SECTION_ANSWER);
					if ((result != ISC_R_SUCCESS) ||
					    query->lookup->trace_root)
						followup_lookup(msg, query,
							DNS_SECTION_AUTHORITY);
				}
			} else if ((msg->rcode != 0) &&
				 (query->lookup->origin != NULL)) {
				next_origin(msg, query);
				if (show_details) {
				       printmessage(query, msg, ISC_TRUE);
				}
			} else {
				if (query->first_soa_rcvd &&
				    query->lookup->doing_xfr)
					printmessage(query, msg, ISC_FALSE);
				else
					printmessage(query, msg, ISC_TRUE);
			}
		} else if (( dns_message_firstname(msg, DNS_SECTION_ANSWER)
			    == ISC_R_SUCCESS) &&
			   query->lookup->ns_search_only &&
			   !query->lookup->trace_root ) {
			printmessage (query, msg, ISC_TRUE);
		}
		
#ifdef DEBUG
		if (query->lookup->pending)
			debug("Still pending.");
#endif
		if (query->lookup->doing_xfr) {
			if (!query->first_soa_rcvd) {
				debug("Not yet got first SOA");
				if (!msg_contains_soa(msg, query)) {
					puts("; Transfer failed.  "
					     "Didn't start with SOA answer.");
					query->working = ISC_FALSE;
					cancel_lookup(query->lookup);
					isc_event_free (&event);
					dns_message_destroy (&msg);
					return;
					launch_next_query(query, ISC_FALSE);
				}
				else {
					query->first_soa_rcvd = ISC_TRUE;
					launch_next_query(query, ISC_FALSE);
				}
			} 
			else {
				if (msg_contains_soa(msg, query)) {
					isc_buffer_init(&ab, abspace, MXNAME);
					result = isc_sockaddr_totext(&sevent->
								     address,
								     &ab);
					check_result(result,
						     "isc_sockaddr_totext");
					isc_buffer_usedregion(&ab, &r);
					received(b->used, r.length,
						 (char *)r.base, query);
					query->working = ISC_FALSE;
					cancel_lookup(query->lookup);
					isc_event_free(&event);
					dns_message_destroy (&msg);
					return;
				}
				else {
					launch_next_query(query, ISC_FALSE);
				}
			}
		}
		else {
			if ((msg->rcode == 0) ||
			    (query->lookup->origin == NULL)) {
				isc_buffer_init(&ab, abspace, MXNAME);
				result = isc_sockaddr_totext(&sevent->address,
							     &ab);
				check_result(result, "isc_sockaddr_totext");
				isc_buffer_usedregion(&ab, &r);
				received(b->used, r.length, (char *)r.base,
					 query);
			}
			query->working = ISC_FALSE;
			query->lookup->pending = ISC_FALSE;
			if (!query->lookup->ns_search_only ||
			    query->lookup->trace_root ) {
				cancel_lookup(query->lookup);
			}
			check_next_lookup(query->lookup);
		}
		dns_message_destroy(&msg);
		isc_event_free(&event);
		return;
	}
	/* In truth, we should never get into the CANCELED routine, since
	   the cancel_lookup() routine clears the pending flag. */
	if (sevent->result == ISC_R_CANCELED) {
		debug ("In cancel handler");
		query->working = ISC_FALSE;
		query->waiting_connect = ISC_FALSE;
		check_next_lookup(query->lookup);
		isc_event_free(&event);
		return;
	}
	isc_event_free(&event);
	fatal("recv_done got result %s",
	      isc_result_totext(sevent->result));
}

void
get_address(char *host, in_port_t port, isc_sockaddr_t *sockaddr) {
	struct in_addr in4;
	struct in6_addr in6;
	struct hostent *he;

	debug("get_address()");

	if (have_ipv6 && inet_pton(AF_INET6, host, &in6) == 1)
		isc_sockaddr_fromin6(sockaddr, &in6, port);
	else if (inet_pton(AF_INET, host, &in4) == 1)
		isc_sockaddr_fromin(sockaddr, &in4, port);
	else {
		he = gethostbyname(host);
		if (he == NULL)
		     fatal("Couldn't look up your server host %s.  errno=%d",
			      host, h_errno);
		INSIST(he->h_addrtype == AF_INET);
		isc_sockaddr_fromin(sockaddr,
				    (struct in_addr *)(he->h_addr_list[0]),
				    port);
	}
}

static void
do_lookup_tcp(dig_lookup_t *lookup) {
	dig_query_t *query;
	isc_result_t result;

	debug("do_lookup_tcp()");
	lookup->pending = ISC_TRUE;
	isc_interval_set(&lookup->interval, timeout, 0);
	result = isc_timer_create(timermgr, isc_timertype_once, NULL,
				  &lookup->interval, task, connect_timeout,
				  lookup, &lookup->timer);
	check_result(result, "isc_timer_create");

	for (query = ISC_LIST_HEAD(lookup->q);
	     query != NULL;
	     query = ISC_LIST_NEXT(query, link)) {
		query->working = ISC_TRUE;
		query->waiting_connect = ISC_TRUE;
		get_address(query->servname, port, &query->sockaddr);

		result = isc_socket_create(socketmgr,
					   isc_sockaddr_pf(&query->sockaddr),
					   isc_sockettype_tcp, &query->sock) ;
		check_result(result, "isc_socket_create");
		if (specified_source) {
			result = isc_socket_bind(query->sock, &bind_address);
			check_result(result, "isc_socket_bind");
		}
		result = isc_socket_connect(query->sock, &query->sockaddr,
					    task, connect_done, query);
		check_result (result, "isc_socket_connect");
	}
}

static void
do_lookup_udp(dig_lookup_t *lookup) {
	dig_query_t *query;
	isc_result_t result;

#ifdef DEBUG
	debug("do_lookup_udp()");
	if (lookup->tcp_mode)
		debug("I'm starting UDP with tcp_mode set!!!");
#endif
	lookup->pending = ISC_TRUE;

	for (query = ISC_LIST_HEAD(lookup->q);
	     query != NULL;
	     query = ISC_LIST_NEXT(query, link)) {
		query->working = ISC_TRUE;
		query->waiting_connect = ISC_FALSE;
		get_address(query->servname, port, &query->sockaddr);

		result = isc_socket_create(socketmgr,
					   isc_sockaddr_pf(&query->sockaddr),
					   isc_sockettype_udp, &query->sock) ;
		check_result(result, "isc_socket_create");
		if (specified_source) {
			result = isc_socket_bind(query->sock, &bind_address);
			check_result(result, "isc_socket_bind");
		}
	}

	send_udp(lookup);
}

void
do_lookup(dig_lookup_t *lookup) {

	REQUIRE (lookup != NULL);

	debug ("do_lookup()");
	if (lookup->tcp_mode)
		do_lookup_tcp(lookup);
	else
		do_lookup_udp(lookup);
}

void
start_lookup(void) {
	dig_lookup_t *lookup;

	debug ("start_lookup()");

	if (free_now)
		return;

	lookup = ISC_LIST_HEAD(lookup_list);
	if (lookup != NULL) {
		setup_lookup(lookup);
		do_lookup(lookup);
	}
}
     
void
free_lists(int _exitcode) {
	void *ptr;
	dig_lookup_t *l;
	dig_query_t *q;
	dig_server_t *s;
	dig_searchlist_t *o;

	debug("free_lists()");

	if (free_now)
		return;

	free_now = ISC_TRUE;

	l = ISC_LIST_HEAD(lookup_list);
	while (l != NULL) {
		q = ISC_LIST_HEAD(l->q);
		while (q != NULL) {
			if (q->sock != NULL) {
				isc_socket_cancel(q->sock, NULL,
						  ISC_SOCKCANCEL_ALL);
				isc_socket_detach(&q->sock);
			}
			if (ISC_LINK_LINKED(&q->recvbuf, link))
				ISC_LIST_DEQUEUE(q->recvlist, &q->recvbuf,
						 link);
			if (ISC_LINK_LINKED(&q->lengthbuf, link))
				ISC_LIST_DEQUEUE(q->lengthlist, &q->lengthbuf,
						 link);
			isc_buffer_invalidate(&q->recvbuf);
			isc_buffer_invalidate(&q->lengthbuf);
			ptr = q;
			q = ISC_LIST_NEXT(q, link);
			isc_mem_free(mctx, ptr);
		}
		if (l->use_my_server_list) {
			s = ISC_LIST_HEAD(l->my_server_list);
			while (s != NULL) {
				ptr = s;
				s = ISC_LIST_NEXT(s, link);
				isc_mem_free(mctx, ptr);

			}
		}
		if (l->sendmsg != NULL)
			dns_message_destroy (&l->sendmsg);
		if (l->timer != NULL)
			isc_timer_detach (&l->timer);
		ptr = l;
		l = ISC_LIST_NEXT(l, link);
		isc_mem_free(mctx, ptr);
	}
	s = ISC_LIST_HEAD(server_list);
	while (s != NULL) {
		ptr = s;
		s = ISC_LIST_NEXT(s, link);
		isc_mem_free(mctx, ptr);
	}
	o = ISC_LIST_HEAD(search_list);
	while (o != NULL) {
		ptr = o;
		o = ISC_LIST_NEXT(o, link);
		isc_mem_free(mctx, ptr);
	}
	if (socketmgr != NULL)
		isc_socketmgr_destroy(&socketmgr);
	if (timermgr != NULL)
		isc_timermgr_destroy(&timermgr);
	if (task != NULL)
		isc_task_detach(&task);
	if (taskmgr != NULL)
		isc_taskmgr_destroy(&taskmgr);

#ifdef MEMDEBUG
	isc_mem_stats(mctx, stderr);
#endif
	isc_app_finish();
	if (mctx != NULL)
		isc_mem_destroy(&mctx);

	debug("Getting ready to exit, code=%d",_exitcode);
	if (_exitcode != 0)
		exit(_exitcode);
}