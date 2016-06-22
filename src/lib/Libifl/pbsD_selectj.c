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
 * @file	pbsD_selectj.c
 * @brief
 *	This file contines two main library entries:
 *		pbs_selectjob()
 *		pbs_selstat()
 *
 *
 *	pbs_selectjob() - the SelectJob request
 *		Return a list of job ids that meet certain selection criteria.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


static int PBSD_select_put(int, int, struct attropl *, struct attrl *, char *);
static char **PBSD_select_get(int);

/**
 * @brief
 *	-the SelectJob request
 *	Return a list of job ids that meet certain selection criteria.
 *
 * @param[in] c - communication handle
 * @param[in] attrib - pointer to attropl structure(selection criteria)
 * @param[in] extend - extend string to encode req
 *
 * @return	string
 * @retval	job ids		success
 * @retval	NULL		error
 *
 */
char **
pbs_selectjob(int c, struct attropl *attrib, char *extend)
{
	char **ret = NULL;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return ((char **)0);

	/* first verify the attributes, if verification is enabled */
	if (pbs_verify_attributes(c, PBS_BATCH_SelectJobs, MGR_OBJ_JOB,
		MGR_CMD_NONE, attrib))
		return ((char **)0);

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return ((char **)0);

	if (PBSD_select_put(c, PBS_BATCH_SelectJobs, attrib, (struct attrl *)0, extend) == 0)
		ret = PBSD_select_get(c);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return ((char **) 0);

	return ret;
}

/**
 * @brief
 * 	-pbs_selstat() - Selectable status
 *	Return status information for jobs that meet certain selection
 *	criteria.  This is a short-cut combination of pbs_selecljob()
 *	and repeated pbs_statjob().
 *
 * @param[in] c - communication handle
 * @param[in] attrib - pointer to attropl structure(selection criteria)
 * @param[in] extend - extend string to encode req
 * @param[in] rattrib - list of attributes to return
 *
 * @return      structure handle
 * @retval      list of attr	success
 * @retval      NULL		error
 *
 */

struct batch_status * 
pbs_selstat(int c, struct attropl *attrib, struct attrl   *rattrib, char *extend)
{
	struct batch_status *ret = NULL;
	extern struct batch_status *PBSD_status_get(int c);

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return ((struct batch_status *)0);

	/* first verify the attributes, if verification is enabled */
	if (pbs_verify_attributes(c, PBS_BATCH_SelectJobs, MGR_OBJ_JOB,
		MGR_CMD_NONE, attrib))
		return ((struct batch_status *)0);

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return ((struct batch_status *)0);


	if (PBSD_select_put(c, PBS_BATCH_SelStat, attrib, rattrib, extend) == 0)
		ret = PBSD_status_get(c);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return ((struct batch_status *)0);

	return ret;
}


/**
 * @brief
 *	-encode and puts selectjob request  data 
 *
 * @param[in] c - communication handle
 * @param[in] type - type of request
 * @param[in] attrib - pointer to attropl structure(selection criteria)
 * @param[in] extend - extend string to encode req
 * @param[in] rattrib - list of attributes to return
 *
 * @return      int
 * @retval      0	success
 * @retval      !0	error
 *
 */
static int
PBSD_select_put(int c, int type, struct attropl *attrib, 
			struct attrl *rattrib, char *extend)
{
	int rc;
	int sock;

	sock = connection[c].ch_socket;

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_setup(sock);

	if ((rc = encode_DIS_ReqHdr(sock, type, pbs_current_user)) ||
		(rc = encode_DIS_attropl(sock, attrib)) ||
		(rc = encode_DIS_attrl(sock, rattrib))  ||
		(rc = encode_DIS_ReqExtend(sock, extend))) {
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		return (pbs_errno);
	}

	/* write data */

	if (DIS_tcp_wflush(sock)) {
		return (pbs_errno = PBSE_PROTOCOL);
	}

	return 0;
}

/**
 * @brief
 *	-reads selectjob reply from stream
 *
 * @param[in] c - communication handle
 *
 * @return	string list
 * @retval	list of strings		success
 * @retval	NULL			error
 *
 */
static char **
PBSD_select_get(int c)
{
	int   i;
	struct batch_reply *reply;
	int   njobs;
	char *sp;
	int   stringtot;
	size_t totsize;
	struct brp_select *sr;
	char **retval = (char **)NULL;

	/* read reply from stream */

	reply = PBSD_rdrpy(c);
	if (reply == NULL) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (reply->brp_choice != BATCH_REPLY_CHOICE_NULL &&
		reply->brp_choice != BATCH_REPLY_CHOICE_Text &&
		reply->brp_choice != BATCH_REPLY_CHOICE_Select) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (connection[c].ch_errno == 0) {
		/* process the reply -- first, build a linked
		 list of the strings we extract from the reply, keeping
		 track of the amount of space used...
		 */
		stringtot = 0;
		njobs = 0;
		sr = reply->brp_un.brp_select;
		while (sr != (struct brp_select *)NULL) {
			stringtot += strlen(sr->brp_jobid) + 1;
			njobs++;
			sr = sr->brp_next;
		}
		/* ...then copy all the strings into one of "Bob's
		 structures", freeing all strings we just allocated...
		 */

		totsize = stringtot + (njobs+1) * (sizeof(char *));
		retval = (char **)malloc(totsize);
		if (retval == (char **)NULL) {
			pbs_errno = PBSE_SYSTEM;
			PBSD_FreeReply(reply);
			return (char **)NULL;
		}
		sr = reply->brp_un.brp_select;
		sp = (char *)retval + (njobs + 1) * sizeof(char *);
		for (i=0; i<njobs; i++) {
			retval[i] = sp;
			strcpy(sp, sr->brp_jobid);
			sp += strlen(sp) + 1;
			sr = sr->brp_next;
		}
		retval[i] = (char *)NULL;
	}

	PBSD_FreeReply(reply);

	return retval;
}