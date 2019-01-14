#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdbool.h>
#include <stdint.h>

#define IS_FREEBSD defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define NEW_LINE(global, local) { \
	if (global) { \
		if (*(global) && !(local)) { \
			printf("\n"); \
		} \
		*(global) = true; \
		(local) = true; \
	} \
}

bool safe_rw(uint64_t * addr, uint64_t * data, bool write);

#endif
