/*
 *  CVS Version: $Id: clicon_proto_client.c,v 1.17 2013/09/05 20:10:54 olof Exp $
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
 * Client-side functions for clicon_proto protocol
 * Not actually a part of the clicon_proto lib, could be in the front-end lib
 * nectconf makes its own rpc code.
 * Therefore this file should probably be removed or moved to the frontend lib
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clicon_queue.h"
#include "clicon_chunk.h"
#include "clicon_hash.h"
#include "clicon_db.h"
#include "clicon_handle.h"
#include "clicon_spec.h"
#include "clicon_lvalue.h"
#include "clicon_proto.h"
#include "clicon_err.h"
#include "clicon_proto_encode.h"
#include "clicon_proto_client.h"

/*
 * cli_proto_copy
 * Let configure daemon copy a file from one location in a local 
 * filesystem (filename1) to another (filename2)
 */
int
cli_proto_copy(char *spath, char *filename1, char *filename2)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_copy_encode(filename1, filename2,
				     __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;

}

/*
 * See also cli_proto_change_cvec
 */
int
cli_proto_change(char *spath, char *db, lv_op_t op,
		 char *key, char *lvec, int lvec_len)
{
    struct clicon_msg *msg;
    int                retval = -1;

    assert(key && strlen(key));
    if ((msg = clicon_msg_change_encode(db, op, key, lvec, lvec_len,
				       __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * Commit changes
 * Send a commit request to the config_daemon
 * Error handling like a clicon_lib function.
 * If OK returns, copy current->candidate
 * If snapshot set, also make a snpshot
 * If startup_config set, also save as startup_config
 */
int
cli_proto_commit(char *spath, char *running_db, char *db, int snapshot, int startup)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_commit_encode(db, running_db, snapshot, startup, 
				     __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * Validate contents of a database
 * Send a validation request to the config_daemon to be passed on to backend.
 * Error handling like a clicon_lib function.
 */
int
cli_proto_validate(char *spath, char *db)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_validate_encode(db, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_save
 * Send a save_current request to the config_daemon
 * Either save snapshot or to a file.
 * Error handling like a clicon_lib function.
 */
int
cli_proto_save(char *spath, char *dbname, int snapshot, char *filename)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_save_encode(dbname, snapshot, filename,
				   __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_load
 * Send a load file request to the config_daemon
 */
int
cli_proto_load(char *spath, int replace, char *db, char *filename)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_load_encode(replace, db, filename,
				   __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_initdb
 * Let configure daemon initialize database
 */
int
cli_proto_initdb(char *spath, char *filename)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_initdb_encode(filename, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}


/*
 * cli_proto_rm
 * Let configure daemon remove a file 
 */
int
cli_proto_rm(char *spath, char *filename)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_rm_encode(filename, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_lock
 * Lock a database
 */
int
cli_proto_lock(char *spath, char *db)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_lock_encode(db, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_unlock
 * Unlock a database
 */
int
cli_proto_unlock(char *spath, char *db)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_unlock_encode(db, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}


/*
 * cli_proto_kill
 * Kill another session
 */
int
cli_proto_kill(char *spath, int session_id)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_kill_encode(session_id, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }

    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_debug
 * Set debug level on config daemon
 */
int
cli_proto_debug(char *spath, int level)
{
    struct clicon_msg *msg;
    int                retval = -1;

    if ((msg=clicon_msg_debug_encode(level, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc_connect(msg, spath, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}

/*
 * cli_proto_subscription
 * create a new notification subscription
 * return socket
 */
int
cli_proto_subscription(char *sockpath, char *stream, int *s0)
{
    struct clicon_msg *msg;
    int                retval = -1;
    int                s = -1;
    struct stat        sb;

    /* special error handling to get understandable messages (otherwise ENOENT) */
    if (stat(sockpath, &sb) < 0){
	clicon_err(OE_PROTO, errno, "%s: config daemon not running?", sockpath);
	goto done;
    }
    if (!S_ISSOCK(sb.st_mode)){
	clicon_err(OE_PROTO, EIO, "%s: Not unix socket", sockpath);
	goto done;
    }
    if ((s = clicon_connect_unix(sockpath)) < 0)
	goto done;
    if ((msg=clicon_msg_subscription_encode(stream, __FUNCTION__)) == NULL)
	return -1;
    if (clicon_rpc(s, msg, NULL, 0, __FUNCTION__) < 0){
#if 0
	if (errno == ESHUTDOWN)
	    /* Maybe could reconnect on a higher layer, but lets fail
	       loud and proud */
	    cli_set_exiting(1);
#endif
	goto done;
    }
    *s0 = s;
    retval = 0;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
}
