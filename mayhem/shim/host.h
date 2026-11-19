/* The few host libc/POSIX functions the shim and harnesses use. Declared by hand because the Limine
 * TUs are built against freestanding headers, whose names (fopen, print, ...) collide with stdio. */
#ifndef MAYHEM_SHIM_HOST_H
#define MAYHEM_SHIM_HOST_H
#include <stddef.h>
#include <stdint.h>
void *calloc(size_t n, size_t sz);
void *malloc(size_t sz);
void free(void *p);
char *strdup(const char *s);
int open(const char *path, int flags, ...);
long read(int fd, void *buf, size_t n);
long write(int fd, const void *buf, size_t n);
int close(int fd);
_Noreturn void _exit(int code);
_Noreturn void abort(void);
/* Read a whole file (at most max bytes) into a malloc'd buffer; returns NULL on failure. */
uint8_t *mayhem_read_file(const char *path, size_t max, size_t *len);
#endif
