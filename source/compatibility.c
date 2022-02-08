#include "compatibility.h"

// Really stupid thing for Android
#if !defined(__GLIBC__) && defined(__BIONIC__)
#define __GLIBC__ 3
#endif

// reallocarray (realloc with extra safety check)
#if (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 26))
#include <errno.h>
void *reallocarray(void *ptr, size_t nmemb, size_t size) {
	size_t new_block_size = nmemb * size;
	if (nmemb != 0 && new_block_size / nmemb != size) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(ptr, new_block_size);
}
#endif
