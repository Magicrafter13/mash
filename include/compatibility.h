#ifndef MASH_COMPAT_H
#define MASH_COMPAT_H

#define _DEFAULT_SOUCE // reallocarray glibc 2.29
#define _GNU_SOURCE    // reallocarray glibc <= 2.28
#include <stdlib.h>
void *reallocarray(void*, size_t, size_t);

#endif
