#include "measure.h"
#include "power.h"
#include "util.h"

#include <dirent.h>
#include <fcntl.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DIR_HWMON "/sys/class/hwmon"
#define BUFSZ 80

struct hwmon_t {
	char * name;
	char * dir;
	int index;
};

static void hwmon_free(void * pointer) {
	struct hwmon_t * hwmon = pointer;
	free(hwmon->name);
	free(hwmon->dir);
}

static void write_maxname(const char * name, int maxname) {
	int len = strlen(name);
	int i;
	printf("%s:", name);
	for (i = len - 1; i < maxname; i++) {
		printf(" ");
	}
}

static void print_rapl(struct rapl_t * rapl, int maxname, bool * nl, bool csv) {
	bool nll = false;
	int i;

	rapl_measure(rapl);
	if (rapl) {
		for (i = 0; i < rapl->devices->count; i++) {
			struct rapl_device_t * device = array_get(rapl->devices, i);
			if (csv) {
				CSV_SEPARATOR(nl, nll);
				printf("%.03f", device->power);
			} else {
				NEW_LINE(nl, nll);
				write_maxname(device->name, maxname);
				printf("%9.03f W\n", device->power);
			}
		}
	}
}

static void print_hwmon(struct array_t * hwmons, int maxname, char * buf,
	char * degstr, bool * nl, bool csv) {
	bool nll = false;
	int i;

	for (i = 0; hwmons && i < hwmons->count; i++) {
		struct hwmon_t * hwmon = array_get(hwmons, i);
		int fd;

		sprintf(buf, DIR_HWMON "/%s/temp%d_input", hwmon->dir, hwmon->index);
		fd = open(buf, O_RDONLY);
		if (fd >= 0) {
			int size = read(fd, buf, BUFSZ - 1);
			if (size > 0) {
				double value;
				buf[buf[size - 1] == '\n' ? size - 1 : size] = '\0';
				value = atol(buf) / 1000.;
				if (csv) {
					CSV_SEPARATOR(nl, nll);
					printf("%.03f", value);
				} else {
					NEW_LINE(nl, nll);
					write_maxname(hwmon->name, maxname);
					printf("%9.03f%s\n", value, degstr);
				}
			}
			close(fd);
		}
	}
}

static void print_cpufreq(int maxname, char * buf, bool * nl, bool csv) {
	bool nll = false;

	int i;
	for (i = 0;; i++) {
		sprintf(buf, "/sys/bus/cpu/devices/cpu%d/cpufreq/scaling_cur_freq", i);
		int fd = open(buf, O_RDONLY);
		if (fd < 0) {
			break;
		}
		int size = read(fd, buf, BUFSZ - 1);
		if (size > 0) {
			buf[buf[size - 1] == '\n' ? size - 1 : size] = '\0';
			double dval = atol(buf) / 1000.;
			if (csv) {
				CSV_SEPARATOR(nl, nll);
				printf("%.03f", dval);
			} else {
				sprintf(buf, "Core %d", i);
				NEW_LINE(nl, nll);
				write_maxname(buf, maxname);
				printf("%9.03f MHz\n", dval);
			}
		}
		close(fd);
	}
}

static bool get_hwmon(const char * name, char * out) {
	char buf[BUFSZ];

	DIR * dir = opendir(DIR_HWMON);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open hwmon directory\n");
		return NULL;
	}
	struct dirent * dirent;
	while ((dirent = readdir(dir))) {
		if (strlen(dirent->d_name) <= 30) {
BEGIN_IGNORE_FORMAT_OVERFLOW
			sprintf(buf, DIR_HWMON "/%s/name", dirent->d_name);
END_IGNORE_FORMAT_OVERFLOW
			int fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int size = read(fd, buf, BUFSZ - 1);
				if (size > 1) {
					int nlen = buf[size - 1] == '\n' ? size - 2 : size - 1;
					buf[nlen + 1] = '\0';
					if (!strcmp(buf, name)) {
						strcpy(out, dirent->d_name);
						close(fd);
						closedir(dir);
						return true;
					}
				}
				close(fd);
			}
		}
	}
	closedir(dir);

	return false;
}

static struct array_t * get_coretemp(int * maxname) {
	char hdir[BUFSZ];
	char buf[BUFSZ];
	struct array_t * hwmons = NULL;
	int i;

	if (!get_hwmon("coretemp", hdir)) {
		fprintf(stderr, "Failed to find coretemp hwmon\n");
		return NULL;
	}

