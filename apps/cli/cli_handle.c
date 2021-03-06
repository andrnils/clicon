/*
 *  CVS Version: $Id: cli_handle.c,v 1.15 2013/09/11 18:53:39 olof Exp $
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
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <assert.h>
#include <dlfcn.h>
#include <dirent.h>
#include <libgen.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clicon/clicon.h>

#include "clicon_cli_api.h"
#include "cli_plugin.h"
#include "cli_handle.h"

#define CLICON_MAGIC 0x99aafabe

#define handle(h) (assert(clicon_handle_check(h)==0),(struct cli_handle *)(h))
#define cligen(h) (handle(h)->cl_cligen)

/*
 * cli_handle
 * first part of this is header, same for clicon_handle and config_handle.
 * Access functions for common fields are found in clicon lib: clicon_options.[ch]
 * This file should only contain access functions for the _specific_
 * entries in the struct below.
 */
struct cli_handle {
    int                      cl_magic;    /* magic (HDR)*/
    clicon_hash_t           *cl_copt;     /* clicon option list (HDR) */
    clicon_hash_t           *cl_data;     /* internal clicon data (HDR) */
    /* ------ end of common handle ------ */
    cligen_handle            cl_cligen;   /* cligen handle */

    int                      cl_usedaemon;     /* Send changes to configuration daemon */
    enum candidate_db_type   cl_candidate_type;
    /* Plugin group being active. Same as cli_cpg unless new group is being loaded*/
    struct cli_syntax_group *cl_active_cpg;
    /* Current plugin group. Same us cli_active_cpg(h) unless loading new group */
    struct cli_syntax_group *cl_cpg;
    /* If we've loaded a new plugin group, this is pending an unload */
    struct cli_syntax_group *cl_unloading_cpg;    
};

/*
 * cli_handle_init
 * returns a clicon handle for other CLICON API calls
 */
clicon_handle
cli_handle_init(void)
{
    struct cli_handle *cl;
    cligen_handle      clih = NULL;
    clicon_handle      h = NULL;

    if ((cl = (struct cli_handle *)clicon_handle_init0(sizeof(struct cli_handle))) == NULL)
	return NULL;

    if ((clih = cligen_init()) == NULL){
	clicon_handle_exit((clicon_handle)cl);
	goto done;
    }
    cligen_userhandle_set(clih, cl);
    cl->cl_cligen = clih;
    cl->cl_candidate_type = CANDIDATE_DB_SHARED;
    h = (clicon_handle)cl;
  done:
    return h;
}

/*
 * cli_handle_exit
 * frees clicon handle
 */
int
cli_handle_exit(clicon_handle h)
{
    cligen_handle ch = cligen(h);

    clicon_handle_exit(h); /* frees h and options */
    cligen_exit(ch);
    return 0;
}


/*----------------------------------------------------------
 * cli-specific handle access functions
 *----------------------------------------------------------*/
int 
cli_set_usedaemon(clicon_handle h, int usedaemon)
{
    struct cli_handle *cl = handle(h);

    cl->cl_usedaemon = usedaemon;
    return 0;
}

int 
cli_usedaemon(clicon_handle h)
{
    struct cli_handle *cl = handle(h);

    return cl->cl_usedaemon;
}

enum candidate_db_type
cli_candidate_type(clicon_handle h)
{
    struct cli_handle *cl = handle(h);

    return cl->cl_candidate_type;
}

int
cli_set_candidate_type(clicon_handle h, enum candidate_db_type type)
{
    struct cli_handle *cl = handle(h);

    cl->cl_candidate_type = type;
    return 0;
}

/* Current syntax-group */
struct cli_syntax_group *
cli_cpg(clicon_handle h)
{
    struct cli_handle *cl = handle(h);
    return cl->cl_cpg;
}

int
cli_set_cpg(clicon_handle h, struct cli_syntax_group *cpg)
{
    struct cli_handle *cl = handle(h);
    cl->cl_cpg = cpg;
    return 0;
}

/* Active syntax-group */
struct cli_syntax_group *
cli_active_cpg(clicon_handle h)
{
    struct cli_handle *cl = handle(h);
    return cl->cl_active_cpg;
}

int
cli_set_active_cpg(clicon_handle h, struct cli_syntax_group *cpg)
{
    struct cli_handle *cl = handle(h);
    cl->cl_active_cpg = cpg;
    return 0;
}

/* Syntax-group pending unload */
struct cli_syntax_group *
cli_unloading_cpg(clicon_handle h)
{
    struct cli_handle *cl = handle(h);
    return cl->cl_unloading_cpg;
}

int
cli_set_unloading_cpg(clicon_handle h, struct cli_syntax_group *cpg)
{
    struct cli_handle *cl = handle(h);
    cl->cl_unloading_cpg = cpg;
    return 0;
}

/*----------------------------------------------------------
 * cligen access functions
 *----------------------------------------------------------*/
cligen_handle
cli_cligen(clicon_handle h)
{
    return cligen(h);
}

/*
 * cli_interactive and clicon_eval
 */
int
cli_exiting(clicon_handle h)
{
    cligen_handle ch = cligen(h);

    return cligen_exiting(ch);
}
/*
 * cli_common.c: cli_quit
 * cli_interactive()
 */
int 
cli_set_exiting(clicon_handle h, int exiting)
{
    cligen_handle ch = cligen(h);

    return cligen_exiting_set(ch, exiting);
}

char
cli_comment(clicon_handle h)
{
    cligen_handle ch = cligen(h);

    return cligen_comment(ch);
}

char
cli_set_comment(clicon_handle h, char c)
{
    cligen_handle ch = cligen(h);

    return cligen_comment_set(ch, c);
}

char
cli_tree_add(clicon_handle h, char *tree, parse_tree pt)
{
    cligen_handle ch = cligen(h);

    return cligen_tree_add(ch, tree, pt);
}

char *
cli_tree_active(clicon_handle h)
{
    cligen_handle ch = cligen(h);

    return cligen_tree_active(ch);
}

int
cli_tree_active_set(clicon_handle h, char *treename)
{
    cligen_handle ch = cligen(h);

    return cligen_tree_active_set(ch, treename);
}

parse_tree *
cli_tree(clicon_handle h, char *name)
{
    cligen_handle ch = cligen(h);

    return cligen_tree(ch, name);
}

int
cli_parse_file(clicon_handle h,
	       FILE *f,
	       char *name, /* just for errs */
	       parse_tree *pt,
	       cvec *globals)
{
    cligen_handle ch = cligen(h);

    return cligen_parse_file(ch, f, name, pt, globals);
}

int
cli_susp_hook(clicon_handle h, cli_susphook_t *fn)
{
    cligen_handle ch = cligen(h);

    /* This assume first arg of fn can be treated as void* */
    return cligen_susp_hook(ch, (cligen_susp_cb_t*)fn); 
}

char *
cli_nomatch(clicon_handle h)
{
    cligen_handle ch = cligen(h);

    return cligen_nomatch(ch);
}

int
cli_prompt_set(clicon_handle h, char *prompt)
{
    cligen_handle ch = cligen(h);
    return cligen_prompt_set(ch, prompt);
}

int
cli_logsyntax_set(clicon_handle h, int status)
{
    cligen_handle ch = cligen(h);
    return cligen_logsyntax_set(ch, status);
}
