/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/alloc.c,v 1.3 1990-02-12 11:55:01 deyke Exp $ */

#include <memory.h>
#include <stdio.h>

extern char  *sbrk();

#define DEBUG           1

#define ALLOCSIZE       0x4000
#define FREETABLESIZE   1024
#define MEMFULL         ((struct block *) (-1))
#define MINSIZE         16
#define SBRK(i)         ((struct block *) sbrk((int) (i)))

struct block {
  struct block *next;
};

static struct block freetable[FREETABLESIZE];
static unsigned int  allocated;
static unsigned int  failures;
static unsigned int  inuse;

/*---------------------------------------------------------------------------*/

static void giveup(mesg)
char  *mesg;
{
  fprintf(stderr, mesg);
  abort();
}

/*---------------------------------------------------------------------------*/

char  *malloc(size)
register unsigned int  size;
{

  static struct block *freespace;
  static unsigned int  freesize;

  int  align_error;
  register struct block *p, *tp;

  size = (size + sizeof(struct block *) + MINSIZE - 1) & ~(MINSIZE - 1);
  if ((tp = freetable + size / MINSIZE) >= freetable + FREETABLESIZE)
    giveup("malloc: requested block too large\n");
  if (p = tp->next)
    tp->next = p->next;
  else {
    if (size > freesize) {
      if (freesize) {
	struct block *tpf = freetable + freesize / MINSIZE;
	freespace->next = tpf->next;
	tpf->next = freespace;
	freesize = 0;
      }
      if ((freespace = SBRK(ALLOCSIZE)) == MEMFULL) {
	failures++;
	return 0;
      }
      allocated += (freesize = ALLOCSIZE);
      if (align_error = (MINSIZE - 1) & -(((int) freespace) + sizeof(struct block *))) {
	freespace = (struct block *) (align_error + (int) freespace);
	freesize -= MINSIZE;
      }
    }
    p = freespace;
    freespace += (size / sizeof(struct block *));
    freesize -= size;
  }
  p->next = tp;
  inuse += size;
  return (char *) (p + 1);
}

/*---------------------------------------------------------------------------*/

char  *_malloc(size)
unsigned int  size;
{
  return malloc(size);
}

/*---------------------------------------------------------------------------*/

void free(pp)
char  *pp;
{
  register struct block *p, *tp;

  if (p = (struct block *) pp) {
#if DEBUG
    if ((MINSIZE - 1) & (int) p) giveup("free: bad alignment\n");
#else
    if ((MINSIZE - 1) & (int) p) return;
#endif
    p--;
    tp = p->next;
#if DEBUG
    if (tp < freetable || tp >= freetable + FREETABLESIZE)
      giveup("free: bad free table pointer\n");
#else
    if (tp < freetable || tp >= freetable + FREETABLESIZE) return;
#endif
    inuse -= (tp - freetable) * MINSIZE;
    p->next = tp->next;
    tp->next = p;
  }
}

/*---------------------------------------------------------------------------*/

void _free(pp)
char  *pp;
{
  free(pp);
}

/*---------------------------------------------------------------------------*/

/* Return size of allocated buffer, in bytes */

unsigned int  blksize(p)
char  *p;
{
  register struct block *tp;

  tp = (struct block *) p;
  tp--;
  tp = tp->next;
  return (tp - freetable) * MINSIZE - sizeof(struct block *);
}

/*---------------------------------------------------------------------------*/

char  *realloc(p, size)
char  *p;
unsigned int  size;
{

  char  *q;
  unsigned int  oldsize;

  oldsize = blksize(p);
  if (size <= oldsize) return p;
  if (q = malloc(size)) {
    memcpy(q, p, (int) oldsize);
    free(p);
  }
  return q;
}

/*---------------------------------------------------------------------------*/

char  *_realloc(p, size)
char  *p;
unsigned int  size;
{
  return realloc(p, size);
}

/*---------------------------------------------------------------------------*/

char  *calloc(nelem, elsize)
unsigned int  nelem, elsize;
{
  register char  *p, *q;
  register unsigned int  size;

  if (p = q = malloc(size = nelem * elsize))
    while (size--) *q++ = '\0';
  return p;
}

/*---------------------------------------------------------------------------*/

char  *_calloc(nelem, elsize)
unsigned int  nelem, elsize;
{
  return calloc(nelem, elsize);
}

/*---------------------------------------------------------------------------*/

int  memstat()
{
  printf("%7u bytes of memory allocated\n", allocated);
  printf("%7u bytes of memory being used (%lu%% used)\n", inuse, 100l * inuse / allocated);
  printf("%7u requests for memory denied\n", failures);
  return 0;
}

