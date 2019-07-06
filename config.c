#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void undervolt_free(void * pointer) {
	struct undervolt_t * undervolt = pointer;
	free(undervolt->title);
}

static void hwp_power_term_free(void * pointer) {
	struct hwp_power_term_t * hwp_power_term = pointer;
	free(hwp_power_term->domain);
}

static void hwp_hint_free(void * pointer) {
	struct hwp_hint_t * hwp_hint = pointer;
	if (hwp_hint->hwp_power_terms) {
		array_free(hwp_hint->hwp_power_terms);
	}
	free(hwp_hint->load_hint);
	free(hwp_hint->normal_hint);
}

void free_config(struct config_t * config) {
	if (config) {
		unsigned int i;
		if (config->undervolts) {
			array_free(config->undervolts);
		}
		if (config->hwp_hints) {
			array_free(config->hwp_hints);
		}
		if (config->fd_msr >= 0) {
			close(config->fd_msr);
		}
		for (i = 0; i < ARRAY_SIZE(config->power); i++) {
			if (config->power[i].mem) {
				munmap(config->power[i].mem, MAP_SIZE);
			}
		}
		if (config->fd_mem >= 0) {
			close(config->fd_mem);
		}
		free(config);
	}
}

static bool parse_power_limit_value(const char * line,
	struct power_limit_value_t * value) {
	char * tmp = NULL;
	int power = (int) strtol(line, &tmp, 10);
	float time_window = -1;
	bool enabled = true;
	if (tmp && tmp[0] == '/' && tmp[1]) {
		time_window = strtof(&tmp[1], &tmp);
		if (tmp && tmp[0] && tmp[0] != ':') {
			return false;
		}
	}
	while (tmp && tmp[0] == ':' && tmp[1]) {
		char * next = NULL;
		int n;
		tmp = &tmp[1];
		next = strstr(tmp, ":");
		n = next ? (int) (next - tmp) : (int) strlen(tmp);
		if (strn_eq_const(tmp, "enabled", n)) {
			enabled = true;
		} else if (strn_eq_const(tmp, "disabled", n)) {
			enabled = false;
		} else {
			return false;
		}
		tmp = next ? next : NULL;
	}
	if (!line[0] || (tmp && tmp[0])) {
		return false;
	}
	value->power = power;
	value->time_window = time_window;
	value->enabled = enabled;
	return true;
}

static bool parse_hwp_load(const char * line, bool * multi, float * threshold,
	bool * nl, bool * nll) {
	int args = 0;
	bool error = false;
	bool result_multi;
	float result_threshold;

	while (line) {
		int len;
		char * tmp = strstr(line, ":");
		if (tmp) {
			len = (int) (tmp - line);
		} else {
			len = strlen(line);
		}

		if (args == 0) {
			if (strn_eq_const(line, "single", len)) {
				result_multi = false;
			} else if (strn_eq_const(line, "multi", len)) {
				result_multi = true;
			} else {
				NEW_LINE(nl, *nll);
				fprintf(stderr, "Invalid capture: %.*s\n", len, line);
				error = true;
				break;
			}
		} else if (args == 1) {
			result_threshold = strtof(line, &tmp);
			if (tmp) {
				int tmp_len = (int) (tmp - line);
				if (tmp_len != len) {
					NEW_LINE(nl, *nll);
					fprintf(stderr, "Invalid threshold: %.*s\n", len, line);
					error = true;
					break;
				}
			}
		}

		line = line[len] == ':' ? &line[len + 1] : NULL;
		args++;
	}

	if (!error && args != 2) {
		NEW_LINE(nl, *nll);
		fprintf(stderr, "Wrong number of arguments for 'load' algorithm\n");
		error = true;
	}

	if (error) {
		return false;
	} else {
		*multi = result_multi;
		*threshold = result_threshold;
		return true;
	}
}

