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
#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <pbs_ifl.h>
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"


/**
 * @file	attr_fn_resc.c
 * @brief
 * 	This file contains functions for manipulating attributes of type
 *	resource
 *
 *  A "resource" is similiar to an attribute but with two levels of
 *  names.  The first name is the attribute name, e.g. "resource-list",
 *  the second name is the resource name, e.g. "mem".
 * @details
 * Each resource_def has functions for:
 *	Decoding the value string to the internal representation.
 *	Encoding the internal attribute to external form
 *	Setting the value by =, + or - operators.
 *	Comparing a (decoded) value with the attribute value.
 *	freeing the resource value space (if extra memory is allocated)
 *
 * Some or all of the functions for an resource type may be shared with
 * other resource types or even attributes.
 *
 * The prototypes are declared in "attribute.h", also see resource.h
 *
 * ----------------------------------------------------------------------------
 * Attribute functions for attributes with value type resource
 * ----------------------------------------------------------------------------
 */

/* Global Variables */

int resc_access_perm;

/* External Global Items */

int comp_resc_gt;	/* count of resources compared > */
int comp_resc_eq;	/* count of resources compared = */
int comp_resc_lt;	/* count of resources compared < */
int comp_resc_nc;	/* count of resources not compared  */


/**
 * @brief
 * 	decode_resc - decode a "attribute name/resource name/value" triplet into
 *	         a resource type attribute
 *
 * @param[in] patr - ptr to attribute to decode
 * @param[in] name - attribute name
 * @param[in] rescn - resource name or null
 * @param[out] val - string holding values for attribute structure
 *
 * @retval      int
 * @retval      0       if ok
 * @retval      >0      error number1 if error,
 * @retval      *patr   members set
 *
 */

int
decode_resc(struct attribute *patr, char *name, char *rescn, char *val)
{
	resource	*prsc;
	resource_def	*prdef;
	int		 rc = 0;
	int		 rv;

	if (patr == (attribute *)0)
		return (PBSE_INTERNAL);
	if (rescn == (char *)0)
		return (PBSE_UNKRESC);
	if (!(patr->at_flags & ATR_VFLAG_SET))
		CLEAR_HEAD(patr->at_val.at_list);


	prdef = find_resc_def(svr_resc_def, rescn, svr_resc_size);
	if (prdef == (resource_def *)0) {
		/*
		 * didn't find resource with matching name, use unknown;
		 * but return PBSE_UNKRESC incase caller dosn`t wish to
		 * accept unknown resources
		 */
		rc = PBSE_UNKRESC;
		prdef = find_resc_def(svr_resc_def, RESOURCE_UNKNOWN, svr_resc_size);
	}

	prsc = find_resc_entry(patr, prdef);
	if (prsc == (resource *)0) 	/* no current resource entry, add it */
		if ((prsc = add_resource_entry(patr, prdef)) == (resource *)0) {
			return (PBSE_SYSTEM);
		}

	/* note special use of ATR_DFLAG_ACCESS, see server/attr_recov() */

	if (((prsc->rs_defin->rs_flags&resc_access_perm&ATR_DFLAG_WRACC) ==0) &&
		((resc_access_perm & ATR_DFLAG_ACCESS) != ATR_DFLAG_ACCESS))
		return (PBSE_ATTRRO);

	patr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;

	if ((resc_access_perm & ATR_PERM_ALLOW_INDIRECT) && (*val == '@')) {
		if (strcmp(rescn, "ncpus") != 0)
			rv = decode_str(&prsc->rs_value, name, rescn, val);
		else
			rv = PBSE_BADNDATVAL;
		if (rv == 0)
			prsc->rs_value.at_flags |= ATR_VFLAG_INDIRECT;
	} else {
		rv = prdef->rs_decode(&prsc->rs_value, name, rescn, val);
	}
	if (rv)
		return (rv);
	else
		return (rc);
}

