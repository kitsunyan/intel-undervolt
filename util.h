#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#if defined(__GNUC__) && __GNUC__ >= 7
#define BEGIN_IGNORE_FORMAT_OVERFLOW \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wformat-overflow\"")
#define END_IGNORE_FORMAT_OVERFLOW \
_Pragma("GCC diagnostic pop")
#else
#define BEGIN_IGNORE_FORMAT_OVERFLOW
#define END_IGNORE_FORMAT_OVERFLOW
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

typedef struct {
	int count;
} array_t;

array_t * array_new(int item_size, void (* item_free)(void *));
void * array_get(array_t * array, int index);
void * array_add(array_t * array);
bool array_shrink(array_t * array);
void array_free(array_t * array);

#endif
