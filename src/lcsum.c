/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/lcsum.c,v 1.6 1993-03-30 17:24:04 deyke Exp $ */

/*
 * Word aligned linear buffer checksum routine.  Called from mbuf checksum
 * routine with simple args.  Intent is that this routine may be replaced
 * by assembly language routine for speed if so desired. (On the PC, the
 * replacement is in pcgen.asm.)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */

#include "global.h"
#include "ip.h"

int16
lcsum(wp,len)
register int16 *wp;
register int16 len;
{
	register int32 sum = 0;
	int16 result;

	while(len-- != 0)
		sum += *wp++;
	result = eac(sum);
#if BYTE_ORDER == LITTLE_ENDIAN
	/* Swap the result because of the (char *) to (int *) type punning */
	result = (result << 8) | (result >> 8);
#endif
	return result;
}