static bool parse_hwp_power(const char * line, struct array_t ** hwp_power_terms,
	bool * nl, bool * nll) {
	int args = 1;
	bool error = false;
	bool and = false;
	char * domain = NULL;
	bool greater;
	float power;
	struct array_t * result = NULL;

	while (line) {
		int len;
		char * tmp = strstr(line, ":");
		if (tmp) {
			len = (int) (tmp - line);
		} else {
			len = strlen(line);
		}

		if (args % 4 == 0) {
			if (strn_eq_const(line, "and", len)) {
				and = true;
			} else if (strn_eq_const(line, "or", len)) {
				and = false;
			} else {
				NEW_LINE(nl, *nll);
				fprintf(stderr, "Invalid operator: %.*s\n", len, line);
				error = true;
				break;
			}
		} else if (args % 4 == 1) {
			domain = malloc(len + 1);
			if (!domain) {
				NEW_LINE(nl, *nll);
				fprintf(stderr, "No enough memory\n");
				error = true;
				break;
			}
			memcpy(domain, line, len);
			domain[len] = '\0';
		} else if (args % 4 == 2) {
			if (strn_eq_const(line, "gt", len)) {
				greater = true;
			} else if (strn_eq_const(line, "lt", len)) {
				greater = false;
			} else {
				NEW_LINE(nl, *nll);
				fprintf(stderr, "Invalid operator: %.*s\n", len, line);
				error = true;
				break;
			}
		} else if (args % 4 == 3) {
			struct hwp_power_term_t * hwp_power_term;
			power = strtof(line, &tmp);
			if (tmp) {
				int tmp_len = (int) (tmp - line);
				if (tmp_len != len) {
					NEW_LINE(nl, *nll);
					fprintf(stderr, "Invalid power: %.*s\n", len, line);
					error = true;
					break;
				}
			}
			if (!result) {
				result = array_new(sizeof(struct hwp_power_term_t),
					hwp_power_term_free);
			}
			hwp_power_term = result ? array_add(result) : NULL;
			if (!hwp_power_term) {
				NEW_LINE(nl, *nll);
				fprintf(stderr, "No enough memory\n");
				error = true;
				break;
			}
			hwp_power_term->and = and;
			hwp_power_term->domain = domain;
			hwp_power_term->greater = greater;
			hwp_power_term->power = power;
			domain = NULL;
		}

		line = line[len] == ':' ? &line[len + 1] : NULL;
		args++;
	}

	if (!error && args % 4 != 0) {
		NEW_LINE(nl, *nll);
		fprintf(stderr, "Wrong number of arguments for 'power' algorithm\n");
		error = true;
	}

	if (error) {
		if (domain) {
			free(domain);
		}
		if (result) {
			array_free(result);
		}
		return false;
	} else {
		if (result) {
			array_shrink(result);
		}
		*hwp_power_terms = result;
		return true;
	}
}

static bool contains_hint_or_append(const char ** hints, int count,
	const char * hint) {
	int i;
	for (i = 0; i < count; i++) {
		if (!strcmp(hints[i], hint)) {
			return true;
		}
	}
	hints[i] = hint;
	return false;
}

static bool validate_hwp_hint(struct array_t * hwp_hints, bool * nl, bool * nll) {
	int i;
	int force_count = 0;
	const char * hints[2 * hwp_hints->count];

	for (i = 0; i < hwp_hints->count; i++) {
		struct hwp_hint_t * hwp_hint = array_get(hwp_hints, i);
		if (contains_hint_or_append(hints, 2 * i, hwp_hint->load_hint) ||
			contains_hint_or_append(hints, 2 * i + 1, hwp_hint->normal_hint)) {
			NEW_LINE(nl, *nll);
			fprintf(stderr, "Same HWP hint can not be used multiple times\n");
			return false;
		}
		if (hwp_hint->force) {
			force_count++;
		}
	}

	if (force_count > 1) {
		NEW_LINE(nl, *nll);
		fprintf(stderr, "Only single 'force' rule is allowed\n");
		return false;
	}
	if (force_count == 1 && hwp_hints->count > 1) {
		NEW_LINE(nl, *nll);
		fprintf(stderr, "'switch' rules are not allowed when 'force' "
			"rule is used\n");
		return false;
	}

	return true;
}

struct config_t * load_config(struct config_t * old_config, bool * nl) {
	unsigned int i;
	bool nll = false;
	struct config_t * config;
	if (old_config) {
		config = old_config;
		if (config->undervolts) {
			array_free(config->undervolts);
		}
		if (config->hwp_hints) {
			array_free(config->hwp_hints);
		}
		if (config->daemon_actions) {
			array_free(config->daemon_actions);
		}
	} else {
		config = malloc(sizeof(struct config_t));
		if (!config) {
			NEW_LINE(nl, nll);
			perror("No enough memory");
			return NULL;
		}
		config->fd_msr = -1;
		config->fd_mem = -1;
	}
	config->enable = false;
	config->undervolts = NULL;
	for (i = 0; i < ARRAY_SIZE(config->power); i++) {
		config->power[i].apply = false;
	}
	config->tjoffset_apply = false;
	config->hwp_hints = NULL;
	config->interval = -1;
	config->daemon_actions = NULL;

