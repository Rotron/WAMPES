# @(#) $Header: /home/deyke/tmp/cvs/tcp/Makefile,v 1.1 1992-08-19 13:13:20 deyke Exp $

PATH  = /bin:/usr/bin:/usr/local/bin:/usr/contrib/bin:/usr/local/etc
SHELL = /bin/sh

.IGNORE:

all:
	@(dir=src     ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=convers ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=util    ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=bbs     ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@make /usr/local/lib/users

/usr/local/lib/users: users
	@cp users /usr/local/lib && udbm
