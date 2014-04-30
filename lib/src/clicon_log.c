/*
 *  CVS Version: $Id: clicon_log.c,v 1.6 2013/08/01 09:15:46 olof Exp $
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
 * Regular logging and debugging. Syslog using levels.
 */

#ifdef HAVE_CONFIG_H
#include "clicon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

/* clicon */
#include "clicon_err.h"
#include "clicon_log.h"

/* The global debug level. 0 means no debug */
int debug = 0;

/* Bitmask whether to log to syslog or stderr: CLICON_LOG_STDERR | CLICON_LOG_SYSLOG */
static int _logflags = 0x0;

/* Function pointer to log notify callback */
static clicon_log_notify_t *_log_notify_cb  = NULL;
static void                *_log_notify_arg = NULL;

/* Set to open file to bypass logging and write debug messages directly to file */
static FILE *_debugfile = NULL;

/*!
 * \brief Initialize system logger.
 *
 * Make syslog(3) calls with specified ident and gates calls of level upto specified level (upto).
 * May also print to stderr, if err is set.
 * Applies to clicon_err() and clicon_debug too
 *
 * Args:
 * IN    ident       prefix that appears on syslog (eg 'cli')
 * IN    upto        log priority, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG (see syslog(3)).
 * IN    flags       bitmask: if CLICON_LOG_STDERR, then print logs to stderr
                              if CLICON_LOG_SYSLOG, then print logs to syslog
			      You can do a combination of both
 */
int
clicon_log_init(char *ident, int upto, int flags)
{
    if (setlogmask(LOG_UPTO(upto)) < 0)
	/* Cant syslog here */
	fprintf(stderr, "%s: setlogmask: %s\n", __FUNCTION__, strerror(errno)); 
    _logflags = flags;
    openlog(ident, LOG_PID, LOG_USER); /* LOG_PUSER is achieved by direct stderr logs in clicon_log */
    return 0;
}
/*!
 * \brief Register log callback, return old setting
 */
clicon_log_notify_t *
clicon_log_register_callback(clicon_log_notify_t *cb, void *arg)
{
    clicon_log_notify_t *old = _log_notify_cb;
    _log_notify_cb  = cb;
    _log_notify_arg = arg;
    return old;
}

/*
 * Mimic syslog and print a time on file f
 */
static int
flogtime(FILE *f)
{
    struct timeval tv;
    struct tm *tm;

    gettimeofday(&tv, NULL);
    tm = localtime((time_t*)&tv.tv_sec);
    fprintf(f, "%s %2d %02d:%02d:%02d: ", 
	    mon2name(tm->tm_mon), tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);
    return 0;
}

/*
 * Mimic syslog and print a time on string s
 * String returned needs to be freed.
 */
static char *
slogtime(void)
{
    struct timeval tv;
    struct tm     *tm;
    char           *str;

    /* Example: "Apr 14 11:30:52: " len=17+1 */
    if ((str = malloc(18)) == NULL){
	fprintf(stderr, "%s: malloc: %s\n", __FUNCTION__, strerror(errno));
	return NULL;
    }
    gettimeofday(&tv, NULL);
    tm = localtime((time_t*)&tv.tv_sec);
    snprintf(str, 18, "%s %2d %02d:%02d:%02d: ", 
	     mon2name(tm->tm_mon), tm->tm_mday,
	     tm->tm_hour, tm->tm_min, tm->tm_sec);
    return str;
}


/*!
 * \brief Make a logging call to syslog.
 *
 * This is the _only_ place the actual syslog (or stderr) logging is made in clicon,..
 * See also clicon_log()
 *
 * Args:
 * IN   level    log level, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG. Thisis OR:d with facility == LOG_USER
 * IN   format   Message to print as argv.
 */
