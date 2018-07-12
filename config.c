#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"

void uv_list_foreach(uv_list_t * uv,
	void (* callback)(uv_list_t *, void *), void * data) {
	if (uv) {
		uv_list_foreach(uv->next, callback, data);
		callback(uv, data);
	}
}

static void uv_list_free_item(uv_list_t * uv, void * data) {
	free(uv->title);
	free(uv);
}

static bool is_module_loaded(const char * name) {
	FILE * file = fopen("/proc/modules", "r");
	if (file) {
		char * line = NULL;
		ssize_t linen = 0;
		int len = strlen(name);
		while ((getline(&line, &linen, file)) >= 0) {
			if (strstr(line, name) == line && line[len] == ' ') {
				free(line);
				fclose(file);
				return true;
			}
		}
		if (line) {
			free(line);
		}
		fclose(file);
		return false;
	} else {
		perror("Fopen failed");
		return true;
	}
}

void free_config(config_t * config) {
	if (config) {
		uv_list_foreach(config->uv, uv_list_free_item, NULL);
		if (config->fd_msr >= 0) {
			close(config->fd_msr);
		}
		if (config->tdp_mem) {
			munmap(config->tdp_mem, MAP_SIZE);
		}
		if (config->fd_mem >= 0) {
			close(config->fd_mem);
		}
		free(config);
	}
}

config_t * load_config(config_t * old_config) {
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
	config->tdp_apply = false;
	config->tjoffset_apply = false;
	config->interval = -1;

	int fd[2];
	pipe(fd);
	int pid = fork();
	if (pid < 0) {
		perror("Fork failed");
		free_config(config);
		config = NULL;
	} else if (pid == 0) {
		close(1);
		dup(fd[1]);
		close(fd[0]);
		close(fd[1]);
		execlp("/bin/bash", "/bin/bash", "-c",
			"function apply { printf '%s\\0' apply \"$1\" \"$2\" \"$3\"; };"
			"function tdp { printf '%s\\0' tdp \"$1\" \"$2\"; };"
			"function tjoffset { printf '%s\\0' tjoffset \"$1\"; };"
			"function interval { printf '%s\\0' interval \"$1\"; };"
			"source " SYSCONFDIR "/intel-undervolt.conf",
			NULL);
		perror("Exec failed");
		exit(1);
	} else {
		close(fd[1]);
		FILE * file = fdopen(fd[0], "r");
		char * line = NULL;
		ssize_t linen = 0;
		bool error = false;
		char * tmp = NULL;

		#define iuv_read_line() (getdelim(&line, &linen, '\0', file) >= 0)

		#define iuv_print_break(...) { \
			error = true; \
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
			if (!strcmp(line, "apply")) {
				iuv_read_line_error();
				tmp = NULL;
				int index = (int) strtol(line, &tmp, 10);
				if (!line[0] || tmp && tmp[0]) {
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
				if (!line[0] || tmp && tmp[0]) {
					free(title);
					iuv_print_break("Invalid value: %s\n", line);
				}
				uv_list_t * uv = malloc(sizeof(uv_list_t));
				uv->next = config->uv;
				uv->index = index;
				uv->title = title;
				uv->value = value;
				config->uv = uv;
			} else if (!strcmp(line, "tdp")) {
				iuv_read_line_error();
				tmp = NULL;
				int tdp_short_term = (int) strtol(line, &tmp, 10);
				float tdp_short_time_window = -1;
				if (tmp && tmp[0] == '/' && tmp[1]) {
					tdp_short_time_window = strtof(&tmp[1], &tmp);
				}
				if (!line[0] || tmp && tmp[0]) {
					iuv_print_break("Invalid TDP: %s\n", line);
				}
				config->tdp_short_term = tdp_short_term;
				config->tdp_short_time_window = tdp_short_time_window;
				iuv_read_line_error();
				tmp = NULL;
				int tdp_long_term = (int) strtol(line, &tmp, 10);
				float tdp_long_time_window = -1;
				if (tmp && tmp[0] == '/' && tmp[1]) {
					tdp_long_time_window = strtof(&tmp[1], &tmp);
				}
				if (!line[0] || tmp && tmp[0]) {
					iuv_print_break("Invalid TDP: %s\n", line);
				}
				config->tdp_long_term = tdp_long_term;
				config->tdp_long_time_window = tdp_long_time_window;
				config->tdp_apply = true;
			} else if (!strcmp(line, "tjoffset")) {
				iuv_read_line_error();
				tmp = NULL;
				int tjoffset = (int) strtol(line, &tmp, 10);
				if (!line[0] || tmp && tmp[0]) {
					iuv_print_break("Invalid tjoffset: %s\n", line);
				}
				config->tjoffset = tjoffset;
				config->tjoffset_apply = true;
			} else if (!strcmp(line, "interval")) {
				iuv_read_line_error();
				tmp = NULL;
				int interval = (int) strtol(line, &tmp, 10);
				if (!line[0] || tmp && tmp[0]) {
					iuv_print_break("Invalid interval: %s\n", line);
				}
				config->interval = interval;
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
			fprintf(stderr, "Failed to read configuration\n");
			error = true;
		}

		if (!error) {
			if (config->uv || config->tdp_apply || config->tjoffset_apply) {
				if (!is_module_loaded("msr")) {
					int pid = fork();
					if (pid < 0) {
						perror("Fork failed");
						error = true;
					} else if (pid == 0) {
						execlp("/sbin/modprobe",
							"/sbin/modprobe", "msr", NULL);
						perror("Exec failed");
						exit(1);
					} else {
						waitpid(pid, &status, 0);
						if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
							!is_module_loaded("msr")) {
							fprintf(stderr, "Modprobe failed\n");
							error = 1;
						}
					}
				}

				if (!error && config->fd_msr < 0) {
					int fd = open("/dev/cpu/0/msr", O_RDWR | O_SYNC);
					if (fd >= 0) {
						config->fd_msr = fd;
					} else {
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
			if (config->tdp_apply) {
				if (config->fd_mem < 0) {
					int fd = open("/dev/mem", O_RDWR | O_SYNC);
					if (fd >= 0) {
						void * base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE,
							MAP_SHARED, fd, MEM_ADDR_TDP & ~MAP_MASK);
						if (!base || base == MAP_FAILED) {
							close(fd);
							perror("Mmap failed\n");
							error = true;
						} else {
							config->fd_mem = fd;
							config->tdp_mem = base;
						}
					} else {
						perror("Failed to open memory device\n");
						error = true;
					}
				}
			} else if (config->fd_mem >= 0) {
				munmap(config->tdp_mem, MAP_SIZE);
				config->tdp_mem = NULL;
				close(config->fd_mem);
				config->fd_mem = -1;
			}
		}

		if (error) {
			free_config(config);
			config = NULL;
		}
	}

	return config;
}
