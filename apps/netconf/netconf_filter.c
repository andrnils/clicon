/*
 *  CVS Version: $Id: netconf_filter.c,v 1.9 2013/08/01 09:15:46 olof Exp $
 *
  Copyright (C) 2009-2013 Olof Hagsand and Benny Holmgren

  This file is part of CLICON.

  CLICON is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  CLICON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CLICON; see the file COPYING.  If not, see
  <http://www.gnu.org/licenses/>.

 *
 *  netconf match & selection: get and edit operations
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <assert.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "netconf_rpc.h"
#include "netconf_lib.h"
#include "netconf_filter.h"

/*
 * xml_filter
 * xf specifices a filter, and xn is an xml tree.
 * Select the part of xn that matches xf and return it.
 * Change xn destructively by removing the parts of the sub-tree that does 
 * not match.
 * Match according to Section 6 of RFC 4741.
    NO_FILTER,       select all 
    EMPTY_FILTER,    select nothing 
    ATTRIBUTE_MATCH, select if attribute match 
    SELECTION,       select this node 
    CONTENT_MATCH,   select all siblings with matching content 
    CONTAINMENT      select 
 */

/* return a string containing leafs value, NULL if no leaf or no value */
static char*
leafstring(struct xml_node *x)
{
    struct xml_node *c;

    if (x->xn_type != XML_ELEMENT)
	return NULL;
    if (x->xn_nrchildren != 1)
	return NULL;
    c = x->xn_children[0];
    if (c->xn_nrchildren != 0)
	return NULL;
    if (c->xn_type != XML_BODY)
	return NULL;
    return c->xn_value;
}

/*
 * Select siblings of xp
 * assume parent has been selected and filter match (same name) as parent
 * Return values: 0 OK, -1 error
 * parent is pruned according to selection.
 */
static int
select_siblings(struct xml_node *filter, struct xml_node *parent, int *remove_me)
{
    struct xml_node *s;
    struct xml_node *sprev;
    struct xml_node *f;
    struct xml_node *attr;
    char *an, *af;
    char *fstr, *sstr;
    int containments;

    *remove_me = 0;
    assert(filter && parent && strcmp(filter->xn_name, parent->xn_name)==0);
    /* 1. Check selection */
    if (filter->xn_nrchildren == 0) 
	goto match;

    /* Count containment/selection nodes in filter */
    f = NULL;
    containments = 0;
    while ((f = xml_child_each(filter, f, XML_ELEMENT)) != NULL) {
	if (leafstring(f))
	    continue;
	containments++;
    }

    /* 2. Check attribute match */
    attr = NULL;
    while ((attr = xml_child_each(filter, attr, XML_ATTRIBUTE)) != NULL) {
	af = attr->xn_value;
	an = xml_get(filter, attr->xn_name);
	if (af && an && strcmp(af, an)==0)
	    ; // match
	else
	    goto nomatch;
    }
    /* 3. Check content match */
    f = NULL;
    while ((f = xml_child_each(filter, f, XML_ELEMENT)) != NULL) {
	if ((fstr = leafstring(f)) == NULL)
	    continue;
	if ((s = xml_find(parent, f->xn_name)) == NULL)
	    goto nomatch;
	if ((sstr = leafstring(s)) == NULL)
	    continue;
	if (strcmp(fstr, sstr))
	    goto nomatch;
    }
    /* If filter has no further specifiers, accept */
    if (!containments)
	goto match;
    /* Check recursively the rest of the siblings */
    sprev = s = NULL;
    while ((s = xml_child_each(parent, s, XML_ELEMENT)) != NULL) {
	if ((f = xml_find(filter, s->xn_name)) == NULL){
	    xml_prune(parent, s, 1);
	    s = sprev;
	    continue;
	}
	if (leafstring(f)){
	    sprev = s;
	    continue; // unsure?sk=lf
	}
	{ 	// XXX: s can be removed itself in the recursive call !
	    int remove_s = 0;
	    if (select_siblings(f, s, &remove_s) < 0)
		return -1;
	    if (remove_s){
		xml_prune(parent, s, 1);
		s = sprev;
	    }
	}
	sprev = s;
    }

  match:
    return 0;
  nomatch: /* prune this parent node (maybe only children?) */
    *remove_me = 1;
    return 0;
}

int
xml_filter(struct xml_node *xf, struct xml_node *xn)
{
    int retval;
    int remove_s;

    retval = select_siblings(xf, xn, &remove_s);
    return retval;
}