/**
 * @brief
 * 	Encode attr of type ATR_TYPE_RESR into attr_extern form
 *
 * Here we are a little different from the typical attribute.  Most have a
 * single value to be encoded.  But resource attribute may have a whole bunch.
 * First get the name of the parent attribute (typically "resource-list").
 * Then for each resource in the list, call the individual resource encode
 * routine with "aname" set to the parent attribute name.
 *
 * @param[in] attr -  ptr to attribute to encode
 * @param[in] phead - head of attrlist list
 * @param[in] atname - attribute name
 * @param[in] rsname - resource name, null on call
 * @param[in] mode - encode mode
 * @param[out] rtnl - ptr to svrattrl
 *
 * If mode is either ATR_ENCODE_SAVE or ATR_ENCODE_SVR, then any resource
 * currently set to the default value is not encoded.   This allows it to be
 * reset if the default changes or it is moved.
 *
 * If the mode is ATR_ENCODE_CLIENT or ATR_ENCODE_MOM, the client permission
 * passed in the global variable resc_access_perm is checked against each
 * definition.  This allows a resource by resource access setting, not just
 * on the attribute.
 *
 * If the mode is ATR_ENCODE_HOOK, resource permission checking is bypassed.
 *
 * @return - Error code
 * @retval  =0 if no value to encode, no entries added to list
 * @retval  <0 if some resource entry had an encode error.
 *
 */
int
encode_resc(attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)
{
	int	    dflt;
	resource   *prsc;
	int	    rc;
	int	    grandtotal = 0;
	int	    perm;
	int	    first = 1;
	svrattrl   *xrtnl;
	svrattrl   *xprior;

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0);	/* no resources at all */

	/* ok now do each separate resource */

	prsc = (resource *)GET_NEXT(attr->at_val.at_list);
	while (prsc != (resource *)0) {

		/*
		 * encode if sending to client or MOM with permission
		 * encode if saving and ( not default value or save on deflt set)
		 * encode if sending to server and not default and have permission
		 */

		perm = prsc->rs_defin->rs_flags & resc_access_perm ;
		dflt = prsc->rs_value.at_flags & ATR_VFLAG_DEFLT;
		if (((mode == ATR_ENCODE_CLIENT) && perm)  ||

			(mode == ATR_ENCODE_HOOK)     ||

			(mode == ATR_ENCODE_DB)  ||

			((mode == ATR_ENCODE_MOM) && perm)     ||

			(mode == ATR_ENCODE_SAVE) ||

			((mode == ATR_ENCODE_SVR)  && (dflt == 0) && perm)) {

			rsname = prsc->rs_defin->rs_name;
			if (prsc->rs_value.at_flags & ATR_VFLAG_INDIRECT)
				rc = encode_str(&prsc->rs_value, phead,
					atname, rsname, mode, &xrtnl);
			else
				rc = prsc->rs_defin->rs_encode(&prsc->rs_value, phead,
					atname, rsname, mode, &xrtnl);

			if (rc > 0) {
				if (first) {
					if (rtnl)
						*rtnl  = xrtnl;
					first  = 0;
				} else {
					xprior->al_sister = xrtnl;
				}
				xprior = xrtnl;

			} else if (rc < 0) {
				return (rc);
			}
			grandtotal += rc;
		}
		prsc = (resource *)GET_NEXT(prsc->rs_link);
	}
	return (grandtotal);
}



/**
 * @brief
 * 	set_resc - set value of attribute of type ATR_TYPE_RESR to another
 *
 *	For each resource in the list headed by the "new" attribute,
 *	the correspondingly name resource in the list headed by "old"
 *	is modified.
 *
 *	The mapping of the operations incr and decr depend on the type
 *	of each individual resource.
 *
 * @param[in]   old - pointer to old attribute to be set (A)
 * @param[in]   new  - pointer to attribute (B)
 * @param[in]   op   - operator
 *
 * @return      int
 * @retval      0       if ok
 * @retval     >0       if error
 *
 */