	for (i = 1;; i++) {
		int fd;
		char * name = NULL;
		char * dir;
		struct hwmon_t * hwmon;

BEGIN_IGNORE_FORMAT_OVERFLOW
		sprintf(buf, DIR_HWMON "/%s/temp%d_input", hdir, i);
END_IGNORE_FORMAT_OVERFLOW
		fd = open(buf, O_RDONLY);
		if (fd < 0) {
			break;
		}
		close(fd);

BEGIN_IGNORE_FORMAT_OVERFLOW
		sprintf(buf, DIR_HWMON "/%s/temp%d_label", hdir, i);
END_IGNORE_FORMAT_OVERFLOW
		fd = open(buf, O_RDONLY);
		if (fd >= 0) {
			int size = read(fd, buf, BUFSZ - 1);
			if (size > 1) {
				int nlen = buf[size - 1] == '\n' ? size - 2 : size - 1;
				buf[nlen + 1] = '\0';
				name = malloc(strlen(buf) + 1);
				if (!name) {
					break;
				}
				strcpy(name, buf);
			}
			close(fd);
		}
		if (!name) {
			sprintf(buf, "temp%d", i);
			name = malloc(strlen(buf) + 1);
			if (!name) {
				break;
			}
			strcpy(name, buf);
		}

		dir = malloc(strlen(hdir) + 1);
		if (!dir) {
			free(name);
			break;
		}
		strcpy(dir, hdir);

		if (!hwmons) {
			hwmons = array_new(sizeof(struct hwmon_t), hwmon_free);
			if (!hwmons) {
				free(name);
				free(dir);
				break;
			}
		}
		hwmon = array_add(hwmons);
		if (!hwmon) {
			free(name);
			free(dir);
			break;
		}

		hwmon->name = name;
		hwmon->dir = dir;
		hwmon->index = i;
		if (maxname) {
			int len = strlen(name);
			*maxname = len > *maxname ? len : *maxname;
		}
	}

	if (hwmons) {
		array_shrink(hwmons);
	}
	return hwmons;
}

static bool interrupted;

static void sigint_handler(UNUSED int sig) {
	interrupted = true;
}

bool measure_mode(bool csv, float sleep) {
	char buf[BUFSZ];
	int maxname = 0;
	struct rapl_t * rapl = rapl_init();
	struct array_t * coretemp = get_coretemp(&maxname);
	char degstr[5] = " C";
	bool tty = isatty(1);

	if (rapl) {
		int i;
		for (i = 0; i < rapl->devices->count; i++) {
			struct rapl_device_t * device = array_get(rapl->devices, i);
			int length = strlen(device->name);
			maxname = length > maxname ? length : maxname;
		}
	}

	struct timespec sleep_spec;
	sleep_spec.tv_sec = (time_t) sleep;
	sleep_spec.tv_nsec = (suseconds_t) ((sleep - sleep_spec.tv_sec)
		* 1000000000.);

	setlocale(LC_CTYPE, "");
	if (!csv) {
		iconv_t ic = iconv_open(nl_langinfo(CODESET), "ISO-8859-1");
		if (ic != (iconv_t) -1) {
			char in[3] = "\260C";
			char * inptr = in;
			char * outptr = degstr;
			size_t insize = 3;
			size_t outsize = sizeof(degstr);
			iconv(ic, &inptr, &insize, &outptr, &outsize);
			iconv_close(ic);
		}
	}

	interrupted = false;
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = sigint_handler;
	sigaction(SIGINT, &act, NULL);

	struct timespec csv_start;
	if (csv) {
		clock_gettime(CLOCK_MONOTONIC, &csv_start);
	} else if (tty) {
		/* clear the screen */
		printf("\x1b[H\x1b[J");
		/* hide the cursor */
		printf("\x1b[?25l");
	}
	bool nl = false;
	while (!interrupted) {
		if (!csv && tty) {
			/* move the cursor */
			printf("\x1b[H");
		}
		if (tty) {
			nl = false;
		}
		if (csv) {
			struct timespec csv_now;
			clock_gettime(CLOCK_MONOTONIC, &csv_now);
			double csv_diff = (csv_now.tv_sec - csv_start.tv_sec) +
				(csv_now.tv_nsec - csv_start.tv_nsec) / 1000000000.;
			bool nll = false;
			CSV_SEPARATOR((bool *) &nl, nll);
			printf("%.03f", csv_diff);
		}
		print_rapl(rapl, maxname, &nl, csv);
		print_hwmon(coretemp, maxname, buf, degstr, &nl, csv);
		print_cpufreq(maxname, buf, &nl, csv);
		if (csv) {
			printf("\n");
		}
		fflush(stdout);
		if (!interrupted) {
			nanosleep(&sleep_spec, NULL);
		}
	}
	if (!csv && tty) {
		/* show the cursor */
		printf("\x1b[?25h");
	}

	if (rapl) {
		rapl_free(rapl);
	}
	if (coretemp) {
		array_free(coretemp);
	}

	return true;
}
