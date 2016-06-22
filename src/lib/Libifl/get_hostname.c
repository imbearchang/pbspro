/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
/**
 * @file	get_hostname.c
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "portability.h"
#include <netdb.h>
#include <string.h>
#include "pbs_ifl.h"
#include "pbs_internal.h"


/**
 * @brief
 * 	-get_fullhostname - get the fully qualified name of a host.
 *
 * @param[in] shortname - short name of host
 * @param[in] namebuf - buffer holding local name of host
 * @param[in] bufsize - buffer size
 *
 * @return	int
 * @retval	0	if success
 *			host name in character buffer pointed to by namebuf
 * @retval	-1	if error
 */

int
get_fullhostname(char *shortname, char *namebuf, int bufsize)
{
	int		i;
	char	       *pbkslh = 0;
	char           *pcolon = 0;
	char            extname[PBS_MAXHOSTNAME+1] = {'\0'};
	char            localname[PBS_MAXHOSTNAME+1] = {'\0'};
#if defined(__hpux)
	struct hostent *phe;
	struct in_addr  ina;
#else
	struct addrinfo *aip, *pai;
	struct addrinfo hints;
	struct sockaddr_in *inp;
#endif

	if ((pcolon = strchr(shortname, (int)':')) != NULL) {
		*pcolon = '\0';
		if (*(pcolon-1) == '\\')
			*(pbkslh = pcolon-1) = '\0';
	}

#if defined(__hpux)
	phe = gethostbyname(shortname);
	if (phe == (struct hostent *)0)
		return (-1);
#else
	memset(&hints, 0, sizeof(struct addrinfo));
	/*
	 *	Why do we use AF_UNSPEC rather than AF_INET?  Some
	 *	implementations of getaddrinfo() will take an IPv6
	 *	address and map it to an IPv4 one if we ask for AF_INET
	 *	only.  We don't want that - we want only the addresses
	 *	that are genuinely, natively, IPv4 so we start with
	 *	AF_UNSPEC and filter ai_family below.
	 */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (getaddrinfo(shortname, NULL, &hints, &pai) != 0)
		return (-1);
#endif

	if (pcolon) {
		*pcolon = ':';	/* replace the colon */
		if (pbkslh)
			*pbkslh = '\\';
	}

#if defined(__hpux)
	(void)memcpy((char *)&ina, *phe->h_addr_list, phe->h_length);
	if ((phe = gethostbyaddr((char *)&ina, phe->h_length, phe->h_addrtype))
		== 0)
		return (-1);

	if ((size_t)bufsize < strlen(phe->h_name))
		return (-1);
	for (i=0; i<bufsize; i++) {
		*(namebuf+i) = tolower((int)*(phe->h_name+i));
		if (*(phe->h_name+i) == '\0')
			break;
	}
#else
	/*
	 *	This loop tries to find a non-loopback IPv4 address suitable
	 *	for use by, in particular, pbs_server (which doesn't want to
	 *	name its jobs <N>.localhost), so we ignore non-IPv4 addresses,
	 *	those that aren't invertible, and those on a loopback net.
	 */
	for (aip = pai; aip != NULL; aip = aip->ai_next) {
		if (aip->ai_family != AF_INET)
			continue;  /* skip non-IPv4 addresses */
		if (getnameinfo(aip->ai_addr, aip->ai_addrlen, namebuf,
			bufsize, NULL, 0, 0) != 0)
			continue; /* skip non-invertible addresses */
		inp = (struct sockaddr_in *) aip->ai_addr;
		if (ntohl(inp->sin_addr.s_addr) >> 24 != IN_LOOPBACKNET) {
			strncpy(extname, namebuf, (sizeof(extname) - 1));
			break;          /* skip loopback addresses */
		} else
			strncpy(localname, namebuf, (sizeof(localname) - 1));
	}
	freeaddrinfo(pai);
	if (extname[0] == '\0')
		strncpy(namebuf, localname, bufsize);
	else
		strncpy(namebuf, extname, bufsize);

	if (namebuf[0] == '\0')
		return (-1);

	for (i=0; i<bufsize; i++) {
		*(namebuf+i) = tolower((int)*(namebuf+i));
		if (*(namebuf+i) == '\0')
			break;
	}
#endif
	*(namebuf + bufsize) = '\0';	/* insure null terminated */
	return (0);
}