int
set_resc(struct attribute *old, struct attribute *new, enum batch_op op)
{
	enum batch_op local_op;
	resource *newresc;
	resource *oldresc;
	int	  rc;

	assert(old && new);

	newresc = (resource *)GET_NEXT(new->at_val.at_list);
	while (newresc != (resource *)0) {

		local_op = op;

		/* search for old that has same definition as new */

		oldresc = find_resc_entry(old, newresc->rs_defin);
		if (oldresc == (resource *)0) {
			/* add new resource to list */
			oldresc = add_resource_entry(old, newresc->rs_defin);
			if (oldresc == (resource *)0) {
				log_err(-1, "set_resc", "Unable to malloc space");
				return (PBSE_SYSTEM);
			}
		}

		/*
		 * unlike other attributes, resources can be "unset"
		 * if new is "set" to a value, the old one is set to that
		 * value; if the new resource is unset (no value), then the
		 * old resource is unset by freeing it.
		 */

		if (newresc->rs_value.at_flags & ATR_VFLAG_SET) {

			/*
			 * An indirect resource is a string of the form
			 * "@<node>", it may be of a different type than the
			 * resource definition itself. free_str() must be called
			 * explicitly to clear away indirectness before the
			 * value can be set again.
			 */
			if (oldresc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {
				free_str(&oldresc->rs_value);
			}
			if (newresc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {
				oldresc->rs_defin->rs_free(&oldresc->rs_value);
				rc = set_str(&oldresc->rs_value,
					&newresc->rs_value, local_op);
				oldresc->rs_value.at_flags |= ATR_VFLAG_INDIRECT;
			} else {
				rc = oldresc->rs_defin->rs_set(&oldresc->rs_value,
					&newresc->rs_value, local_op);
				oldresc->rs_value.at_flags &= ~ATR_VFLAG_INDIRECT;
			}
			if (rc != 0)
				return (rc);
			oldresc->rs_value.at_flags |=
				(newresc->rs_value.at_flags & ATR_VFLAG_DEFLT);
		} else {
			oldresc->rs_defin->rs_free(&oldresc->rs_value);
		}

		newresc = (resource *)GET_NEXT(newresc->rs_link);
	}
	old->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	return (0);
}

/**
 * @brief
 * 	comp_resc - compare two attributes of type ATR_TYPE_RESR
 *
 *	DANGER Will Robinson, DANGER
 *
 *	As you can see from the returns, this is different from the
 *	at_comp model...  PLEASE read the Internal Design Spec
 *
 * @param[in] attr - pointer to attribute structure
 * @param[in] with - pointer to attribute structure
 *
 * @return      int
 * @retval      0       if the set of strings in "with" is a subset of "attr"
 * @retval      -1       otherwise
 *
 */

int
comp_resc(struct attribute *attr, struct attribute *with)
{
	resource *atresc;
	resource *wiresc;
	int rc;

	comp_resc_gt = 0;
	comp_resc_eq = 0;
	comp_resc_lt = 0;
	comp_resc_nc = 0;

	if ((attr == (attribute *)0) || (with == (attribute *)0))
		return (-1);

	wiresc = (resource *)GET_NEXT(with->at_val.at_list);
	while (wiresc != (resource *)0) {
		if (wiresc->rs_value.at_flags & ATR_VFLAG_SET) {
			atresc = find_resc_entry(attr, wiresc->rs_defin);
			if (atresc != (resource *)0) {
				if (atresc->rs_value.at_flags & ATR_VFLAG_SET) {
					if ((rc=atresc->rs_defin->rs_comp(&atresc->rs_value, 				      &wiresc->rs_value)) > 0)
						comp_resc_gt++;
					else if (rc < 0)
						comp_resc_lt++;
					else
						comp_resc_eq++;
				}
			} else {
				comp_resc_nc++;
			}
		}
		wiresc = (resource *)GET_NEXT(wiresc->rs_link);
	}
	return (0);
}

/**
 * @brief
 * 	free_resc - free space associated with attribute value
 *
 *	For each entry in the resource list, the entry is delinked,
 *	the resource entry value space freed (by calling the resource
 *	free routine), and then the resource structure is freed.
 *
 * @param[in] pattr - pointer to attribute structure
 *
 * @return	Void
 *
 */

void
free_resc(attribute *pattr)
{
	resource *next;
	resource *pr;

	pr = (resource *)GET_NEXT(pattr->at_val.at_list);
	while (pr != (resource *)0) {
		next = (resource *)GET_NEXT(pr->rs_link);
		delete_link(&pr->rs_link);
		if (pr->rs_value.at_flags & ATR_VFLAG_INDIRECT)
			free_str(&pr->rs_value);
		else
			pr->rs_defin->rs_free(&pr->rs_value);
		(void)free(pr);
		pr = next;
	}
	free_null(pattr);
	CLEAR_HEAD(pattr->at_val.at_list);
}

/**
 * @brief
 * 	find_resc_def - find the resource_def structure for a resource with
 *	a given name
 *
 * @param[in] rscdf - address of array of resource_def structs 
 * @param[in] name - name of resource
 * @param[in] limit - number of members in resource_def array
 * 
 * @return	pointer to structure
 * @retval	pointer to resource_def structure	Success
 * @retval	NULL					Error
 *
 */

resource_def *
find_resc_def(resource_def *rscdf, char *name, int limit)
{
	if (rscdf == NULL || name == NULL)
		return ((resource_def *)0);

	while (limit--) {
		if (strcasecmp(rscdf->rs_name, name) == 0)
			return (rscdf);
		rscdf = rscdf->rs_next;
	}
	return ((resource_def *)0);
}

/**
 * @brief 
 *	Determines if a resource is a PBS built-in or user custom
 *
 * @param[in] rscdf - address of array of resource_def structs
 *
 * @return	int
 * @retval 	1 	if built-in
 * @retval 	0 	if custom
 * @retval 	-1 	on error
 *
 */
int
is_builtin(resource_def *rscdef)
{
	int i;
	resource_def *svr_rsdef;
	int builtin_resc = 1;

	if (rscdef == NULL) {
		return -1;

	}
	for (i=0, svr_rsdef=svr_resc_def; i < svr_resc_size; i++) {
		/* the |unknown| resource is the delimiter between builtins
		 * and custom
		 */
		if (strcmp(svr_rsdef->rs_name, RESOURCE_UNKNOWN) == 0) {
			builtin_resc = 0;
		}
		else if (strcasecmp(svr_rsdef->rs_name, rscdef->rs_name) == 0) {
			if (builtin_resc) {
				return 1;
			}
			return 0;
		}
		svr_rsdef = svr_rsdef->rs_next;
	}
	return -1;
}

/**
 * @brief
 * 	find_resc_entry - find a resource (value) entry in a list headed in an
 * 	an attribute that points to the specified resource_def structure
 *
 * @param[in] pattr - pointer to attribute structure
 * @param[in] rscdf - pointer to resource_def structure
 *
 * @return	structure handler
 * @retval	pointer to struct resource 	Success
 * @retval	NULL				Error
 *
 */

resource *
find_resc_entry(attribute *pattr, resource_def *rscdf)
{
	resource *pr;

	pr = (resource *)GET_NEXT(pattr->at_val.at_list);
	while (pr != (resource *)0) {
		if (pr->rs_defin == rscdf)
			break;
		pr = (resource *)GET_NEXT(pr->rs_link);
	}
	return (pr);
}

/**
 * @brief
 * 	add_resource_entry - add and "unset" entry for a resource type to a
 *	list headed in an attribute.  Just for later displaying, the
 *	resource list is maintained in an alphabetic order.
 *	The parent attribute is marked with ATR_VFLAG_SET and ATR_VFLAG_MODIFY
 *
 * @param[in] pattr - pointer to attribute structure
 * @param[in] prdef -  pointer to resource_def structure
 *
 * @return	structure handler
 * @retval      pointer to struct resource      Success
 * @retval      NULL                            Error
 *
 */

resource *
add_resource_entry(attribute *pattr, resource_def *prdef)
{
	int 		 i;
	resource	*new;
	resource	*pr;

	pr = (resource *)GET_NEXT(pattr->at_val.at_list);
	while (pr != (resource *)0) {
		i = strcasecmp(pr->rs_defin->rs_name, prdef->rs_name);
		if (i == 0)	/* found a matching entry */
			return (pr);
		else if (i > 0)
			break;
		pr = (resource *)GET_NEXT(pr->rs_link);
	}
	new = (resource *)malloc(sizeof(resource));
	if (new == (resource *)0) {
		log_err(-1, "add_resource_entry", "unable to malloc space");
		return ((resource *)0);
	}
	CLEAR_LINK(new->rs_link);
	new->rs_defin = prdef;
	new->rs_value.at_type = prdef->rs_type;
	new->rs_value.at_flags = 0;
	new->rs_value.at_user_encoded = 0;
	new->rs_value.at_priv_encoded = 0;
	prdef->rs_free(&new->rs_value);

	if (pr != (resource *)0) {
		insert_link(&pr->rs_link, &new->rs_link, new, LINK_INSET_BEFORE);
	} else {
		append_link(&pattr->at_val.at_list, &new->rs_link, new);
	}
	pattr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	return (new);
}

/**
 * @brief
 *      This function is called by action routine of resource_list attribute
 *	of job and reservation. For each resource in the list, if it has its
 *	own action routine,it calls it.
 *
 * @see
 *	action_resc_job
 *	action_resc_resv
 *
 * @param[in]   pattr   -     pointer to new attribute value
 * @param[in]   pobject -     pointer to object
 * @param[in]   type    -     object is job or reservation
 * @param[in]   actmode -     action mode
 *
 * @return      int
 * @retval       PBSE_NONE : success
 * @retval       Error code returned by resource action routine
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */

int
action_resc(attribute *pattr, void *pobject, int type, int actmode)
{
	resource *pr;
	int rc;

	pr = (resource *)GET_NEXT(pattr->at_val.at_list);
	while (pr) {
		if ((pr->rs_value.at_flags & ATR_VFLAG_MODIFY) &&
			(pr->rs_defin->rs_action)) {
			if ((rc=pr->rs_defin->rs_action(pr, pattr, pobject,
				type, actmode))!=0)
				return (rc);
		}

		pr->rs_value.at_flags &= ~ATR_VFLAG_MODIFY;
		pr = (resource *)GET_NEXT(pr->rs_link);
	}
	return (0);
}

/**
 * @brief
 *      the at_action for the resource_list attribute of a job
 *
 * @see
 *	action_resc
 *
 * @param[in]   pattr    -     pointer to new attribute value
 * @param[in]   pobject  -     pointer to job
 * @param[in]   actmode  -     action mode
 *
 * @return      int
 * @retval       PBSE_NONE : success
 * @retval       Error code returned by resource action routine
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */

int
action_resc_job(attribute *pattr, void *pobject, int actmode)
{
	return (action_resc(pattr, pobject, PARENT_TYPE_JOB, actmode));
}

/**
 * @brief
 *      the at_action for the resource_list attribute of a reservation
 *
 * @see
 *	action_resc
 *
 * @param[in]   pattr    -     pointer to new attribute value
 * @param[in]   pobject  -     pointer to reservation
 * @param[in]   actmode  -     action mode
 *
 * @return      int
 * @retval       PBSE_NONE : success
 * @retval       Error code returned by resource action routine
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */

int
action_resc_resv(attribute *pattr, void *pobject, int actmode)
{
	return (action_resc(pattr, pobject, PARENT_TYPE_RESV, actmode));
}

/**
 * @brief
 *      the at_action for the resource_default attribute of a server
 *
 * @param[in]   pattr    -     pointer to new attribute value
 * @param[in]   pobject  -     pointer to reservation
 * @param[in]   actmode  -     action mode
 *
 * @return      int
 * @retval       PBSE_NONE : success
 * @retval       Error code returned by action_resc_dflt routine
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */
int
action_resc_dflt_svr(attribute *pattr, void *pobj, int actmode)
{
	return (action_resc(pattr, pobj, PARENT_TYPE_SERVER, actmode));
}

/**
 * @brief
 *      the at_action for the resource_default attribute of a queue
 *
 * @param[in]   pattr    -     pointer to new attribute value
 * @param[in]   pobject  -     pointer to reservation
 * @param[in]   actmode  -     action mode
 *
 * @return      int
 * @retval       PBSE_NONE : success
 * @retval       Error code returned by action_resc_dflt routine
 *
 * @par Side Effects: None
 *
 * @par MT-safe: Yes
 *
 */
int
action_resc_dflt_queue(attribute *pattr, void *pobj, int actmode)
{
	return (action_resc(pattr, pobj, PARENT_TYPE_QUE_ALL, actmode));
}