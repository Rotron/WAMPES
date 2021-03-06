#ifndef _LOCKFILE_H
#define _LOCKFILE_H

/* In lockfile.c: */
int lock_fd(int fd, int exclusive, int dont_block);
int lock_file(const char *filename, int exclusive, int dont_block);

#endif  /* _LOCKFILE_H */
