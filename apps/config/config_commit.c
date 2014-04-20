/*
 *  CVS Version: $Id: config_commit.c,v 1.55 2013/09/18 19:22:12 olof Exp $
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

 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_backend_api.h"
#include "config_lib.h"
#include "config_plugin.h"
#include "config_dbdiff.h"
#include "config_dbdep.h"
#include "config_handle.h"
#include "config_commit.h"

/*
 * plugin_modify_key_value
 * A wrapper function for invoking the plugin dependency set/del call
 * for a changed a key value. 
 * The routine logs on debug.
 * It also checks whether an error was properly registered using clicon_err().
 * Arguments:
 *   db:   The database which contains the key value, most relevant in a set operation
 *         but may possibly in some cases be important in delete operations, although
 *         I cannot think of any,..
 *   key:  The name of the key in the database above.
 *   op:   Either set or delete
 *   dep:  plugin dependency information. Contains function and argument pointers.
 *
 * Returns:
 *  0: OK
 * -1: An error occured in the plugin commit function. It is assumed that 
 *     clicon_err() has been called there. Here, we interpret the clicon_err
 *     as a 'commit' error and does not handle it fatally. 
 */
static int
plugin_modify_key_value(clicon_handle h, 
			char *db, 
			char *key,
			enum trans_cb_type type,
			lv_op_t op,
			dbdep_t *dp)
{
    int retval = -1;

    clicon_debug(2, "commit diff %c%s", (op==LV_SET)?'+':'-', key);
    clicon_err_reset();
    if (dp->dp_callback(h, db, type, op, key, dp->dp_arg) < 0){
	if (!clicon_errno) 	/* sanity: log if clicon_err() is not called ! */
	    clicon_err(OE_DB, 0, "Unknown error: %c%s: plugin does not make clicon_err call on error",
		    (op==LV_SET)?'+':'-', key);
	goto done;
    }
    retval = 0;
  done:
    return retval;
}

/*
 * generic_validate
 *
 * key values are checked for validity independent of user-defined callbacks
 * They are checked as follows:
 * 1. If no value and default value defined, add it.
 * 2. If no value and mandatory flag set in spec, report error.
 * 3. Validate value versus spec, and report error if no match. Currently only int ranges and
 *    string regexp checked.
 */
static int
generic_validate(clicon_handle h, char *dbname, const struct dbdiff *dd)
{
    int             i, j;
    char           *key;
    cvec           *cvec = NULL;
    cg_var         *cv;
    cg_varspec     *cs;
    int             retval = -1;
    cg_obj         *co;
    cg_obj         *cov;
    parse_tree     *dbspec_co;
    char           *reason = NULL;
    parse_tree     *pt;

    if ((dbspec_co = clicon_dbspec_pt(h)) == NULL)
	goto done;
    /* dd->df_ents[].dfe_key1 (running),  
       dd->df_ents[].dfe_key2 (candidate) */
    for (i = 0; i < dd->df_nr; i++) {
	if ((key = dd->df_ents[i].dfe_key2) == NULL)
	    continue;
	if ((co = key2spec_co(dbspec_co, key)) == NULL)
	    continue;
	/* read variable list from db */
	if ((cvec = dbkey2cvec(dbname, key)) == NULL) 
	    goto done;
	/* Loop over all co:s children (spec) and check if actual values
	   in db(cv) satisfies them */
	pt = &co->co_pt;
	for (j=0; j<pt->pt_len; j++){
	    if ((cov = pt->pt_vec[j]) == NULL)
		continue;
	    if (cov->co_type == CO_VARIABLE){

		/* There is no db-value, but dbspec has default value */
		if ((cv = dbspec_default_get(cov)) != NULL &&
		    cvec_find(cvec, cov->co_command) == NULL){
		    cv_flag_set(cv, V_DEFAULT); /* mark it as default XXX not survive DB */
		    /* add default value to cvec */
		    if (cvec_add_cv(cvec, cv) < 0){
			clicon_err(OE_CFG, 0, "cvec_add_cv");
			goto done;
		    }
		    /* Write to database */
		    if (cvec2dbkey(dbname, key, cvec) < 0)
			goto done;
		}
		else
		if (!dbspec_optional_get(cov) && cvec_find(cvec, cov->co_command) == NULL){
		    clicon_err(OE_CFG, 0, 
			       "key %s: Missing mandatory variable: %s\n",
			       key, cov->co_command);
		    goto done;
		}
	    }
	}
	cv = NULL;
	/* Loop over all actual db/cv:s och check their validity */	
	while ((cv = cvec_each(cvec, cv))) {
	    if ((cov = co_find_one(*pt, cv_name_get(cv))) == NULL){
		clicon_err(OE_CFG, 0, "key %s: variable %s not found in co-spec", 
			   key, cv_name_get(cv));
		goto done;
	    }
	    if ((cs = co2varspec(cov)) == NULL)
		continue;
	    if (cv_validate(cv, cs, &reason) != 1){ /* We ignore errors */
		clicon_err(OE_DB, 0, 
			   "key %s: validation of %s failed\n",
			   key, cov->co_command);
		goto done;
	    }

	}
	if (cvec){
	    cvec_free(cvec);
	    cvec = NULL;
	}
    } /* for */
    retval = 0;
  done:
    if (cvec)
	cvec_free(cvec);
    if (reason)
	free(reason);
    return retval;
}

