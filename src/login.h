/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/login.h,v 1.8 1992-10-05 17:29:25 deyke Exp $ */

#ifndef _LOGIN_H
#define _LOGIN_H

/* In login.c: */
void fixutmpfile __ARGS((void));
struct passwd *getpasswdentry __ARGS((const char *name, int create));
struct login_cb *login_open __ARGS((const char *user, const char *protocol,
	void (*read_upcall)__ARGS ((void *arg)),
	void (*close_upcall)__ARGS ((void *arg)),
	void *upcall_arg));
void login_close __ARGS((struct login_cb *tp));
struct mbuf *login_read __ARGS((struct login_cb *tp, int cnt));
void login_write __ARGS((struct login_cb *tp, struct mbuf *bp));

#endif  /* _LOGIN_H */