	int fd[2];
	pipe(fd);
	int pid = fork();
	if (pid < 0) {
		NEW_LINE(nl, nll);
		perror("Fork failed");
		free_config(config);
		config = NULL;
	} else if (pid == 0) {
		close(fd[0]);
		char fdarg[20];
		sprintf(fdarg, "%d", fd[1]);
		execlp("/bin/sh", "/bin/sh", "-c", "readonly fd=$1;"
			"pz() { printf '%s\\0' \"$@\" >&$fd; };"
			"enable() { pz enable \"$1\"; };"
			"apply() { pz apply; pz undervolt \"$1\" \"$2\" \"$3\"; };"
			"undervolt() { pz undervolt \"$1\" \"$2\" \"$3\"; };"
			"tdp() { pz tdp; pz power package \"$1\" \"$2\"; };"
			"power() { pz power \"$1\" \"$2\" \"$3\"; };"
			"tjoffset() { pz tjoffset \"$1\"; };"
			"hwphint() { pz hwphint \"$1\" \"$2\" \"$3\" \"$4\"; };"
			"interval() { pz interval \"$1\"; };"
			"daemon() { pz daemon \"$1\"; };"
			". " SYSCONFDIR "/intel-undervolt.conf",
			"sh", fdarg, NULL);
		exit(1);
	} else {
		close(fd[1]);
		FILE * file = fdopen(fd[0], "r");
		char * line = NULL;
		size_t linen = 0;
		bool error = false;
		char * tmp = NULL;
		bool apply_deprecation = false;
		bool tdp_deprecation = false;
		int status;

		#define iuv_read_line() (getdelim(&line, &linen, '\0', file) >= 0)

		#define iuv_print_break(...) { \
			error = true; \
			NEW_LINE(nl, nll); \
			fprintf(stderr, __VA_ARGS__); \
			break; \
		}

		#define iuv_print_break_nomem() iuv_print_break("No enough memory\n")

