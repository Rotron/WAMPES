# @(#) $Header: /home/deyke/tmp/cvs/tcp/Makefile,v 1.25 1995-06-04 09:36:30 deyke Exp $

MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi

OBSOLETE   = bbs/sfstat* \
	     examples/bbs.conf \
	     lib/bbs.h \
	     src/linux_include/stdlib.h \
	     src/mail_bbs.* \
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
	-if [ -f users ]; then $(MAKE) -i -f $(MAKEFILE) /usr/local/lib/users; fi

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

/usr/local/lib/users: users
	$(MKDIR)
	cp $? $@ && /usr/local/etc/udbm

clean:;

depend:;