int
clicon_log_str(int level, char *msg)
{
    if (_logflags & CLICON_LOG_SYSLOG)
	syslog(LOG_MAKEPRI(LOG_USER, level), "%s", msg);
    if (_logflags & CLICON_LOG_STDERR){
	flogtime(stderr);
	fprintf(stderr, "%s\n", msg);
    }
    if (_logflags & CLICON_LOG_STDOUT){
	flogtime(stdout);
	fprintf(stdout, "%s\n", msg);
    }
    if (_log_notify_cb){
	static int  cb = 0;
	char       *d, *msg2;
	int         len;

	if (cb++ == 0){
	    /* Here there is danger of recursion: if callback in turn logs, therefore
	       make static check (should be stack-based - now global) 
	    */
	    if ((d = slogtime()) == NULL)
		return -1;
	    len = strlen(d) + strlen(msg) + 1;
	    if ((msg2 = malloc(len)) == NULL){
		fprintf(stderr, "%s: malloc: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	    }
	    snprintf(msg2, len, "%s%s", d, msg);
	    _log_notify_cb(level, msg2, _log_notify_arg);
	    free(d);
	    free(msg2);
	}
	cb--;
    }
    return 0;
}

/*!
 * \brief Make a logging call to syslog using variable arg syntax.
 *
 * See also clicon_log_init() and clicon_log_str()
 *
 * Args:
 * IN   level    log level, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG. Thisis OR:d with facility == LOG_USER
 * IN   format   Message to print as argv.
 */
int
clicon_log(int level, char *format, ...)
{
    va_list args;
    int     len;
    char   *msg    = NULL;
    int     retval = -1;

    /* first round: compute length of debug message */
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    /* allocate a message string exactly fitting the message length */
    if ((msg = malloc(len+1)) == NULL){
	fprintf(stderr, "malloc: %s\n", strerror(errno)); /* dont use clicon_err here due to recursion */
	goto done;
    }

    /* second round: compute write message from format and args */
    va_start(args, format);
    if (vsnprintf(msg, len+1, format, args) < 0){
	va_end(args);
	fprintf(stderr, "vsnprintf: %s\n", strerror(errno)); /* dont use clicon_err here due to recursion */
	goto done;
    }
    va_end(args);
    /* Actually log it */
    clicon_log_str(level, msg);

    retval = 0;
  done:
    if (msg)
	free(msg);
    return retval;
}


/*!
 * \brief Initialize debug messages. Set debug level.
 *
 * Initialize debug module. The level is used together with clicon_debug(dbglevel) calls as follows: 
 * print message if level >= dbglevel.
 * Example: clicon_debug_init(1) -> debug(1) is printed, but not debug(2).
 * Normally, debug messages are sent to clicon_log() which in turn can be sent to syslog and/or stderr.
 * But you can also override this with a specific debug file so that debug messages are written on the file
 * independently of log or errors. This is to ensure that a syslog of normal logs is unpolluted by extensive
 * debugging.
 *
 * Args:
 * IN    dbglevel   0 is show no debug messages, 1 is normal, 2.. is high debug. Note this is not
 *                  level from syslog(3)
 * IN    f          Debug-file. Open file where debug messages are directed. This overrides the clicon_log settings
 *                  which is otherwise where debug messages are directed.
 */
int
clicon_debug_init(int dbglevel, FILE *f)
{
    debug = dbglevel; /* Global variable */
    return 0;
}


/*!
 * \brief Print a debug message with debug-level. Settings determine where msg appears.
 *
 * If the dbglevel passed in the function is equal to or lower than the one set by 
 * clicon_debug_init(level).  That is, only print debug messages <= than what you want:
 *      print message if level >= dbglevel.
 * The message is sent to clicon_log. EIther to syslog, stderr or both, depending on 
 * clicon_log_init() setting
 * 
 * Args:
 * IN dbglevel   0 always called (dont do this: not really a dbg message)
 *               1 default level if passed -D
 *               2.. Higher debug levels
 * IN format     Message to print as argv.
 * 
 */
int
clicon_debug(int dbglevel, char *format, ...)
{
    va_list args;
    int     len;
    char   *msg    = NULL;
    int     retval = -1;

    if (dbglevel > debug) /* debug mask */
	return 0;
    /* first round: compute length of debug message */
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    /* allocate a message string exactly fitting the messgae length */
    if ((msg = malloc(len+1)) == NULL){
	clicon_err(OE_UNIX, errno, "malloc");
	goto done;
    }
    /* second round: compute write message from format and args */
    va_start(args, format);
    if (vsnprintf(msg, len+1, format, args) < 0){
	va_end(args);
	clicon_err(OE_UNIX, errno, "vsnprintf");
	goto done;
    }
    va_end(args);
    if (_debugfile != NULL){ /* Bypass syslog altogether */
	/* XXX: Here use date sub-routine as found in err_print1 */
	flogtime(_debugfile);
	fprintf(_debugfile, "%s\n", msg);
    }
    else
	clicon_log_str(LOG_DEBUG, msg);
    retval = 0;
  done:
    if (msg)
	free(msg);
    return retval;
}

/*!
 * \brief Translate month number (0..11) to a three letter month name
 */
char *
mon2name(int md)
{
    switch(md){
    case 0: return "Jan";
    case 1: return "Feb";
    case 2: return "Mar";
    case 3: return "Apr";
    case 4: return "May";
    case 5: return "Jun";
    case 6: return "Jul";
    case 7: return "Aug";
    case 8: return "Sep";
    case 9: return "Oct";
    case 10: return "Nov";
    case 11: return "Dec";
    default:
	break;
    }
    return NULL;
}

