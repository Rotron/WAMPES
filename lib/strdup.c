/* @(#) $Header: /home/deyke/tmp/cvs/tcp/lib/strdup.c,v 1.6 1995-11-19 11:54:19 deyke Exp $ */

#ifdef __cplusplus
#define EXTERN_C        extern "C"
#else
#define EXTERN_C
#endif

EXTERN_C char *strcpy(char *s1, const char *s2);
EXTERN_C unsigned int strlen(const char *s);
EXTERN_C void *malloc(int size);

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = (char *) malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}
