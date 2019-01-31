#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wformat-overflow"
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#define IS_FREEBSD
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define GLOBAL_LOCAL_SEPARATOR(global, local, separator, always) { \
	if (global != NULL) { \
		if (*(global) && !(local)) { \
			printf(separator); \
		} \
		if (*(global) && always) { \
			printf(separator); \
		} \
		*(global) = true; \
		(local) = true; \
	} \
}

#define NEW_LINE(global, local) GLOBAL_LOCAL_SEPARATOR(global, local, "\n", false)
#define CSV_SEPARATOR(global, local) GLOBAL_LOCAL_SEPARATOR(global, local, ";", true)

bool safe_rw(uint64_t * addr, uint64_t * data, bool write);

#endif