		#define iuv_read_line_error_action(on_error) { \
			if (!iuv_read_line()) { \
				on_error; \
				iuv_print_break("Configuration error\n"); \
			} \
		}

		#define iuv_read_line_error() iuv_read_line_error_action({})

		while (iuv_read_line()) {
			if (!strcmp(line, "enable")) {
				bool enable;
				iuv_read_line_error();
				if (!strcmp(line, "yes")) {
					enable = true;
				} else if (!strcmp(line, "no")) {
					enable = false;
				} else {
					iuv_print_break("Invalid value: %s\n", line);
				}
				config->enable = enable;
			} else if (!strcmp(line, "undervolt")) {
				int index;
				int len;
				char * title;
				float value;
				struct undervolt_t * undervolt;
				iuv_read_line_error();
				tmp = NULL;
				index = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid index: %s\n", line);
				}
				iuv_read_line_error();
				len = strlen(line);
				title = malloc(len + 1);
				if (!title) {
					iuv_print_break_nomem();
				}
				memcpy(title, line, len + 1);
				iuv_read_line_error_action({
					free(title);
				});
				tmp = NULL;
				value = strtof(line, &tmp);
				if (!line[0] || (tmp && tmp[0])) {
					free(title);
					iuv_print_break("Invalid value: %s\n", line);
				}
				if (!config->undervolts) {
					config->undervolts = array_new(sizeof(struct undervolt_t),
						undervolt_free);
					if (!config->undervolts) {
						free(title);
						iuv_print_break_nomem();
					}
				}
				undervolt = array_add(config->undervolts);
				if (!undervolt) {
					free(title);
					iuv_print_break_nomem();
				}
				undervolt->index = index;
				undervolt->title = title;
				undervolt->value = value;
			} else if (!strcmp(line, "power")) {
				int index = -1;
				iuv_read_line_error();
				for (i = 0; i < ARRAY_SIZE(power_domains); i++) {
					if (!strcmp(line, power_domains[i].name)) {
						index = i;
						break;
					}
				}
				if (index < 0) {
					iuv_print_break("Invalid domain: %s\n", line);
				}
				iuv_read_line_error();
				if (!parse_power_limit_value(line,
					&config->power[index].short_term)) {
					iuv_print_break("Invalid power value: %s\n", line);
				}
				iuv_read_line_error();
				if (!parse_power_limit_value(line,
					&config->power[index].long_term)) {
					iuv_print_break("Invalid power value: %s\n", line);
				}
				config->power[index].apply = true;
			} else if (!strcmp(line, "tjoffset")) {
				int tjoffset;
				iuv_read_line_error();
				tmp = NULL;
				tjoffset = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid tjoffset: %s\n", line);
				}
				config->tjoffset = tjoffset;
				config->tjoffset_apply = true;
			} else if (!strcmp(line, "hwphint")) {
				bool force = false;
				int len;
				bool load = false;
				bool load_multi;
				float load_threshold;
				bool power = false;
				struct array_t * hwp_power_terms = NULL;
				char * load_hint;
				char * normal_hint;
				struct hwp_hint_t * hwp_hint;
				iuv_read_line_error();
				if (!strcmp(line, "force")) {
					force = true;
				} else if (strcmp(line, "switch")) {
					iuv_print_break("Invalid mode: %s\n", line);
				}
				iuv_read_line_error();
				tmp = strstr(line, ":");
				if (tmp) {
					len = (int) (tmp - line);
					line[len] = '\0';
					tmp = &line[len + 1];
				}
				if (!strcmp(line, "load")) {
					load = true;
					if (!parse_hwp_load(tmp, &load_multi, &load_threshold,
						nl, &nll)) {
						error = true;
						break;
					}
				} else if (!strcmp(line, "power")) {
					power = true;
					if (!parse_hwp_power(tmp, &hwp_power_terms, nl, &nll)) {
						error = true;
						break;
					}
				} else {
					iuv_print_break("Invalid algorithm: %s\n", line);
				}
				iuv_read_line_error();
				len = strlen(line);
				load_hint = malloc(len + 1);
				if (!load_hint) {
					iuv_print_break_nomem();
				}
				memcpy(load_hint, line, len + 1);
				iuv_read_line_error_action({
					free(load_hint);
				});
				len = strlen(line);
				normal_hint = malloc(len + 1);
				if (!normal_hint) {
					free(load_hint);
					iuv_print_break_nomem();
				}
				memcpy(normal_hint, line, len + 1);
				if (!config->hwp_hints) {
					config->hwp_hints = array_new(sizeof(struct hwp_hint_t),
						hwp_hint_free);
					if (!config->hwp_hints) {
						free(load_hint);
						free(normal_hint);
						iuv_print_break_nomem();
					}
				}
				hwp_hint = array_add(config->hwp_hints);
				if (!hwp_hint) {
					free(load_hint);
					free(normal_hint);
					iuv_print_break_nomem();
				}
				hwp_hint->force = force;
				hwp_hint->load = load;
				hwp_hint->load_multi = load_multi;
				hwp_hint->load_threshold = load_threshold;
				hwp_hint->power = power;
				hwp_hint->hwp_power_terms = hwp_power_terms;
				hwp_hint->load_hint = load_hint;
				hwp_hint->normal_hint = normal_hint;
			} else if (!strcmp(line, "interval")) {
				int interval;
				iuv_read_line_error();
				tmp = NULL;
				interval = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid interval: %s\n", line);
				}
				config->interval = interval;
			} else if (!strcmp(line, "daemon")) {
				struct daemon_action_t * daemon_action;
				bool once = false;
				bool invalid_option = false;
				enum daemon_action_kind kind;
				char * tmp;
				int n;
				iuv_read_line_error();
				tmp = strstr(line, ":");
				n = tmp ? (int) (tmp - line) : (int) strlen(line);
				if (strn_eq_const(line, "undervolt", n)) {
					kind = DAEMON_ACTION_KIND_UNDERVOLT;
				} else if (strn_eq_const(line, "power", n)) {
					kind = DAEMON_ACTION_KIND_POWER;
				} else if (strn_eq_const(line, "tjoffset", n)) {
					kind = DAEMON_ACTION_KIND_TJOFFSET;
				} else {
					invalid_option = true;
				}
				while (!invalid_option && tmp && tmp[0] == ':' && tmp[1]) {
					char * next = NULL;
					tmp = &tmp[1];
					next = strstr(tmp, ":");
					n = next ? (int) (next - tmp) : (int) strlen(tmp);
					if (strn_eq_const(tmp, "once", n)) {
						once = true;
					} else {
						invalid_option = true;
						break;
					}
					tmp = next ? next : NULL;
				}
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid daemon action: %s\n", line);
				}
				if (!config->daemon_actions) {
					config->daemon_actions = array_new(sizeof(struct daemon_action_t), NULL);
				}
				daemon_action = array_add(config->daemon_actions);
				if (!daemon_action) {
					iuv_print_break_nomem();
				}
				daemon_action->kind = kind;
				daemon_action->once = once;
			} else if (!strcmp(line, "apply")) {
				if (!apply_deprecation) {
					NEW_LINE(nl, nll);
					fprintf(stderr, "Warning: 'apply' option is deprecated, "
						"use 'undervolt' instead\n");
				}
				apply_deprecation = true;
			} else if (!strcmp(line, "tdp")) {
				if (!tdp_deprecation) {
					NEW_LINE(nl, nll);
					fprintf(stderr, "Warning: 'tdp' option is deprecated, "
						"use 'power package' instead\n");
				}
				tdp_deprecation = true;
			} else {
				iuv_print_break("Configuration error\n");
			}
		}

		if (line) {
			free(line);
		}
		fclose(file);
		waitpid(pid, &status, 0);
		if (!error && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
			NEW_LINE(nl, nll);
			fprintf(stderr, "Failed to read configuration\n");
			error = true;
		}

		if (!error && config->hwp_hints &&
			!validate_hwp_hint(config->hwp_hints, nl, &nll)) {
			error = true;
		}

		if (!error) {
			bool need_power_msr = false;
			for (i = 0; i < ARRAY_SIZE(config->power); i++) {
				if (config->power[i].apply && power_domains[i].msr_addr != 0) {
					need_power_msr = true;
					break;
				}
			}

			if (config->undervolts || need_power_msr ||
				config->tjoffset_apply) {
				if (config->fd_msr < 0) {
#ifdef IS_FREEBSD
					char * dev = "/dev/cpuctl0";
#else
					char * dev = "/dev/cpu/0/msr";
#endif
					int fd = open(dev, O_RDWR | O_SYNC);
					if (fd < 0) {
						int pid = fork();
						if (pid < 0) {
							NEW_LINE(nl, nll);
							perror("Fork failed");
						} else if (pid == 0) {
#ifdef IS_FREEBSD
							char * executable = "/sbin/kldload";
							execlp(executable, executable, "cpuctl", NULL);
#else
							char * executable = "/sbin/modprobe";
							execlp(executable, executable, "msr", NULL);
#endif
							exit(1);
						} else {
							waitpid(pid, &status, 0);
							if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
								fd = open(dev, O_RDWR | O_SYNC);
							}
						}
					}
					if (fd >= 0) {
						config->fd_msr = fd;
					} else {
						NEW_LINE(nl, nll);
						perror("Failed to open MSR device");
						error = true;
					}
				}
			} else if (config->fd_msr >= 0) {
				close(config->fd_msr);
				config->fd_msr = -1;
			}
		}

		if (!error) {
			bool need_power_mem = false;
			for (i = 0; i < ARRAY_SIZE(config->power); i++) {
				if (config->power[i].apply && power_domains[i].mem_addr != 0) {
					need_power_mem = true;
					break;
				}
			}

			if (need_power_mem) {
				if (config->fd_mem < 0) {
					int fd = open("/dev/mem", O_RDWR | O_SYNC);
					if (fd >= 0) {
						config->fd_mem = fd;
					} else {
						NEW_LINE(nl, nll);
						perror("Failed to open memory device");
						error = true;
					}
				}
				if (!error) {
					for (i = 0; i < ARRAY_SIZE(config->power); i++) {
						if (config->power[i].apply) {
							size_t mem_addr = power_domains[i].mem_addr;
							if (mem_addr != 0 && !config->power[i].mem) {
								void * base = mmap(0, MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_SHARED,
									config->fd_mem, mem_addr & ~MAP_MASK);
								if (!base || base == MAP_FAILED) {
									NEW_LINE(nl, nll);
									perror("Mmap failed");
									need_power_mem = false;
									error = true;
									break;
								} else {
									config->power[i].mem = base;
								}
							}
						} else if (config->power[i].mem) {
							munmap(config->power[i].mem, MAP_SIZE);
							config->power[i].mem = NULL;
						}
					}
				}
			}

			if (!need_power_mem) {
				for (i = 0; i < ARRAY_SIZE(config->power); i++) {
					if (config->power[i].mem) {
						munmap(config->power[i].mem, MAP_SIZE);
						config->power[i].mem = NULL;
					}
				}
				if (config->fd_mem >= 0) {
					close(config->fd_mem);
					config->fd_mem = -1;
				}
			}
		}

		if (error) {
			free_config(config);
			config = NULL;
		} else {
			if (config->undervolts) {
				array_shrink(config->undervolts);
			}
			if (config->hwp_hints) {
				array_shrink(config->hwp_hints);
			}
			if (config->daemon_actions) {
				array_shrink(config->daemon_actions);
			}
		}
	}

	return config;
}
