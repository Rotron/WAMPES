# @(#) $Id: Makefile,v 1.36 1999-01-22 21:19:44 deyke Exp $

OBSOLETE   = /usr/local/bin/sfstat \
	     bbs/bbs.h \
	     bbs/findpath* \
	     bbs/help* \
	     bbs/killdup* \
	     bbs/sfstat* \
	     doc/bbs.2.gif \
	     lib/bbs.h \
	     src/cc \
	     src/linux_include/stdlib.h \
	     src/mail_bbs.* \
	     users \
	     util/genupd

DIRS       = lib \
	     aos \
	     NeXT \
	     src \
	     convers \
	     util \
	     bbs

all:;   @-rm -f $(OBSOLETE)
	-chmod 755 cc
	-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i all install ); done
	-$(MAKE) -i /tcp/hostaddr.pag
	-if [ -d tools ]; then ( cd tools; $(MAKE) -i all install ); fi

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

distrib:
	@version=`awk -F- '/.#.WAMPES-/ {print substr($$2,1,6)}' < src/version.c`; \
	sources=`find \
		*.R \
		ChangeLog \
		Makefile \
		NeXT/*.[ch] \
		NeXT/*.sh \
		NeXT/Makefile \
		NeXT/README.NeXT \
		NeXT/uname-next \
		README \
		aos/*.[ch] \
		aos/Makefile \
		bbs/*.[ch] \
		bbs/Makefile \
		bbs/bbs.help \
		cc \
		convers/*.[ch] \
		convers/Makefile \
		doc/?*.* \
		domain.txt \
		examples/?*.* \
		hosts \
		lib/*.[ch] \
		lib/Makefile \
		lib/configure \
		src/*.[ch] \
		src/Makefile \
		src/linux_include/*/*.h \
		util/*.[ch] \
		util/Makefile \
		! -name configure.h -print`; \
	tar cvf - $$sources | gzip -9 -v > wampes-$$version.tar.gz; \
	cp README wampes-$$version.txt

clean:; -for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i clean ); done

depend:; -for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i depend ); done
