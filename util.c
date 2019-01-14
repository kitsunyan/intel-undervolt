#include "util.h"

#include <setjmp.h>
#include <signal.h>
#include <string.h>

static jmp_buf sigsegv_handler_jmp_buf;

static void sigsegv_handler(int sig) {
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
