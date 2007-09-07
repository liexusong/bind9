/*
 * Issues to be discussed:
 * - Return values.  There seems to be no standard for return value (RFC2553)
 *   but INRIA implementation returns EAI_xxx defined for getaddrinfo().
 */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by WIDE Project and
 *    its contributors.
 * 4. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <lwres/lwres.h>
#include <lwres/net.h>
#include <lwres/netdb.h>

#include "assert_p.h"

#define SUCCESS 0

static struct afd {
	int a_af;
	size_t a_addrlen;
	size_t a_socklen;
} afdl [] = {
	/* first entry is linked last... */
	{AF_INET, sizeof(struct in_addr), sizeof(struct sockaddr_in)},
	{AF_INET6, sizeof(struct in6_addr), sizeof(struct sockaddr_in6)},
	{0, 0, 0},
};

#define ENI_NOSOCKET 	0
#define ENI_NOSERVNAME	1
#define ENI_NOHOSTNAME	2
#define ENI_MEMORY	3
#define ENI_SYSTEM	4
#define ENI_FAMILY	5
#define ENI_SALEN	6

/*
 * The test against 0 is there to keep the Solaris compiler
 * from complaining about "end-of-loop code not reached".
 */
#define ERR(code) \
	do { result = (code);			\
		if (result != 0) goto cleanup;	\
	} while (0)

int
lwres_getnameinfo(const struct sockaddr *sa, size_t salen, char *host,
	    size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct afd *afd;
	struct servent *sp;
	u_short port;
#ifdef ISC_PLATFORM_HAVESALEN
	size_t len;
#endif
	int family, i;
	void *addr;
	char *p;
#if 0
	u_long v4a;
	u_char pfx;
#endif
	char numserv[sizeof("65000")];
	char numaddr[sizeof("abcd:abcd:abcd:abcd:abcd:abcd:255.255.255.255")];
	char *proto;
	lwres_uint32_t lwf = 0;
	lwres_context_t *lwrctx = NULL;
	lwres_gnbaresponse_t *by = NULL;
	int result = SUCCESS;
	int n;

	if (sa == NULL)
		ERR(ENI_NOSOCKET);

#ifdef ISC_PLATFORM_HAVESALEN
	len = sa->sa_len;
	if (len != salen)
		ERR(ENI_SALEN);
#endif
	
	family = sa->sa_family;
	for (i = 0; afdl[i].a_af; i++)
		if (afdl[i].a_af == family) {
			afd = &afdl[i];
			goto found;
		}
	ERR(ENI_FAMILY);
	
 found:
	if (salen != afd->a_socklen)
		ERR(ENI_SALEN);
	
	switch (family) {
	case AF_INET:
		port = ((struct sockaddr_in *)sa)->sin_port;
		addr = &((struct sockaddr_in *)sa)->sin_addr.s_addr;
		break;

	case AF_INET6:
		port = ((struct sockaddr_in6 *)sa)->sin6_port;
		addr = ((struct sockaddr_in6 *)sa)->sin6_addr.s6_addr;
		break;

	default:
		port = 0;
		addr = NULL;
		INSIST(0);
	}
	proto = (flags & NI_DGRAM) ? "udp" : "tcp";

	if (serv == NULL || servlen == 0) {
		/* Caller does not want service. */
	} else if ((flags & NI_NUMERICSERV) != 0 ||
		   (sp = getservbyport(port, proto)) == NULL) {
		sprintf(numserv, "%d", ntohs(port));
		if ((strlen(numserv) + 1) > servlen)
			ERR(ENI_MEMORY);
		strcpy(serv, numserv);
	} else {
		if ((strlen(sp->s_name) + 1) > servlen)
			ERR(ENI_MEMORY);
		strcpy(serv, sp->s_name);
	}

#if 0
	switch (sa->sa_family) {
	case AF_INET:
		v4a = ((struct sockaddr_in *)sa)->sin_addr.s_addr;
		if (IN_MULTICAST(v4a) || IN_EXPERIMENTAL(v4a))
			flags |= NI_NUMERICHOST;
		v4a >>= IN_CLASSA_NSHIFT;
		if (v4a == 0 || v4a == IN_LOOPBACKNET)
			flags |= NI_NUMERICHOST;			
		break;

	case AF_INET6:
		pfx = ((struct sockaddr_in6 *)sa)->sin6_addr.s6_addr[0];
		if (pfx == 0 || pfx == 0xfe || pfx == 0xff)
			flags |= NI_NUMERICHOST;
		break;
	}
#endif

	if (host == NULL || hostlen == 0) {
		/* what should we do? */
	} else if (flags & NI_NUMERICHOST) {
		if (lwres_net_ntop(afd->a_af, addr, numaddr, sizeof(numaddr))
		    == NULL)
			ERR(ENI_SYSTEM);
		if (strlen(numaddr) > hostlen)
			ERR(ENI_MEMORY);
		strcpy(host, numaddr);
	} else {
		switch (family) {
		case AF_INET:
			lwf = LWRES_ADDRTYPE_V4;
			break;
		case AF_INET6:
			lwf = LWRES_ADDRTYPE_V6;
			break;
		default:
			INSIST(0);
		}

		n = lwres_context_create(&lwrctx, NULL, NULL, NULL);
		if (n == 0)
			n = lwres_getnamebyaddr(lwrctx, lwf, afd->a_addrlen,
						addr, &by);
		if (n == 0) {
			if (flags & NI_NOFQDN) {
				p = strchr(by->realname, '.');
				if (p)
					*p = '\0';
			}
			if ((strlen(by->realname) + 1) > hostlen)
				ERR(ENI_MEMORY);
			strcpy(host, by->realname);
		} else {
			if (flags & NI_NAMEREQD)
				ERR(ENI_NOHOSTNAME);
			if (lwres_net_ntop(afd->a_af, addr, numaddr,
					   sizeof(numaddr))
			    == NULL)
				ERR(ENI_NOHOSTNAME);
			if ((strlen(numaddr) + 1) > hostlen)
				ERR(ENI_MEMORY);
			strcpy(host, numaddr);
		}
		lwres_context_destroy(&lwrctx);
	}
	result = SUCCESS;
 cleanup:
	if (by != NULL)
		lwres_gnbaresponse_free(lwrctx, &by);
	if (lwrctx != NULL)
		lwres_context_destroy(&lwrctx);
	return (result);
}