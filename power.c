#include "power.h"
#include "util.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DIR_POWERCAP "/sys/class/powercap"
#define BUFSZ 80

struct rapl_device_ext_t {
	char * dir;
	int64_t last;
	struct timespec time;
};

struct rapl_full_t {
	struct rapl_t parent;
	struct array_t * exts;
};

static void rapl_device_free(void * pointer) {
	struct rapl_device_t * device = pointer;
	free(device->name);
}

static void rapl_device_ext_free(void * pointer) {
	struct rapl_device_ext_t * ext = pointer;
	free(ext->dir);
}

struct rapl_t * rapl_init() {
	char buf[BUFSZ];
	DIR * dir;
	struct dirent * dirent;
	struct array_t * devices = NULL;
	struct array_t * exts = NULL;
	bool nomem = false;
	struct rapl_full_t * full = NULL;

	dir = opendir(DIR_POWERCAP);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open powercap directory\n");
		return NULL;
	}

	while ((dirent = readdir(dir))) {
		if (strstr(dirent->d_name, ":") && strlen(dirent->d_name) <= 30) {
			int fd;
BEGIN_IGNORE_FORMAT_OVERFLOW
			sprintf(buf, DIR_POWERCAP "/%s/name", dirent->d_name);
END_IGNORE_FORMAT_OVERFLOW
			fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int size = read(fd, buf, BUFSZ - 1);
				if (size >= 1) {
					struct rapl_device_t * device;
					struct rapl_device_ext_t * ext;
					char * name;
					char * dir;
					int name_length;
					int dir_length;

					name_length = buf[size - 1] == '\n' ? size - 1 : size;
					buf[name_length] = '\0';
					if (name_length == 0) {
						close(fd);
						continue;
					}
					name = malloc(name_length + 1);
					if (!name) {
						close(fd);
						nomem = true;
						break;
					}
					memcpy(name, buf, name_length + 1);

					dir_length = strlen(dirent->d_name);
					dir = malloc(dir_length + 1);
					if (!dir) {
						free(name);
						close(fd);
						nomem = true;
						break;
					}
					memcpy(dir, dirent->d_name, dir_length + 1);

					if (!devices && !exts) {
						devices = array_new(sizeof(struct rapl_device_t),
							rapl_device_free);
						exts = array_new(sizeof(struct rapl_device_ext_t),
							rapl_device_ext_free);
						if (!devices || !exts) {
							free(name);
							free(dir);
							close(fd);
							nomem = true;
							break;
						}
					}

					device = array_add(devices);
					if (!device) {
						free(name);
						free(dir);
						close(fd);
						nomem = true;
						break;
					}
					device->name = name;
					device->power = 0;

					ext = array_add(exts);
					if (!ext) {
						free(dir);
						close(fd);
						nomem = true;
						break;
					}
					ext->dir = dir;
					ext->last = 0;
					ext->time.tv_sec = 0;
					ext->time.tv_nsec = 0;
				}
				close(fd);
			}
		}
	}

	closedir(dir);
	if (!nomem) {
		full = malloc(sizeof(struct rapl_full_t));
		if (!full) {
			nomem = true;
		}
	}

	if (nomem) {
		if (devices) {
			array_free(devices);
		}
		if (exts) {
			array_free(exts);
		}
		if (full) {
			free(full);
		}
		fprintf(stderr, "No enough memory\n");
		return NULL;
	} else {
		array_shrink(devices);
		array_shrink(exts);
		full->parent.devices = devices;
		full->exts = exts;
		return &full->parent;
	}
}

void rapl_measure(struct rapl_t * rapl) {
	if (rapl) {
		char buf[BUFSZ];
		struct rapl_full_t * full = (struct rapl_full_t *) rapl;
		int i;

		for (i = 0; i < full->parent.devices->count; i++) {
			int fd;
			struct rapl_device_t * device = array_get(full->parent.devices, i);
			struct rapl_device_ext_t * ext = array_get(full->exts, i);
			sprintf(buf, DIR_POWERCAP "/%s/energy_uj", ext->dir);
			fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int size = read(fd, buf, BUFSZ - 1);
				if (size > 0) {
					struct timespec tnow;
					double power = 0;
					buf[buf[size - 1] == '\n' ? size - 1 : size] = '\0';
					int64_t value = (int64_t) atoll(buf);
					clock_gettime(CLOCK_MONOTONIC, &tnow);
					if (ext->last > 0 && value >= ext->last) {
						struct timespec tdiff;
						int64_t diff;
						tdiff.tv_sec = tnow.tv_sec - ext->time.tv_sec;
						tdiff.tv_nsec = tnow.tv_nsec - ext->time.tv_nsec;
						while (tdiff.tv_nsec < 0) {
							tdiff.tv_nsec += 1000000000;
							tdiff.tv_sec--;
						}
						diff = (int64_t) (tdiff.tv_sec * 1000000000 +
							tdiff.tv_nsec);
						power = (double) (value - ext->last) * 1000 / diff;
					} else {
						power = device->power;
					}
					ext->last = value;
					ext->time = tnow;
					device->power = power;
				}
				close(fd);
			}
		}
	}
}

void rapl_free(struct rapl_t * rapl) {
	if (rapl) {
		struct rapl_full_t * full = (struct rapl_full_t *) rapl;
		if (full->parent.devices) {
			array_free(full->parent.devices);
		}
		if (full->exts) {
			array_free(full->exts);
		}
		free(full);
	}
}