/*
 * get_operation
 * get the value of the "operation" attribute and change op if given
 */
static int
get_operation(struct xml_node *xn, enum operation_type *op,
	      xf_t *xf_err, struct xml_node *xt)
{
    char *opstr;

    if ((opstr = xml_get(xn, "operation")) != NULL){
	if (strcmp("merge", opstr) == 0)
	    *op = OP_MERGE;
	else
	if (strcmp("replace", opstr) == 0)
	    *op = OP_REPLACE;
	else
	if (strcmp("create", opstr) == 0)
	    *op = OP_CREATE;
	else
	if (strcmp("delete", opstr) == 0)
	    *op = OP_DELETE;
	else
	if (strcmp("remove", opstr) == 0)
	    *op = OP_REMOVE;
	else{
	    netconf_create_rpc_error(xf_err, xt, 
				     "bad-attribute", 
				     "protocol", 
				     "error", 
				     NULL,
				     "<bad-attribute>operation</bad-attribute>");
	    return -1;
	}
    }
    return 0;
}


/*
 * in edit_config, we copy a tree to the config. But some wthings shouldbe 
 * cleaned:
 * - operation attribute
 */
static int
netconf_clean(struct xml_node *xn)
{
    struct xml_node *xa;    
    struct xml_node *x;

    if ((xa = xml_find(xn, "operation")) != NULL &&
	xa->xn_type == XML_ATTRIBUTE)
	xml_prune(xn, xa, 1);
    x = NULL;
    while ((x = xml_child_each(xn, x, XML_ELEMENT)) != NULL) 
	netconf_clean(x);
    return 0;
}

/*
 * xml_edit
 * Merge two XML trees according to RFC4741/Junos
 * 1. in configuration(parent) but not in new(filter) -> remain in configuration
 * 2. not in configuration but in new -> add to configuration
 * 3. Both in configuration and new: Do 1.2.3 with children.
 * A key is: the configuration data identified by the element
 */
static int
edit_selection(struct xml_node *filter, 
	       struct xml_node *parent, 
	       enum operation_type op,
	       xf_t *xf_err, 
	       struct xml_node *xt)
{
    int retval = -1;

    assert(filter && parent && strcmp(filter->xn_name, parent->xn_name)==0);
    fprintf(stderr, "%s: %s\n", __FUNCTION__, filter->xn_name);
    switch (op){
    case OP_MERGE:
	break;
    case OP_REPLACE:
	xml_prune(parent->xn_parent, parent, 1);
	break;
    case OP_CREATE:
	    netconf_create_rpc_error(xf_err, xt, 
				     NULL,
				     "protocol", 
				     "error", 
				     "statement creation failed",
				     "<bad-element></bad-element>");
	    goto done;
	break;
    case OP_DELETE:
    fprintf(stderr, "%s: %s DELETE\n", __FUNCTION__, filter->xn_name);
	if (parent->xn_nrchildren == 0){
	    fprintf(stderr, "%s: %s ERROR\n", __FUNCTION__, filter->xn_name);
	    netconf_create_rpc_error(xf_err, xt, 
				     NULL,
				     "protocol", 
				     "error", 
				     "statement not found",
				     "<bad-element></bad-element>");
	    goto done;
	}
	/* fall through */
    case OP_REMOVE:
	fprintf(stderr, "%s: %s REMOVE\n", __FUNCTION__, filter->xn_name);
	xml_prune(parent->xn_parent, parent, 1);
	break;
    case OP_NONE:
	break;
    }
    retval = 0;
    netconf_ok_set(1); /* maybe cc_ok shouldnt be set if we continue? */
  done:
    return retval;
}