/*
 * validate_db
 * Make user-defined callbacks on each changed keys in order:
 * deleted keys, changed keys, added keys.
 */
static int
validate_db(clicon_handle h, int nvec, dbdep_dd_t *ddvec, char *candidate)
{
    int                retval = -1;
    int                i;
    dbdep_t           *dp;
    struct dbdiff_ent *dfe;

    /* 3a. Validate callbacks: deleted keys */
    for (i = nvec-1; i >= 0; i--){ /* Loop over vec[] */
	dfe = ddvec[i].dd_dbdiff; /* key1/key2/op */
	dp = ddvec[i].dd_dep;     /* op, callback, arg */
	if (dp->dp_type & TRANS_CB_VALIDATE &&
	    (dfe->dfe_op & DBDIFF_OP_FIRST) == DBDIFF_OP_FIRST &&
	    plugin_modify_key_value(h, candidate,      /* db */
				    dfe->dfe_key1,     /* key */
				    TRANS_CB_VALIDATE, /* commit|validate */
				    LV_DELETE,         /* operation */
				    dp                 /* callback(arg) */
		) < 0){
	    goto done;
	}
    }

    /* 3b. Validate callbacks: changed keys */
    for (i=0; i < nvec; i++){
	dfe = ddvec[i].dd_dbdiff; /* key1/key2/op */
	dp = ddvec[i].dd_dep;     /* op, callback, arg */
	if (dp->dp_type & TRANS_CB_VALIDATE &&
	    dfe->dfe_op == DBDIFF_OP_BOTH &&
	    plugin_modify_key_value(h, candidate,      /* db */
				    dfe->dfe_key2,     /* key */
				    TRANS_CB_VALIDATE, /* commit|validate */
				    LV_SET,            /* operation */
				    dp                 /* callback(arg) */
		) < 0){
	    goto done;
	}
    }

    /* 3c. Validate callbacks: added keys */
    for (i=0; i < nvec; i++){
	dfe = ddvec[i].dd_dbdiff; /* key1/key2/op */
	dp = ddvec[i].dd_dep;     /* op, callback, arg */
	if (dp->dp_type & TRANS_CB_VALIDATE &&
	    dfe->dfe_op == DBDIFF_OP_SECOND &&
	    plugin_modify_key_value(h, candidate,      /* db */
				    dfe->dfe_key2,     /* key */
				    TRANS_CB_VALIDATE, /* commit|validate */
				    LV_SET,            /* operation */
				    dp                 /* callback(arg) */
		) < 0){
	    goto done;
	}
    }
    retval = 0;
  done:
    return retval;
}

