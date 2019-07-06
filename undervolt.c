#include "undervolt.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef IS_FREEBSD
#include <sys/cpuctl.h>
#include <sys/ioccom.h>
#endif
#include <unistd.h>

#define absf(x) ((x) < 0 ? -(x) : (x))

#ifdef IS_FREEBSD

static inline bool cpuctl_rd(int fd, int a, uint64_t * t) {
	cpuctl_msr_args_t args;
	args.msr = a;
	if (ioctl(fd, CPUCTL_RDMSR, &args) == -1) {
		return false;
	}
	*t = args.data;
	return true;
}

static inline bool cpuctl_wr(int fd, int a, uint64_t * t) {
	cpuctl_msr_args_t args;
	args.msr = a;
	args.data = *t;
	return ioctl(fd, CPUCTL_WRMSR, &args) != -1;
}

#define rd(c, a, t) (cpuctl_rd(c->fd_msr, (a), &(t)))
#define wr(c, a, t) (cpuctl_wr(c->fd_msr, (a), &(t)))

#else

#define rd(c, a, t) (pread(c->fd_msr, &(t), 8, (a)) == 8)
#define wr(c, a, t) (pwrite(c->fd_msr, &(t), 8, (a)) == 8)

#endif

bool undervolt(struct config_t * config, bool * nl, bool write) {
	bool success = true;
	bool nll = false;
	int i;

	for (i = 0; config->undervolts && i < config->undervolts->count; i++) {
		struct undervolt_t * undervolt = array_get(config->undervolts, i);

		static const int mask = 0x800;
		uint64_t uvint = ((uint64_t) (mask - absf(undervolt->value) * 1.024f +
			0.5f) << 21) & 0xffffffff;
		uint64_t rdval = 0x8000001000000000 |
			((uint64_t) undervolt->index << 40);
		uint64_t wrval = rdval | 0x100000000 | uvint;

		bool write_success = !write ||
			wr(config, MSR_ADDR_VOLTAGE, wrval);
		bool read_success = write_success &&
			wr(config, MSR_ADDR_VOLTAGE, rdval) &&
			rd(config, MSR_ADDR_VOLTAGE, rdval);

		const char * errstr = NULL;
		if (!write_success || !read_success) {
			errstr = strerror(errno);
		} else if (write && (rdval & 0xffffffff) != (wrval & 0xffffffff)) {
			errstr = "Values do not equal";
		}

		NEW_LINE(nl, nll);
		if (errstr) {
			success = false;
			printf("%s (%d): %s\n", undervolt->title, undervolt->index, errstr);
		} else if (nl) {
			float val = ((mask - (rdval >> 21)) & (mask - 1)) / 1.024f;
			printf("%s (%d): -%.02f mV\n", undervolt->title,
				undervolt->index, val);
		}
	}

	return success;
}

static float power_to_seconds(int value, int time_unit) {
	float multiplier = 1 + ((value >> 6) & 0x3) / 4.f;
	int exponent = (value >> 1) & 0x1f;
	return exp2f(exponent) * multiplier / time_unit;
}

static int power_from_seconds(float seconds, int time_unit) {
	if (log2f(seconds * time_unit / 1.75f) >= 0x1f) {
		return 0xfe;
	} else {
		int i;
		float last_diff = 1.f;
		int last_result = 0;
		for (i = 0; i < 4; i++) {
			float multiplier = 1 + (i / 4.f);
			float value = seconds * time_unit / multiplier;
			float exponent = log2f(value);
			int exponent_int = (int) exponent;
			float diff = exponent - exponent_int;
			if (exponent_int < 0x19 && diff > 0.5f) {
				exponent_int++;
				diff = 1.f - diff;
			}
			if (exponent_int < 0x20) {
				if (diff < last_diff) {
					last_diff = diff;
					last_result = (i << 6) | (exponent_int << 1);
				}
			}
		}
		return last_result;
	}
}

