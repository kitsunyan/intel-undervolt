#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

#define absf(x) ((x) < 0 ? -(x) : (x))
#define rd(c, a, t) (pread(c->fd_msr, &(t), 8, (a)) == 8)
#define wr(c, a, t) (pwrite(c->fd_msr, &(t), 8, (a)) == 8)

static jmp_buf sigsegv_handler_jmp_buf;

static void sigsegv_handler(int sig) {
	siglongjmp(sigsegv_handler_jmp_buf, 1);
}

static bool safe_read_write(u_int64_t * addr, u_int64_t * data, bool write) {
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

#define newline(nl) { \
	if (*nl) { \
		printf("\n"); \
	} \
	*nl = true; \
}

typedef struct {
	config_t * config;
	bool write;
	bool success;
} undervolt_ctx;

static void undervolt_it(uv_list_t * uv, void * data) {
	undervolt_ctx * ctx = data;

	static int mask = 0x800;
	u_int64_t uvint = ((u_int64_t) (mask - absf(uv->value) * 1.024f + 0.5f)
		<< 21) & 0xffffffff;
	u_int64_t rdval = 0x8000001000000000 | ((u_int64_t) uv->index << 40);
	u_int64_t wrval = rdval | 0x100000000 | uvint;

	bool write_success = !ctx->write ||
		wr(ctx->config, MSR_ADDR_VOLTAGE, wrval);
	bool read_success = write_success &&
		wr(ctx->config, MSR_ADDR_VOLTAGE, rdval) &&
		rd(ctx->config, MSR_ADDR_VOLTAGE, rdval);

	const char * errstr = NULL;
	if (!write_success || !read_success) {
		errstr = strerror(errno);
	} else if (ctx->write && (rdval & 0xffffffff) != (wrval & 0xffffffff)) {
		errstr = "Values do not equal";
	}

	if (errstr) {
		ctx->success = false;
		printf("%s (%d): %s\n", uv->title, uv->index, errstr);
	} else {
		float val = ((mask - (rdval >> 21)) & (mask - 1)) / 1.024f;
		printf("%s (%d): -%.02f mV\n", uv->title, uv->index, val);
	}
}

static bool undervolt(config_t * config, bool * nl, bool write) {
	if (config->uv) {
		newline(nl);
		undervolt_ctx ctx;
		ctx.config = config;
		ctx.write = write;
		ctx.success = true;
		uv_list_foreach(config->uv, undervolt_it, &ctx);
		return ctx.success;
	} else {
		return true;
	}
}

static bool tdp(config_t * config, bool * nl, bool write) {
	if (config->tdp_apply) {
		newline(nl);
		void * mem = config->tdp_mem + (MEM_ADDR_TDP & MAP_MASK);
		const char * errstr = NULL;

		u_int64_t msr_limit;
		u_int64_t mchbar_limit;
		if (rd(config, MSR_ADDR_TDP, msr_limit)) {
			if (!safe_read_write(mem, &mchbar_limit, false)) {
				errstr = "Segmentation fault";
			}
		} else {
			errstr = strerror(errno);
		}

		if (errstr) {
			printf("Failed to read TDP values: %s\n", errstr);
		} else {
			if (write) {
				u_int64_t masked = msr_limit & 0xfffff000fffff000;
				u_int64_t short_term = config->tdp_short_term < 0 ? 0 :
					config->tdp_short_term > 511 ? 511 :
					config->tdp_short_term * 8;
				u_int64_t long_term = config->tdp_long_term < 0 ? 0 :
					config->tdp_long_term > 511 ? 511 :
					config->tdp_long_term * 8;
				u_int64_t value = masked | (short_term << 32) | long_term;
				if (wr(config, MSR_ADDR_TDP, value)) {
					msr_limit = value;
					if (!safe_read_write(mem, &value, true)) {
						errstr = "Segmentation fault";
					}
				} else {
					errstr = strerror(errno);
				}
			} else if (msr_limit != mchbar_limit) {
				printf("Warning: MSR and MCHBAR values are not equal\n");
			}

			if (errstr) {
				printf("Failed to write TDP values: %s\n", errstr);
			} else {
				int short_term = ((msr_limit >> 32) & 0xfff) / 8;
				int long_term = (msr_limit & 0xfff) / 8;
				printf("Short term TDP: %d W\n", short_term);
				printf("Long term TDP: %d W\n", long_term);
			}
		}

		return errstr == NULL;
	} else {
		return true;
	}
}

static bool tjoffset(config_t * config, bool * nl, bool write) {
	if (config->tjoffset_apply) {
		newline(nl);
		const char * errstr = NULL;

		if (write) {
			u_int64_t limit;
			if (rd(config, MSR_ADDR_TEMPERATURE, limit)) {
				u_int64_t offset = abs(config->tjoffset);
				offset = offset > 0x3f ? 0x3f : offset;
				limit = (limit & 0xffffffffc0ffffff) | (offset << 24);
				if (!wr(config, MSR_ADDR_TEMPERATURE, limit)) {
					errstr = strerror(errno);
				}
			} else {
				errstr = strerror(errno);
			}
		}

		if (errstr) {
			printf("Failed to write temperature offset: %s\n", errstr);
		} else {
			u_int64_t limit;
			if (rd(config, MSR_ADDR_TEMPERATURE, limit)) {
				int offset = (limit & 0x3f000000) >> 24;
				printf("Critical offset: -%d°C\n", offset);
			} else {
				printf("Failed to read temperature offset: %s\n", errstr);
			}
		}

		return errstr == NULL;
	} else {
		return true;
	}
}

static bool read_apply(bool write) {
	config_t * config = load_config(NULL);
	if (config) {
		bool nl = false;
		bool success = true;
		success &= undervolt(config, &nl, write);
		success &= tdp(config, &nl, write);
		success &= tjoffset(config, &nl, write);
		free_config(config);
		return success;
	} else {
		fprintf(stderr, "Failed to setup the program\n");
		return false;
	}
}

int main(int argc, char ** argv) {
	bool write;
	if (argc == 2 && !strcmp(argv[1], "read")) {
		return read_apply(false) ? 0 : 1;
	} else if (argc == 2 && !strcmp(argv[1], "apply")) {
		return read_apply(true) ? 0 : 1;
	} else {
		fprintf(stderr,
			"Usage: intel-undervolt COMMAND\n"
			"  read      Read and display current values\n"
			"  apply     Apply values from config file\n");
		return argc == 1 ? 0 : 1;
	}
}
