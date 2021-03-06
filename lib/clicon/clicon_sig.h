/*
 *  CVS Version: $Id: clicon_sig.h,v 1.5 2013/08/01 09:15:46 olof Exp $
 *
  Copyright (C) 2009-2013 Olof Hagsand and Benny Holmgren
  Olof Hagsand
 *
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

#ifndef _CLICON_SIG_H_
#define _CLICON_SIG_H_

/*
 * Types
 */
typedef void (*sigfn_t)(int);

/*
 * Prototypes
 */ 
int set_signal(int signo, void (*handler)(int), void (**oldhandler)(int));
void clicon_signal_block(int);
void clicon_signal_unblock(int);
pid_t pidfile_check(char *pidfile, int kill_it);

#endif  /* _CLICON_SIG_H_ */