/*
 * candidate_commit
 * Do a diff between candidate and running, and then call plugins to
 * commit the changes. 
 * The code reverts changes if the commit fails. But if the revert
 * fails, we just ignore the errors and proceed. Maybe we should
 * do something more drastic?
 * Arguments:
 * running:   The current database. The state of the router corresponds
 *            to these values. Also called db1.
 * candidate: The candidate database. We are aiming to put the router in this 
 *            state.   Also called db2.

                      (_dp) [op, callback] (dpe_)
                      +---------------+    +---------------+
                      |    dbdep_t    |--> |  dbdep_ent_t  | [key, var] 
                      +---------------+    +---------------+
                      ^ Database dependency description
from dbdep_commit()   |
+---------------+*    |dd_dep
|  dbdep_dd_t   |-----+
+---------------+     |dd_dbdiff
(dd_)                 |
                      v
+---------------+     +---------------+  
|   dbdiff      |---->|    dbdiff_ent |  [key1, key2, add/change/rm] (dfe_)
+---------------+     +---------------+  
(df_) from dbdiff(),   (dfe_)
                     
 */
int
candidate_commit(clicon_handle h, char *candidate, char *running)
{
    struct dbdiff      df;
    dbdep_t           *dp;
    struct dbdiff_ent *dfe;
    dbdep_dd_t        *ddvec = NULL;
    dbdep_dd_t        *dd;
    int                nvec;
    int                retval = -1;
    int                i, j;
    int                failed = 0;
    struct stat        sb;
    void              *firsterr = NULL;

    /* Sanity checks that databases exists. */
    if (stat(running, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", running);
	goto done;
    }
    if (stat(candidate, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", candidate);
	goto done;
    }
    memset(&df, 0, sizeof(df));

    /* Find the differences between the two databases and store it in dd vector. */
    if (db_diff(running, candidate,
		__FUNCTION__,
		clicon_dbspec_key(h),
		&df
	    ) < 0)
	goto done;
    /* 1. Get commit processing dbdiff vector: one entry per key that changed.
          changes are registered as if they exist in the 1st(candidate) or
	  2nd(running) dbs.
     */
    if (dbdep_commitvec(h, &df, &nvec, &ddvec) < 0)
	goto done;
    
    /* 2. Call plugin pre-commit hooks */
    if (plugin_begin_hooks(h, candidate) < 0)
	goto done;

    /* call generic cv_validate() on all new or changed keys. */
    if (generic_validate(h, candidate, &df) < 0)
	goto done;

    /* user-defined callbacks */
    if (validate_db(h, nvec, ddvec, candidate) < 0)
	goto done;

    /* Call plugin post-commit hooks */
    if (plugin_complete_hooks(h, candidate) < 0)
	goto done;

    /* Now follows commit rules in order.
    * 4. For all keys that are in candidate, delete key (from running). 
    */
    for (i = nvec-1; i >= 0; i--){
	dd = &ddvec[i];
	dfe = dd->dd_dbdiff; /* key1/key2/op */
	dp = dd->dd_dep;     /* op, callback, arg */
	if (dp->dp_type & TRANS_CB_COMMIT &&
	    dfe->dfe_op & DBDIFF_OP_FIRST &&
	    plugin_modify_key_value(h, running,        /* db */
				    dfe->dfe_key1,     /* key */
				    TRANS_CB_COMMIT, /* commit|validate */
				    LV_DELETE,         /* operation */
				    dp                 /* callback(arg) */
		) < 0){
	    firsterr = clicon_err_save(); /* save this error */
	    break;
	}
    }
    /* 5. Failed deletion of running, add the key value back (from running) */
    if (i >= 0){ /* failed */
	for (j=i+1; j<nvec; j++){ /* revert in opposite order */
	    dd = &ddvec[j];
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if (dp->dp_type & TRANS_CB_COMMIT &&
		dfe->dfe_op & DBDIFF_OP_FIRST &&
		plugin_modify_key_value(h, running,       /* db */
					dfe->dfe_key1,    /* key */
					TRANS_CB_COMMIT,  /* commit|validate */
					LV_SET,           /* operation */
					dp                 /* callback(arg) */
		    ) < 0)
		continue; /* ignore errors or signal major setback ? */
	}
	goto done;
    }

    /* 6. Set keys (from candidate) */
    for (i=0; i < nvec; i++){
	dd = &ddvec[i];
	dfe = dd->dd_dbdiff; /* key1/key2/op */
	dp = dd->dd_dep;     /* op, callback, arg */
	if (dp->dp_type & TRANS_CB_COMMIT &&
	    dfe->dfe_op & DBDIFF_OP_SECOND &&
	    plugin_modify_key_value(h, candidate,        /* db */
				    dfe->dfe_key2,     /* key */
				    TRANS_CB_COMMIT, /* commit|validate */
				    LV_SET,         /* operation */
				    dp                 /* callback(arg) */
		) < 0){
	    firsterr = clicon_err_save(); /* save this error */
	    failed++;
	    break;
	}
    }
    if (!failed)
	if (file_cp(candidate, running) < 0){
	    clicon_err(OE_UNIX, errno, "file_cp");
	    failed++;
	}
    /* 7. Failed setting keys in running, first remove the keys set */
    if (failed){ /* failed */
	for (j=i-1; j>=0; j--){ /* revert in opposite order */
	    dd = &ddvec[j];
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if (dp->dp_type & TRANS_CB_COMMIT &&
		dfe->dfe_op & DBDIFF_OP_SECOND &&
		plugin_modify_key_value(h, candidate,    /* db */
					dfe->dfe_key2,   /* key */
					TRANS_CB_COMMIT, /* commit|validate */
					LV_DELETE,       /* operation */
					dp               /* callback(arg) */
		    ) < 0)
		continue; /* ignore errors or signal major setback ? */
	}
	/* 7. Set back original running values */
	for (j=0; j < nvec; j++){ /* revert in opposite order */
	    dd = &ddvec[j];
	    dfe = dd->dd_dbdiff; /* key1/key2/op */
	    dp = dd->dd_dep;     /* op, callback, arg */
	    if (dp->dp_type & TRANS_CB_COMMIT &&
		dfe->dfe_op & DBDIFF_OP_FIRST &&
		plugin_modify_key_value(h, running,        /* db */
					dfe->dfe_key1,     /* key */
					TRANS_CB_COMMIT, /* commit|validate */
					LV_SET,         /* operation */
					dp                 /* callback(arg) */
		    ) < 0)
		continue; /* ignore errors or signal major setback ? */
	}
	goto done;
    }


    /* Copy running back to candidate in case end functions triggered 
       updates in running */
    /* XXX: check for errors */
    file_cp(running, candidate);

    /* Call plugin post-commit hooks */
    plugin_end_hooks(h, candidate);

    retval = 0;
  done:
    if (retval < 0) /* Call plugin fail-commit hooks */
	plugin_abort_hooks(h, candidate);
    if (ddvec)
	free(ddvec);
    unchunk_group(__FUNCTION__); 
    if (firsterr)
	clicon_err_restore(firsterr);
    return retval;
}

int
candidate_validate(clicon_handle h, char *candidate, char *running)
{
    struct dbdiff      df;
    dbdep_dd_t        *ddvec = NULL;
    int                nvec;
    int                retval = -1;
    struct stat        sb;
    void              *firsterr = NULL;

    /* Sanity checks that databases exists. */
    if (stat(running, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", running);
	goto done;
    }
    if (stat(candidate, &sb) < 0){
	clicon_err(OE_DB, errno, "%s", candidate);
	goto done;
    }
    memset(&df, 0, sizeof(df));

    /* Find the differences between the two databases and store it in df vector. */
    if (db_diff(running, candidate,
		__FUNCTION__,
		clicon_dbspec_key(h),
		&df
	    ) < 0)
	goto done;
    /* 1. Get commit processing dbdiff vector (df): one entry per key that 
       changed. changes are registered as if they exist in the 1st(candidate) 
       or 2nd(running) dbs.
     */
    if (dbdep_commitvec(h, &df, &nvec, &ddvec) < 0)
	goto done;
    
    /* 2. Call plugin pre-commit hooks */
    if (plugin_begin_hooks(h, candidate) < 0)
	goto done;

    /* call generic cv_validate() on all new or changed keys. */
    if (generic_validate(h, candidate, &df) < 0)
	goto done;

    /* user-defined callbacks */
    if (validate_db(h, nvec, ddvec, candidate) < 0)
	goto done;

    /* Call plugin post-commit hooks */
    if (plugin_complete_hooks(h, candidate) < 0)
	goto done;

    retval = 0;
  done:
    if (retval < 0) /* Call plugin fail-commit hooks */
	plugin_abort_hooks(h, candidate);
    if (ddvec)
	free(ddvec);
    unchunk_group(__FUNCTION__); 
    if (firsterr)
	clicon_err_restore(firsterr);
    return retval;
}


/*
 * from_client_commit
 * Handle an incoming commit message from a client.
 * XXX: If commit succeeds and snapshot/startup fails, we have strange state:
 *   the commit has succeeded but an error message is returned.
 */
int
from_client_commit(clicon_handle h,
		   int s, 
		   struct clicon_msg *msg, 
		   const char *label)
{
    char *candidate, *running;
    uint32_t snapshot, startup;
    int retval = -1;
    char *snapshot_0;

    if (clicon_msg_commit_decode(msg, &candidate, &running, 
				&snapshot, &startup, label) < 0)
	goto err;

    if (candidate_commit(h, candidate, running) < 0){
	clicon_debug(1, "Commit %s failed",  candidate);
	retval = 0; /* We ignore errors from commit, but maybe
		       we should fail on fatal errors? */
	goto err;
    }
    clicon_debug(1, "Commit %s",  candidate);

    if (snapshot && config_snapshot(clicon_dbspec_key(h), running, clicon_archive_dir(h)) < 0)
	goto err;

    if (startup){
	snapshot_0 = chunk_sprintf(__FUNCTION__, "%s/0", 
				   clicon_archive_dir(h));
	if (file_cp(snapshot_0, clicon_startup_config(h)) < 0){
	    clicon_err(OE_PROTO, errno, "%s: Error when creating startup", 
		    __FUNCTION__);
		goto err;
	}
    }
    retval = 0;
    if (send_msg_ok(s) < 0)
	goto done;
    goto done;
  err:
    /* XXX: more elaborate errstring? */
    if (send_msg_err(s, clicon_errno, clicon_suberrno, "%s", clicon_err_reason) < 0)
	retval = -1;
  done:
    unchunk_group(__FUNCTION__);
    return retval; /* may be zero if we ignoring errors from commit */
} /* from_client_commit */



/*
 * Call backend plugin
 */
int
from_client_validate(clicon_handle h,
		     int s, 
		     struct clicon_msg *msg, 
		     const char *label)
{
    char *dbname;
    int retval = -1;

    if (clicon_msg_validate_decode(msg, &dbname, label) < 0){
	send_msg_err(s, clicon_errno, clicon_suberrno,
		     clicon_err_reason);
	goto err;
    }

    clicon_debug(1, "Validate %s",  dbname);

    if (candidate_validate(h, dbname, clicon_running_db(h)) < 0){
	clicon_debug(1, "Validate %s failed",  dbname);
	retval = 0; /* We ignore errors from commit, but maybe
		       we should fail on fatal errors? */
	goto err;
    }
    retval = 0;
    if (send_msg_ok(s) < 0)
	goto done;
    goto done;
  err:
    /* XXX: more elaborate errstring? */
    if (send_msg_err(s, clicon_errno, clicon_suberrno, "%s", clicon_err_reason) < 0)
	retval = -1;
  done:
    unchunk_group(__FUNCTION__);
    return retval;
} /* from_client_validate */
