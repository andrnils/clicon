# 
# Makefile 
# CVS Version: $Id: Makefile.in,v 1.4 2013/09/19 16:03:40 olof Exp $ 
#
# Copyright (C) 2009-2013 Olof Hagsand and Benny Holmgren
#
# This file is part of CLICON.
#
# CLICON is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# CLICON is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CLICON; see the file COPYING.  If not, see
# <http://www.gnu.org/licenses/>.
#
#

srcdir  	= .
top_srcdir  	= ../..
prefix 		= /usr/local
bindir 		= ${exec_prefix}/bin
includedir	= ${prefix}/include
localstatedir	= ${prefix}/var
INSTALL		= @INSTALL@
APPDIR          = /perfmon
CC		= gcc
CFLAGS  	= -g -Wall -Werror -rdynamic -fPIC 

INCLUDES 	= -I$(includedir)  

FE_PLUGIN       = ntp_cli.so
NC_PLUGIN       = ntp_netconf.so
BE_PLUGIN       = ntp_backend.so

PLUGINS = $(FE_PLUGIN) $(BE_PLUGIN) $(NC_PLUGIN) 

all:	$(PLUGINS)

# You can add more .c:s for every SRC.
FE_SRC   = ntp_cli.c
NC_SRC   = ntp_netconf.c
BE_SRC   = ntp_backend.c

SRC = $(FE_SRC) $(BE_SRC) $(NC_SRC) 

.c.o: $(SRC)
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) -c $<

FE_OBJ   = $(FE_SRC:%.c=%.o)
BE_OBJ   = $(BE_SRC:%.c=%.o)
NC_OBJ   = $(NC_SRC:%.c=%.o)

OBJS = $(FE_OBJ) $(BE_OBJ) $(NC_OBJ) 

$(FE_PLUGIN): $(FE_OBJ)
	$(CC) -shared -o $(FE_PLUGIN) -lc $<

$(NC_PLUGIN): $(NC_OBJ)
	$(CC) -shared -o $(NC_PLUGIN) -lc $<

$(BE_PLUGIN): $(BE_OBJ)
	$(CC) -shared -o $(BE_PLUGIN) -lc $<

clean:
	rm -f $(OBJS) $(PLUGINS)

distclean: clean
	rm -f clicon.conf Makefile *~ .depend

install:	clicon.conf ntp.spec ntp_cli.cli $(PLUGINS)
	install -m 755 -d $(DESTDIR)$(APPDIR) 
	install -m 755 clicon.conf $(DESTDIR)$(APPDIR)
	install -m 755 ntp.spec $(DESTDIR)$(APPDIR)
	install -m 755 -d $(DESTDIR)$(APPDIR)/frontend
	install -m 755 -d $(DESTDIR)$(APPDIR)/netconf
	install -m 755 -d $(DESTDIR)$(APPDIR)/backend
	install -m 755 -d $(DESTDIR)$(APPDIR)/db
	install -m 755 -d $(DESTDIR)$(APPDIR)/archive
	for i in $(FE_PLUGIN); do \
		install $$i $(DESTDIR)$(APPDIR)/frontend/`basename $$i`; \
	done
	for i in $(NC_PLUGIN); do \
		install $$i $(DESTDIR)$(APPDIR)/netconf/`basename $$i`; \
	done
	for i in $(BE_PLUGIN); do \
		install $$i $(DESTDIR)$(APPDIR)/backend/`basename $$i`; \
	done
	for i in ntp_cli.cli; do \
		install $$i $(DESTDIR)$(APPDIR)/frontend/`basename $$i`; \
	done

uninstall: 
	rm -rf $(APPDIR)/frontend
	rm -rf $(APPDIR)/backend
	rm -rf $(APPDIR)/netconf
	rm -rf $(APPDIR)/db
	rm -rf $(APPDIR)/archive
	rm -rf $(APPDIR)/ntp.spec
	rm -rf $(APPDIR)/*

install-include:	

depend:
	$(CC) $(DEPENDFLAGS) -DHAVE_CONFIG_H $(INCLUDES) $(CFLAGS) -MM $(SRC) > .depend

#include .depend

