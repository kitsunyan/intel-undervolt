#include "util.h"

#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf sigsegv_handler_jmp_buf;

static void sigsegv_handler(UNUSED int sig) {
	siglongjmp(sigsegv_handler_jmp_buf, 1);
}

bool safe_rw(uint64_t * addr, uint64_t * data, bool write) {
	struct sigaction oldact;
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = sigsegv_handler;
	sigaction(SIGSEGV, &act, &oldact);
	bool success = false;
	if (sigsetjmp(sigsegv_handler_jmp_buf, 1) == 0) {
		if (write) {
			*addr = *data;
		} else {
			*data = *addr;
		}
		success = true;
	}
	sigaction(SIGSEGV, &oldact, &act);
	return success;
}

typedef struct {
	array_t parent;
	int item_size;
	int capacity;
	void (* item_free)(void *);
	void * data;
} array_full_t;

array_t * array_new(int item_size, void (* item_free)(void *)) {
	array_full_t * full = malloc(sizeof(array_full_t));
	if (!full) {
		return NULL;
	}
	full->parent.count = 0;
	full->item_size = item_size;
	full->capacity = 0;
	full->item_free = item_free;
	full->data = NULL;
	return &full->parent;
}

void * array_get(array_t * array, int index) {
	array_full_t * full = (array_full_t *) array;
	return full->data + index * full->item_size;
}

void * array_add(array_t * array) {
	array_full_t * full = (array_full_t *) array;
	if (full->parent.count >= full->capacity) {
		int capacity = full->capacity > 0 ? 2 * full->capacity : 2;
		void * data = realloc(full->data, capacity * full->item_size);
		if (!data) {
			return NULL;
		}
		full->capacity = capacity;
		full->data = data;
	}
	return full->data + (full->parent.count++) * full->item_size;
}

bool array_shrink(array_t * array) {
	array_full_t * full = (array_full_t *) array;
	if (full->parent.count > full->capacity) {
		void * data = realloc(full->data, full->parent.count * full->item_size);
		if (!data) {
			return false;
		}
		full->capacity = full->parent.count;
		full->data = data;
	}
	return true;
}

void array_free(array_t * array) {
	array_full_t * full = (array_full_t *) array;
	if (full->data) {
		if (full->item_free) {
			int i = 0;
			for (i = 0; i < full->parent.count; i++) {
				full->item_free(full->data + i * full->item_size);
			}
		}
		free(full->data);
	}
	free(full);
}
