#ifndef MASH_COMPAT_H
#define MASH_COMPAT_H

#define _DEFAULT_SOUCE // reallocarray glibc 2.29
#define _GNU_SOURCE    // reallocarray glibc <= 2.28, strchrnul glibc 2.1.1

#include <stdlib.h>
void *reallocarray(void*, size_t, size_t);

#include <string.h>
char *strchrnul(const char*, int);

// HOST_NAME_MAX not defined in macOS
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

#endif
