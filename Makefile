# @(#) $Header: /home/deyke/tmp/cvs/tcp/Makefile,v 1.27 1995-06-10 17:23:20 deyke Exp $

MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi

OBSOLETE   = bbs/bbs.h \
	     bbs/findpath* \
	     bbs/killdup* \
	     bbs/sfstat* \
	     lib/bbs.h \
	     src/linux_include/stdlib.h \
	     src/mail_bbs.* \
	     users \
	     util/genupd \
	     /usr/local/bin/sfstat

all:;   @-rm -f $(OBSOLETE)
	-chmod 755 cc
	-cd lib;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd aos;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd src;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd convers; $(MAKE) -i -f $(MAKEFILE) all install
	-cd util;    $(MAKE) -i -f $(MAKEFILE) all install
	-cd bbs;     $(MAKE) -i -f $(MAKEFILE) all install
	-            $(MAKE) -i -f $(MAKEFILE) /tcp/hostaddr.pag

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

clean:;

depend:;
