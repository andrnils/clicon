/*
 *  CVS Version: $Id: netconf_lib.c,v 1.23 2013/08/09 13:25:42 olof Exp $
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
 *  netconf lib
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

/*
 * Exported variables
 */
enum transport_type    transport = NETCONF_SSH; 
int cc_closed = 0;

static int cc_ok = 0;

void
netconf_ok_set(int ok)
{
    cc_ok = ok;
}

int
netconf_ok_get(void)
{
    return cc_ok;
}

int
add_preamble(xf_t *xf)
{
    if (transport == NETCONF_SOAP)
	xprintf(xf, "\n<soapenv:Envelope\n xmlns:soapenv=\"http://www.w3.org/2003/05/soap-envelope\">\n"
	"<soapenv:Body>");
    return 0;
}

/* 
 * add_postamble
 * add netconf xml postamble of message. That is, xml after the body of the message.
 * for soap this is the envelope stuff, for ssh this is ]]>]]>
 */
int
add_postamble(xf_t *xf)
{
    switch (transport){
    case NETCONF_SSH:
	xprintf(xf, "]]>]]>");     /* Add RFC4742 end-of-message marker */
	break;
    case NETCONF_SOAP:
	xprintf(xf, "\n</soapenv:Body>" "</soapenv:Envelope>");
	break;
    }
    return 0;
}

/* 
 * add_error_preamble
 * compared to regular messages (see add_preamble), error message differ in some
 * protocols (eg soap) by adding a longer and deeper header.
 */
int
add_error_preamble(xf_t *xf, char *reason)
{
    switch (transport){
    case NETCONF_SOAP:
	xprintf(xf, "<soapenv:Envelope xmlns:soapenv=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:xml=\"http://www.w3.org/XML/1998/namespace\">"
		"<soapenv:Body>"
		"<soapenv:Fault>"
		"<soapenv:Code>"
		"<soapenv:Value>env:Receiver</soapenv:Value>"
		"</soapenv:Code>"
		"<soapenv:Reason>"
		"<soapenv:Text xml:lang=\"en\">%s</soapenv:Text>"
		"</soapenv:Reason>"
		"<detail>", reason);
	break;
    default:
	if (add_preamble(xf) < 0)
	    return -1;
	break;
    }
    return 0;
}

/* 
 * add_error_postamble
 * compared to regular messages (see add_postamble), error message differ in some
 * protocols (eg soap) by adding a longer and deeper header.
 */
int
add_error_postamble(xf_t *xf)
{
    switch (transport){
    case NETCONF_SOAP:
	xprintf(xf, "</detail>" "</soapenv:Fault>");
    default: /* fall through */
	if (add_postamble(xf) < 0)
	    return -1;
	break;
    }
    return 0;
}

/*
 * detect_endtag
 * Args:
 *  ch - input character
 *  state - a state integer holding how far we have parsed.
 *  transport - soap or ssh?
 * Return value:
 *  0 - No, we havent detected end tag
 *  1 - Yes, we have detected end tag!
 */
int
detect_endtag(char *tag, char ch, int *state)
{
    int retval = 0;

    if (tag[*state] == ch){
	(*state)++;
	if (*state == strlen(tag)){
	    *state = 0;
	    retval = 1;
	}
    }
    else
	*state = 0;
    return retval;
}

/*
 * target_locked
 * return 1 if locked, 0 if not.
 * return clientid if locked.
 */
int
target_locked(enum target_type target, int *client)
{
    return 0;
}

/*
 * unlock_target
 */
int
unlock_target(enum target_type target)
{
    return 0;
}

int
lock_target(enum target_type target)
{
    return 0;
}

char *
get_target(clicon_handle h, struct xml_node *xn, char *path)
{
    struct xml_node *x;    
    char *target = NULL;

    if ((x = xml_xpath(xn, path)) != NULL){
	if (xml_xpath(x, "/candidate") != NULL)
	    target = clicon_candidate_db(h);
	else
	if (xml_xpath(x, "/running") != NULL)
	    target = clicon_running_db(h);
    }
    return target;
    
}

/*
 * netconf_downcall
 * Call a config function
 * XXX: clone of cli_downcall
 */
int
netconf_downcall(clicon_handle h, uint16_t op, char *plugin, char *func,
		 void *param, uint16_t paramlen, 
		 char **ret, uint16_t *retlen,
		 const void *label
    )
{
    struct clicon_msg *msg;
    char *s;
    int retval = -1;

    if ((msg = clicon_msg_call_encode(op, plugin, func, 
				      paramlen, param, 
				      label)) == NULL)
	goto done;

    if ((s = clicon_sock(h)) == NULL)
	goto done;
    if (clicon_rpc_connect(msg, s, (char**)ret, retlen, label) < 0)
	goto done;
    retval = 0;
done:
    return retval;
}