static int
edit_match(struct xml_node *filter, 
	   struct xml_node *parent, 
	   enum operation_type op,
	   xf_t *xf_err, 
	   struct xml_node *xt,
	   int match
    )
{
    struct xml_node *f;
    struct xml_node *s;
    struct xml_node *copy;
    int retval = -1;

    if (debug)
	fprintf(stderr, "%s: %s op:%d\n", __FUNCTION__, filter->xn_name, op);
    if (match)
	switch (op){
	case OP_REPLACE:
	case OP_CREATE:
	    if (debug)
		fprintf(stderr, "%s: %s REPLACE\n", __FUNCTION__, filter->xn_name);
	    s = NULL;
	    while ((s = xml_child_each(parent, s, -1)) != NULL){ 
		xml_prune(parent, s, 1);
		s = NULL;
	    }
	    if (xml_cp(filter, parent) < 0)
		goto done;
	    netconf_clean(parent);
	    retval = 0;
	    netconf_ok_set(1);
	    goto done;
	    break;
	case OP_DELETE:
	case OP_REMOVE:
	    xml_prune(parent->xn_parent, parent, 1);
	    netconf_ok_set(1);
	    goto done;
	    break;
	case OP_MERGE: 
	case OP_NONE:
	    break;
	}

    f = NULL;
    while ((f = xml_child_each(filter, f, XML_ELEMENT)) != NULL) {
	s = xml_find(parent, f->xn_name);
	switch (op){
	case OP_MERGE: 
	    if (debug)
		fprintf(stderr, "%s: merge: %s\n", __FUNCTION__, f->xn_name);
	    /* things in filter:
	       not in conf should be added 
	       in conf go down recursive
	    */
	    if (s == NULL && match){
		if ((copy = xml_new(f->xn_name, parent)) == NULL)
		    goto done;
		if (xml_cp(f, copy) < 0)
		    goto done;
		netconf_clean(copy);
	    }
	    else{
		if (debug)
		    fprintf(stderr, "%s: merge: %s descent\n", __FUNCTION__, f->xn_name);
		s = NULL;
		while ((s = xml_child_each(parent, s, XML_ELEMENT)) != NULL) {
		    if (strcmp(f->xn_name, s->xn_name))
			continue;
		    if ((retval = xml_edit(f, s, op, xf_err, xt)) < 0)
			goto done;
		}
		if (debug)
		    fprintf(stderr, "%s: merge: %s descent done\n", __FUNCTION__, f->xn_name);
	    }
	    break;
	case OP_REPLACE:
#if 1
	    /* things in filter
	       in conf: remove from conf and
	       add to conf
	    */
//	    if (!match)
//		break;
	    if (s != NULL)
		xml_prune(parent, s, 1);
	    if ((copy = xml_new(f->xn_name, parent)) == NULL)
		goto done;
	    if (xml_cp(f, copy) < 0)
		goto done;
	    netconf_clean(copy);
#endif
	    break;
	case OP_CREATE:
#if 0
	    /* things in filter
	       in conf: error
	       else add to conf
	    */
	    if (!match)
		break;
	    if (s != NULL){
		netconf_create_rpc_error(xf_err, xt, 
					 NULL,
					 "protocol", 
					 "error", 
					 "statement creation failed",
					 "<bad-element></bad-element>");
		goto done;
	    }
	    if ((copy = xml_new(f->xn_name, parent)) == NULL)
		goto done;
	    if (xml_cp(f, copy) < 0)
		goto done;
	    netconf_clean(copy);
#endif
	    break;
	case OP_DELETE:
	    /* things in filter
	       if not in conf: error
	       else remove from conf
	    */
#if 0
	    if (!match)
		break;
	    if (s == NULL){
		netconf_create_rpc_error(xf_err, xt, 
					 NULL,
					 "protocol", 
					 "error", 
					 "statement not found",
					 "<bad-element></bad-element>");
		goto done;
	    }
#endif
	    /* fall through */
	case OP_REMOVE:
	    /* things in filter
	       remove from conf
	    */
#if 0
	    if (!match)
		break;
	    xml_prune(parent, s, 1);
#endif
	    break;
	case OP_NONE:
	    /* recursive descent */
	    s = NULL;
	    while ((s = xml_child_each(parent, s, XML_ELEMENT)) != NULL) {
		if (strcmp(f->xn_name, s->xn_name))
		    continue;
		if ((retval = xml_edit(f, s, op, xf_err, xt)) < 0)
		    goto done;
	    }
	    break;
	}
    } /* while f */
    retval = 0;
    netconf_ok_set(1); /* maybe cc_ok shouldnt be set if we continue? */
  done:
    return retval;
    
}

/*
 * xml_edit
 * merge filter into parent
 */
int
xml_edit(struct xml_node *filter, 
	 struct xml_node *parent, 
	 enum operation_type op,
	 xf_t *xf_err, 
	 struct xml_node *xt)
{
    struct xml_node *attr;
    struct xml_node *f;
    struct xml_node *s;
    int retval = -1;
    char *an, *af;
    char *fstr, *sstr;
    int keymatch = 0;

