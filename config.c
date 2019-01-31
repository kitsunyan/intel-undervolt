#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

void uv_list_foreach(uv_list_t * uv,
	void (* callback)(uv_list_t *, void *), void * data) {
	if (uv) {
		uv_list_foreach(uv->next, callback, data);
		callback(uv, data);
	}
}

static void uv_list_free_item(uv_list_t * uv, UNUSED void * data) {
	free(uv->title);
	free(uv);
}

void free_config(config_t * config) {
	unsigned int i;
	if (config) {
		uv_list_foreach(config->uv, uv_list_free_item, NULL);
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
	power_limit_value_t * value) {
	char * tmp = NULL;
	char * next = NULL;
	int power = (int) strtol(line, &tmp, 10);
	float time_window = -1;
	bool enabled = true;
	int n;
	if (tmp && tmp[0] == '/' && tmp[1]) {
		time_window = strtof(&tmp[1], &tmp);
		if (tmp && tmp[0] && tmp[0] != ':') {
			return false;
		}
	}
	while (tmp && tmp[0] == ':' && tmp[1]) {
		tmp = &tmp[1];
		next = strstr(tmp, ":");
		n = next ? (int) (next - tmp) : (int) strlen(tmp);
		if (!strncmp("enabled", tmp, n)) {
			enabled = true;
		} else if (!strncmp("disabled", tmp, n)) {
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

config_t * load_config(config_t * old_config, bool * nl) {
	unsigned int i;
	bool nll = false;
	config_t * config;
	if (old_config) {
		config = old_config;
		uv_list_foreach(config->uv, uv_list_free_item, NULL);
	} else {
		config = malloc(sizeof(config_t));
		config->fd_msr = -1;
		config->fd_mem = -1;
	}
	config->uv = NULL;
	for (i = 0; i < ARRAY_SIZE(config->power); i++) {
		config->power[i].apply = false;
	}
	config->tjoffset_apply = false;
	config->interval = -1;

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
		execlp("/bin/bash", "/bin/bash", "-c", "readonly fd=$1;"
			"function pz { printf '%s\\0' \"$@\" >&$fd; };"
			"function apply { pz apply; pz undervolt \"$1\" \"$2\" \"$3\"; };"
			"function undervolt { pz undervolt \"$1\" \"$2\" \"$3\"; };"
			"function tdp { pz tdp; pz power package \"$1\" \"$2\"; };"
			"function power { pz power \"$1\" \"$2\" \"$3\"; };"
			"function tjoffset { pz tjoffset \"$1\"; };"
			"function interval { pz interval \"$1\"; };"
			"source " SYSCONFDIR "/intel-undervolt.conf",
			"bash", fdarg, NULL);
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

		#define iuv_read_line() (getdelim(&line, &linen, '\0', file) >= 0)

		#define iuv_print_break(...) { \
			error = true; \
			NEW_LINE(nl, nll); \
			fprintf(stderr, __VA_ARGS__); \
			break; \
		}

		#define iuv_read_line_error_action(on_error) { \
			if (!iuv_read_line()) { \
				on_error; \
				iuv_print_break("Configuration error\n"); \
			} \
		}

		#define iuv_read_line_error() iuv_read_line_error_action({})

		while (iuv_read_line()) {
			if (!strcmp(line, "undervolt")) {
				iuv_read_line_error();
				tmp = NULL;
				int index = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid index: %s\n", line);
				}
				iuv_read_line_error();
				int len = strlen(line);
				char * title = malloc(len + 1);
				memcpy(title, line, len + 1);
				iuv_read_line_error_action({
					free(title);
				});
				tmp = NULL;
				float value = strtof(line, &tmp);
				if (!line[0] || (tmp && tmp[0])) {
					free(title);
					iuv_print_break("Invalid value: %s\n", line);
				}
				uv_list_t * uv = malloc(sizeof(uv_list_t));
				uv->next = config->uv;
				uv->index = index;
				uv->title = title;
				uv->value = value;
				config->uv = uv;
			} else if (!strcmp(line, "power")) {
				iuv_read_line_error();
				int index = -1;
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
				iuv_read_line_error();
				tmp = NULL;
				int tjoffset = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid tjoffset: %s\n", line);
				}
				config->tjoffset = tjoffset;
				config->tjoffset_apply = true;
			} else if (!strcmp(line, "interval")) {
				iuv_read_line_error();
				tmp = NULL;
				int interval = (int) strtol(line, &tmp, 10);
				if (!line[0] || (tmp && tmp[0])) {
					iuv_print_break("Invalid interval: %s\n", line);
				}
				config->interval = interval;
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
		int status;
		waitpid(pid, &status, 0);
		if (!error && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
			NEW_LINE(nl, nll);
			fprintf(stderr, "Failed to read configuration\n");
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

			if (config->uv || need_power_msr || config->tjoffset_apply) {
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
		}
	}

	return config;
}