bool power_limit(struct config_t * config, int index, bool * nl, bool write) {
	bool nll = false;
	struct power_limit_t * power = &config->power[index];
	struct power_domain_t * domain = &power_domains[index];
	if (power->apply) {
		void * mem = NULL;
		if (power->mem != NULL) {
			mem = power->mem + (domain->mem_addr & MAP_MASK);
		}
		const char * errstr = NULL;

		uint64_t msr_limit;
		uint64_t mem_limit;
		uint64_t units;
		if (domain->msr_addr == 0 || rd(config, domain->msr_addr, msr_limit)) {
			if (domain->mem_addr == 0 ||
				safe_rw(mem, &mem_limit, false)) {
				if (!rd(config, MSR_ADDR_UNITS, units)) {
					errstr = strerror(errno);
				}
			} else {
				errstr = "Segmentation fault";
			}
		} else {
			errstr = strerror(errno);
		}

		if (!errstr) {
			if (domain->msr_addr == 0) {
				msr_limit = mem_limit;
			} else if (domain->mem_addr == 0) {
				mem_limit = msr_limit;
			}
			if (domain->msr_addr == 0 && domain->mem_addr == 0) {
				errstr = "No method available";
			}
		}

		if (errstr) {
			NEW_LINE(nl, nll);
			printf("Failed to read %s power values: %s\n",
				domain->name, errstr);
		} else {
			int power_unit = (int) (exp2f(units & 0xf) + 0.5f);
			int time_unit = (int) (exp2f((units >> 16) & 0xf) + 0.5f);

			if (write) {
				int max_power = 0x7fff / power_unit;
				uint64_t masked = msr_limit & 0xffff0000ffff0000;
				uint64_t short_term = power->short_term.power < 0 ? 0 :
					power->short_term.power > max_power ? max_power :
					power->short_term.power * power_unit;
				uint64_t long_term = power->long_term.power < 0 ? 0 :
					power->long_term.power > max_power ? max_power :
					power->long_term.power * power_unit;
				uint64_t value = masked | (short_term << 32) | long_term;
				uint64_t time;
				if (power->short_term.time_window > 0) {
					masked = value & 0xff01ffffffffffff;
					time = power_from_seconds(power->short_term.time_window,
						time_unit);
					value = masked | (time << 48);
				}
				if (power->long_term.time_window > 0) {
					masked = value & 0xffffffffff01ffff;
					time = power_from_seconds(power->long_term.time_window,
						time_unit);
					value = masked | (time << 16);
				}
				value |= (power->short_term.enabled ? 1L << 47 : 0) |
					(power->long_term.enabled ? 1L << 15 : 0);
				if (domain->msr_addr == 0 ||
					wr(config, domain->msr_addr, value)) {
					if (domain->mem_addr == 0 ||
						safe_rw(mem, &value, true)) {
						msr_limit = value;
						mem_limit = value;
					} else {
						errstr = "Segmentation fault";
					}
				} else {
					errstr = strerror(errno);
				}
			} else if (msr_limit != mem_limit) {
				NEW_LINE(nl, nll);
				printf("Warning: MSR and memory values are not equal\n");
			}

			NEW_LINE(nl, nll);
			if (errstr) {
				printf("Failed to write %s power values: %s\n",
					domain->name, errstr);
			} else if (nl) {
				if ((msr_limit >> 63) & 0x1) {
					printf("Warning: %s power limit is locked\n", domain->name);
				}
				int short_term = ((msr_limit >> 32) & 0x7fff) / power_unit;
				int long_term = (msr_limit & 0x7fff) / power_unit;
				bool short_term_enabled = !!((msr_limit >> 47) & 1);
				bool long_term_enabled = !!((msr_limit >> 15) & 1);
				float short_term_window = power_to_seconds(msr_limit >> 48,
					time_unit);
				float long_term_window = power_to_seconds(msr_limit >> 16,
					time_unit);
				printf("Short term %s power: %d W, %.03f s, %s\n",
					domain->name, short_term, short_term_window,
					(short_term_enabled ? "enabled" : "disabled"));
				printf("Long term %s power: %d W, %.03f s, %s\n",
					domain->name, long_term, long_term_window,
					(long_term_enabled ? "enabled" : "disabled"));
			}
		}

		return errstr == NULL;
	} else {
		return true;
	}
}

bool tjoffset(struct config_t * config, bool * nl, bool write) {
	bool nll = false;
	if (config->tjoffset_apply) {
		const char * errstr = NULL;

		if (write) {
			uint64_t limit;
			if (rd(config, MSR_ADDR_TEMPERATURE, limit)) {
				uint64_t offset = absf(config->tjoffset);
				offset = offset > 0x3f ? 0x3f : offset;
				limit = (limit & 0xffffffffc0ffffff) | (offset << 24);
				if (!wr(config, MSR_ADDR_TEMPERATURE, limit)) {
					errstr = strerror(errno);
				}
			} else {
				errstr = strerror(errno);
			}
		}

		NEW_LINE(nl, nll);
		if (errstr) {
			printf("Failed to write temperature offset: %s\n", errstr);
		} else if (nl) {
			uint64_t limit;
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