    if (debug)
	fprintf(stderr, "%s: %s\n", __FUNCTION__, filter->xn_name);
    get_operation(filter, &op, xf_err, xt);
    /* 1. First try selection: filter is empty */
    if (filter->xn_nrchildren == 0){  /* no elements? */
	retval = edit_selection(filter, parent, op, xf_err, xt);
	goto done;
    }
    if (filter->xn_nrchildren == 1 && /* same as above */
	xml_xpath(filter, "/[@operation]")){
	retval = edit_selection(filter, parent, op, xf_err, xt);
	goto done;
    }
    /* 2. Check attribute match */
    attr = NULL;
    while ((attr = xml_child_each(filter, attr, XML_ATTRIBUTE)) != NULL) {
	af = attr->xn_value;
	an = xml_get(filter, attr->xn_name);
	if (af && an && strcmp(af, an)==0)
	    ; // match
	else
	    goto nomatch;
    }
    /* 3. Check content match */
    /*
     * For content-match we do a somewhat strange thing, we find
     * a match in first content-node and assume that is unique
     * and then we remove/replace that
     * For merge we just continue
     */
    f = NULL;
    while ((f = xml_child_each(filter, f, XML_ELEMENT)) != NULL) {
	if ((fstr = leafstring(f)) == NULL)
	    continue;
	if (debug)
	    fprintf(stderr, "%s: filter %s leafstring: %s=%s\n", __FUNCTION__, 
		    filter->xn_name,
		    f->xn_name, fstr);
	/* we found a filter leaf-match: no return we say it should match*/
	if ((s = xml_find(parent, f->xn_name)) == NULL)
	    goto nomatch;
	if ((sstr = leafstring(s)) == NULL)
	    goto nomatch;
	if (debug){
	    fprintf(stderr, "%s: parent leafstring: %s=%s\n", __FUNCTION__, 
		    s->xn_name, sstr);
	    fprintf(stderr, "%s: leafstring sibling: %s=%s\n", __FUNCTION__, s->xn_name, sstr);
	}
	if (strcmp(fstr, sstr))
	    goto nomatch;
	if (debug)
	    fprintf(stderr, "%s: leaftsring match\n", __FUNCTION__);
	keymatch++;
	break; /* match */
    }

    if (debug && keymatch){
	fprintf(stderr, "%s: match\n", __FUNCTION__);
	fprintf(stderr, "%s: filter:\n", __FUNCTION__);
	xml_to_file(stdout, filter, 0, 1);
	fprintf(stderr, "%s: config:\n", __FUNCTION__);
	xml_to_file(stdout, parent, 0, 1);
    }

    retval = edit_match(filter, parent, op, xf_err, xt, keymatch);
    /* match */
    netconf_ok_set(1);
    retval = 0;
  done:
    return retval;
  nomatch:
    return 0;
}

/*
 * netconf_xpath
 * Arguments:
 *  xsearch is where you search for xpath, grouped by a single top node which is
 *          not significant and will not be returned in any result. 
 *  xfilter is the xml sub-tree, eg: 
 *             <filter type="xpath" select="/t:top/t:users/t:user[t:name='fred']"/>
 *  xt is original tree
 *  
 */
int 
netconf_xpath(struct xml_node *xsearch,
	      struct xml_node *xfilter, 
	      xf_t *xf, 
	      xf_t *xf_err, 
	      struct xml_node *xt)
{
    struct xml_node  *x;
    int               retval = -1;
    char             *selector;
    struct xml_node **xv;
    int               xlen;
    int               i;

    if ((selector = xml_get(xfilter, "select")) == NULL){
	netconf_create_rpc_error(xf_err, xt, 
				 "operation-failed", 
				 "application", 
				 "error", 
				 NULL,
				 "select");
	goto done;
    }

    x = NULL;

    clicon_errno = 0;
    if ((xv = xpath_vec(xsearch, selector, &xlen)) != NULL) {
	for (i=0; i<xlen; i++){
	    x = xv[i];
	    print_xml_xf_node(xf, x, 0, 1);
	}
	free(xv);
    }
    /* XXX: NULL means error sometimes */
    if (clicon_errno){
	netconf_create_rpc_error(xf_err, xt, 
				 "operation-failed", 
				 "application", 
				 "error", 
				 clicon_err_reason,
				 "select");
	goto done;
    }

    retval = 0;
  done:
    return retval;
}


