#include "configure.h"

#if !HAS_STRDUP || defined _AIX

#include <stdlib.h>
#include <string.h>

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = (char *) malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}

#else

void strdup_prevent_empty_file_message(void)
{
}

#endif
