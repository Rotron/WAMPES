/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/misc.c,v 1.21 1995-12-20 09:46:50 deyke Exp $ */

/* Miscellaneous machine independent utilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <ctype.h>
#include <stdio.h>
#include "global.h"
#include "socket.h"
#include "mbuf.h"

char Whitespace[] = " \t\r\n";

int Xtolower(
int c)
{
	return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

int Xtoupper(
int c)
{
	return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

/* Select from an array of strings, or return ascii number if out of range */
char *
smsg(
char *msgs[],
unsigned nmsgs,unsigned n)
{
	static char buf[16];

	if(n < nmsgs && msgs[n] != NULL)
		return msgs[n];
	sprintf(buf,"%u",n);
	return buf;
}

/* Convert hex-ascii to integer */
int
htoi(
char *s)
{
	int i = 0;
	char c;

	while((c = *s++) != '\0'){
		if(c == 'x')
			continue;       /* allow 0x notation */
		if('0' <= c && c <= '9')
			i = (i * 16) + (c - '0');
		else if('a' <= c && c <= 'f')
			i = (i * 16) + (c - 'a' + 10);
		else if('A' <= c && c <= 'F')
			i = (i * 16) + (c - 'A' + 10);
		else
			break;
	}
	return i;
}
/* Convert single hex-ascii character to binary */
int
htob(
char c)
{
	if('0' <= c && c <= '9')
		return c - '0';
	else if('a' <= c && c <= 'f')
		return c - 'a' + 10;
	else if('A' <= c && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}
/* Read an ascii-encoded hex string, convert to binary and store in
 * output buffer. Return number of bytes converted
 */
int
readhex(
uint8 *out,
char *in,
int size)
{
	int c,count;

	if(in == NULL)
		return 0;
	for(count=0;count < size;count++){
		while(*in == ' ' || *in == '\t')
			in++;   /* Skip white space */
		if((c = htob(*in++)) == -1)
			break;  /* Hit non-hex character */
		out[count] = c << 4;    /* First nybble */
		while(*in == ' ' || *in == '\t')
			in++;   /* Skip white space */
		if((c = htob(*in++)) == -1)
			break;  /* Hit non-hex character */
		out[count] |= c;        /* Second nybble */
	}
	return count;
}
/* replace terminating end of line marker(s) with null */
void
rip(
register char *s)
{
	register char *cp = s;

	while (*cp)
		cp++;
	while (--cp >= s && (*cp == '\r' || *cp == '\n'))
		;
	cp[1] = '\0';
}
/* Count the occurrances of 'c' in a buffer */
int
memcnt(
uint8 *buf,
uint8 c,
int size)
{
	int cnt = 0;
	uint8 *icp;

	while(size != 0){
		int change;

		if((icp = (uint8 *) memchr(buf,c,size)) == NULL)
			break;  /* No more found */
		/* Advance the start of the next search to right after
		 * this character
		 */
		change = (int) (icp - buf + 1);
		buf += change;
		size -= change;
		cnt++;
	}
	return cnt;
}
/* XOR block 'b' into block 'a' */
void
memxor(
uint8 *a,
uint8 *b,
unsigned int n)
{
	while(n-- != 0)
		*a++ ^= *b++;
}

#if 0
/* Copy a string to a malloc'ed buffer. Turbo C has this one in its
 * library, but it doesn't call mallocw() and can therefore return NULL.
 * NOS uses of strdup() generally don't check for NULL, so they need this one.
 */
char *
strdup(
const char *s)
{
	register char *out;
	register int len;

	if(s == NULL)
		return NULL;
	len = strlen(s);
	out = mallocw(len+1);
	/* This is probably a tad faster than strcpy, since we know the len */
	memcpy(out,s,len);
	out[len] = '\0';
	return out;
}
#endif
/* Routines not needed for Turbo 2.0, but available for older libraries */
/* #ifdef  AZTEC */

/* Case-insensitive string comparison */

int stricmp(
char *s1,char *s2)
{
	while(Xtolower(*s1) == Xtolower(*s2)){
		if(!*s1)
			return 0;
		s1++;
		s2++;
	}
	return Xtolower(*s1) - Xtolower(*s2);
}

int strnicmp(
char *a,char *b,
size_t n)
{
	char a1=0,b1=0;

	while(n-- != 0 && (a1 = *a++) != '\0' && (b1 = *b++) != '\0'){
		if(a1 == b1)
			continue;       /* No need to convert */
		a1 = Xtolower(a1);
		b1 = Xtolower(b1);
		if(a1 == b1)
			continue;       /* NOW they match! */
		if(a1 > b1)
			return 1;
		if(a1 < b1)
			return -1;
	}
	return 0;
}

#ifdef  AZTEC
char *
strtok(
char *s1,       /* Source string (first call) or NULL */
#ifdef  __STDC__        /* Ugly kludge for aztec's declaration */
const char *s2) /* Delimiter string */
#else
char *s2)       /* Delimiter string */
#endif
{
	static int isdelim();
	static char *next;
	register char *cp;
	char *tmp;

	if(s2 == NULL)
		return NULL;    /* Must give delimiter string */

	if(s1 != NULL)
		next = s1;              /* First call */

	if(next == NULL)
		return NULL;    /* No more */

	/* Find beginning of this token */
	for(cp = next;*cp != '\0' && isdelim(*cp,s2);cp++)
		;

	if(*cp == '\0')
		return NULL;    /* Trailing delimiters, no token */

	/* Save the beginning of this token, and find its end */
	tmp = cp;
	next = NULL;    /* In case we don't find another delim */
	for(;*cp != '\0';cp++){
		if(isdelim(*cp,s2)){
			*cp = '\0';
			next = cp + 1;  /* Next call will begin here */
			break;
		}
	}
	return tmp;
}
static int
isdelim(
char c,
register char *delim)
{
	char d;

	while((d = *delim++) != '\0'){
		if(c == d)
			return 1;
	}
	return 0;
}
#endif  /* AZTEC */

/* Host-network conversion routines, replaced on the x86 with
 * assembler code in pcgen.asm
 */
#ifndef MSDOS
/* Put a long in host order into a char array in network order */
uint8 *
put32(
register uint8 *cp,
int32 x)
{
	*cp++ = (uint8) (x >> 24);
	*cp++ = (uint8) (x >> 16);
	*cp++ = (uint8) (x >>  8);
	*cp++ = (uint8) (x      );
	return cp;
}
/* Put a short in host order into a char array in network order */
uint8 *
put16(
register uint8 *cp,
uint16 x)
{
	*cp++ = x >> 8;
	*cp++ = x;

	return cp;
}
uint16
get16(
register uint8 *cp)
{
	register uint16 x;

	x = *cp++;
	x <<= 8;
	x |= *cp;
	return x;
}
/* Machine-independent, alignment insensitive network-to-host long conversion */
int32
get32(
register uint8 *cp)
{
	int32 rval;

	rval = *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;
	rval |= *cp;

	return rval;
}
/* Compute int(log2(x)) */
int
ilog2(
register uint16 x)
{
	register int n = 16;
	for(;n != 0;n--){
		if(x & 0x8000)
			break;
		x <<= 1;
	}
	n--;
	return n;
}

#endif

