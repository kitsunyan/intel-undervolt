#include "config.h"
#include "power.h"
#include "scaling.h"
#include "stat.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DIR_CPUFREQ "/sys/devices/system/cpu/cpufreq"
#define FILE_HINT "energy_performance_preference"
#define BUFSZ 80

struct cpu_policy_full_t {
	int cpu_count;
	struct cpu_stat_t * cpu_stat;
	struct rapl_t * rapl;
};

struct cpu_policy_t * cpu_policy_init() {
	int cpu_count = 0;
	DIR * dir;

	dir = opendir(DIR_CPUFREQ);
	if (dir) {
		struct dirent * entry;
		while ((entry = readdir(dir))) {
			if (strstr(entry->d_name, "policy") == entry->d_name) {
				const char * index_str = &entry->d_name[6];
				int index = atoi(index_str);
				if (index >= 0) {
					index++;
					cpu_count = index > cpu_count ? index : cpu_count;
				}
			}
		}
		closedir(dir);
	}

	if (cpu_count > 0) {
		struct cpu_policy_full_t * full = malloc(sizeof(struct cpu_policy_full_t));
		if (!full) {
			fprintf(stderr, "No enough memory");
			return NULL;
		}
		full->cpu_count = cpu_count;
		full->cpu_stat = cpu_stat_init();
		full->rapl = rapl_init();
		return (struct cpu_policy_t *) full;
	} else {
		return NULL;
	}
}

static bool check_cpu_stat(struct cpu_stat_t * cpu_stat, bool multi, float threshold) {
	return cpu_stat && (multi ? cpu_stat->multi_core
		: cpu_stat->single_core) >= threshold;
}

static int rapl_lookup(struct rapl_t * rapl, const char * domain) {
	int i;
	for (i = 0; i < rapl->devices->count; i++) {
		struct rapl_device_t * rapl_device = array_get(rapl->devices, i);
		const char * tmp = strstr(rapl_device->name, domain);
		if (tmp == rapl_device->name) {
			int len = strlen(domain);
			if (rapl_device->name[len] == '\0' ||
				rapl_device->name[len] == '-') {
				return i;
			}
		}
	}
	return -1;
}

static bool check_rapl(struct rapl_t * rapl, struct array_t * hwp_power_terms) {
	bool result = false;
	if (rapl && rapl->devices && hwp_power_terms) {
		int i;
		for (i = 0; i < hwp_power_terms->count; i++) {
			struct hwp_power_term_t * hwp_power_term = array_get(hwp_power_terms, i);
			int device_index = rapl_lookup(rapl, hwp_power_term->domain);
			float power = 0;
			bool current;
			if (device_index >= 0) {
				struct rapl_device_t * rapl_device = array_get(rapl->devices,
					device_index);
				power = rapl_device->power;
			}
			if (hwp_power_term->greater) {
				current = power > hwp_power_term->power;
			} else {
				current = power < hwp_power_term->power;
			}
			if (hwp_power_term->and) {
				result &= current;
			} else {
				result |= current;
			}
		}
	}
	return result;
}

enum {
	STATUS_UNKNOWN,
	STATUS_NORMAL,
	STATUS_LOAD
};

void cpu_policy_update(struct cpu_policy_t * cpu_policy, struct array_t * hwp_hints) {
	if (cpu_policy) {
		struct cpu_policy_full_t * full = (struct cpu_policy_full_t *) cpu_policy;
		bool handled[full->cpu_count];
		char * current_hints[full->cpu_count];
		bool read_hints = false;
		int cpu_stat_status = STATUS_UNKNOWN;
		int rapl_status = STATUS_UNKNOWN;
		int i;

		memset(handled, 0, full->cpu_count * sizeof(bool));
		memset(current_hints, 0, full->cpu_count * sizeof(char *));

		for (i = 0; hwp_hints && i < hwp_hints->count; i++) {
			struct hwp_hint_t * hwp_hint = array_get(hwp_hints, i);
			int total_handled = 0;
			const char * hint;
			char buf[BUFSZ];
			int j;

			if (!hwp_hint->force && !read_hints) {
				read_hints = true;
				for (j = 0; j < full->cpu_count; j++) {
					int fd;
					sprintf(buf, DIR_CPUFREQ "/policy%d/" FILE_HINT, j);
					fd = open(buf, O_RDONLY);
					if (fd >= 0) {
						int size = read(fd, buf, BUFSZ - 1);
						if (size >= 1) {
							if (buf[size - 1] == '\n') {
								size--;
							}
						} else {
							size = 0;
						}
						buf[size] = '\0';
						current_hints[j] = malloc(size + 1);
						if (current_hints[j] != NULL) {
							memcpy(current_hints[j], buf, size + 1);
						} else {
							handled[j] = true;
							perror("No enough memory");
						}
						close(fd);
					} else {
						handled[j] = true;
						perror("Failed to get hint");
					}
				}
			}

			for (j = 0; j < full->cpu_count; j++) {
				hint = NULL;
				if (!handled[j] && (hwp_hint->force || (current_hints[j] &&
					(!strcmp(current_hints[j], hwp_hint->normal_hint) ||
						!strcmp(current_hints[j], hwp_hint->load_hint))))) {
					bool load = false;
					if (hwp_hint->load) {
						if (cpu_stat_status == STATUS_UNKNOWN) {
							if (full->cpu_stat) {
								cpu_stat_measure(full->cpu_stat);
							}
							cpu_stat_status = check_cpu_stat(full->cpu_stat,
								hwp_hint->load_multi, hwp_hint->load_threshold)
								? STATUS_LOAD : STATUS_NORMAL;
						}
						load = cpu_stat_status == STATUS_LOAD;
					} else if (hwp_hint->power) {
						if (rapl_status == STATUS_UNKNOWN) {
							if (full->rapl) {
								rapl_measure(full->rapl);
							}
							rapl_status = check_rapl(full->rapl,
								hwp_hint->hwp_power_terms)
								? STATUS_LOAD : STATUS_NORMAL;
						}
						load = rapl_status == STATUS_LOAD;
					}
					hint = load ? hwp_hint->load_hint : hwp_hint->normal_hint;
				}

				if (hint && (hwp_hint->force || (current_hints[j] &&
					strcmp(current_hints[j], hint)))) {
					int fd;
					sprintf(buf, DIR_CPUFREQ "/policy%d/" FILE_HINT, j);
					fd = open(buf, O_WRONLY);
					if (fd >= 0) {
						if (write(fd, hint, strlen(hint)) < 0) {
							perror("Failed to set hint");
						}
						close(fd);
					} else {
						perror("Failed to set hint");
					}

					handled[j] = true;
				}

				if (handled[j]) {
					total_handled++;
				}
			}

			if (total_handled == full->cpu_count) {
				break;
			}
		}

		for (i = 0; i < full->cpu_count; i++) {
			if (current_hints[i]) {
				free(current_hints[i]);
			}
		}
	}
}

void cpu_policy_free(struct cpu_policy_t * cpu_policy) {
	if (cpu_policy) {
		struct cpu_policy_full_t * full = (struct cpu_policy_full_t *) cpu_policy;
		if (full->cpu_stat) {
			cpu_stat_free(full->cpu_stat);
		}
		if (full->rapl) {
			rapl_free(full->rapl);
		}
		free(full);
	}
}
