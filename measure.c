#include "measure.h"
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

#define DIR_POWERCAP "/sys/class/powercap"
#define DIR_HWMON "/sys/class/hwmon"
#define BUFSZ 80

typedef struct {
	void * next;
	char * name;
	char * dir;
	int64_t last;
	struct timespec time;
} powercap_list_t;

typedef struct {
	void * next;
	char * name;
	char * dir;
	int index;
} hwmon_list_t;

static void write_maxname(const char * name, int maxname) {
	int len = strlen(name);
	write(0, name, len);
	write(0, ":", 1);
	int i;
	for (i = len - 1; i < maxname; i++) {
		write(0, " ", 1);
	}
}

static void print_powercap_next(powercap_list_t * lst, int maxname, char * buf,
	bool * nl, bool * nll, bool csv) {
	if (!lst) {
		return;
	}
	bool nlla = false;
	if (!nll) {
		nll = &nlla;
	}
	print_powercap_next(lst->next, maxname, buf, nl, nll, csv);

	sprintf(buf, DIR_POWERCAP "/%s/energy_uj", lst->dir);
	int fd = open(buf, O_RDONLY);
	if (fd >= 0) {
		int count = read(fd, buf, BUFSZ - 1);
		if (count > 0) {
			buf[buf[count - 1] == '\n' ? count - 1 : count] = '\0';
			int64_t val = (int64_t) atoll(buf);
			struct timespec tnow;
			clock_gettime(CLOCK_MONOTONIC, &tnow);
			double dval = 0;
			if (lst->last > 0) {
				struct timespec tdiff;
				tdiff.tv_sec = tnow.tv_sec - lst->time.tv_sec;
				tdiff.tv_nsec = tnow.tv_nsec - lst->time.tv_nsec;
				while (tdiff.tv_nsec < 0) {
					tdiff.tv_nsec += 1000000000;
					tdiff.tv_sec--;
				}
				int64_t diff = (int64_t) (tdiff.tv_sec * 1000000000 +
					tdiff.tv_nsec);
				dval = (double) (val - lst->last) * 1000 / diff;
			}
			lst->last = val;
			lst->time = tnow;
			if (csv) {
				CSV_SEPARATOR(nl, *nll);
				printf("%.03f", dval);
			} else {
				NEW_LINE(nl, *nll);
				write_maxname(lst->name, maxname);
				printf("%9.03f W\n", dval);
			}
		}
		close(fd);
	}
}

static void print_hwmon_next(hwmon_list_t * lst, int maxname, char * buf,
	char * degstr, bool * nl, bool * nll, bool csv) {
	if (!lst) {
		return;
	}
	bool nlla = false;
	if (!nll) {
		nll = &nlla;
	}
	print_hwmon_next(lst->next, maxname, buf, degstr, nl, nll, csv);

	sprintf(buf, DIR_HWMON "/%s/temp%d_input", lst->dir, lst->index);
	int fd = open(buf, O_RDONLY);
	if (fd >= 0) {
		int count = read(fd, buf, BUFSZ - 1);
		if (count > 0) {
			buf[buf[count - 1] == '\n' ? count - 1 : count] = '\0';
			double dval = atol(buf) / 1000.;
			if (csv) {
				CSV_SEPARATOR(nl, *nll);
				printf("%.03f", dval);
			} else {
				NEW_LINE(nl, *nll);
				write_maxname(lst->name, maxname);
				printf("%9.03f%s\n", dval, degstr);
			}
		}
		close(fd);
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
		int count = read(fd, buf, BUFSZ - 1);
		if (count > 0) {
			buf[buf[count - 1] == '\n' ? count - 1 : count] = '\0';
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

static powercap_list_t * get_powercap(int * maxname) {
	char buf[BUFSZ];
	powercap_list_t * lst = NULL;

	DIR * dir = opendir(DIR_POWERCAP);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open powercap directory\n");
		return NULL;
	}
	struct dirent * dirent;
	while ((dirent = readdir(dir))) {
		if (strstr(dirent->d_name, ":") && strlen(dirent->d_name) <= 30) {
			sprintf(buf, DIR_POWERCAP "/%s/name", dirent->d_name);
			int fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int count = read(fd, buf, BUFSZ - 1);
				if (count > 1) {
					int nlen = buf[count - 1] == '\n' ? count - 2 : count - 1;
					buf[nlen + 1] = '\0';
					powercap_list_t * nlst = malloc(sizeof(powercap_list_t));
					nlst->next = lst;
					lst = nlst;
					nlst->name = malloc(nlen + 1);
					memcpy(nlst->name, buf, nlen + 1);
					int dlen = strlen(dirent->d_name);
					nlst->dir = malloc(dlen + 1);
					memcpy(nlst->dir, dirent->d_name, dlen + 1);
					nlst->last = 0;
					if (maxname) {
						*maxname = nlen > *maxname ? nlen : *maxname;
					}
				}
				close(fd);
			}
		}
	}
	closedir(dir);

	return lst;
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
			sprintf(buf, DIR_HWMON "/%s/name", dirent->d_name);
			int fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int count = read(fd, buf, BUFSZ - 1);
				if (count > 1) {
					int nlen = buf[count - 1] == '\n' ? count - 2 : count - 1;
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

static hwmon_list_t * get_coretemp(int * maxname) {
	char hdir[BUFSZ];
	char buf[BUFSZ];
	hwmon_list_t * lst = NULL;

	if (!get_hwmon("coretemp", hdir)) {
		fprintf(stderr, "Failed to find coretemp hwmon\n");
		return NULL;
	}

	int i;
	for (i = 1;; i++) {
		sprintf(buf, DIR_HWMON "/%s/temp%d_input", hdir, i);
		int fd = open(buf, O_RDONLY);
		if (fd < 0) {
			break;
		}
		sprintf(buf, DIR_HWMON "/%s/temp%d_label", hdir, i);
		fd = open(buf, O_RDONLY);
		char * name = NULL;
		if (fd >= 0) {
			int count = read(fd, buf, BUFSZ - 1);
			if (count > 1) {
				int nlen = buf[count - 1] == '\n' ? count - 2 : count - 1;
				buf[nlen + 1] = '\0';
				name = malloc(strlen(buf) + 1);
				strcpy(name, buf);
			}
			close(fd);
		}
		if (!name) {
			sprintf(buf, "temp%d", i);
			name = malloc(strlen(buf) + 1);
			strcpy(name, buf);
		}
		hwmon_list_t * nlst = malloc(sizeof(hwmon_list_t));
		nlst->next = lst;
		lst = nlst;
		nlst->name = name;
		nlst->dir = malloc(strlen(hdir) + 1);
		strcpy(nlst->dir, hdir);
		nlst->index = i;
		if (maxname) {
			int len = strlen(name);
			*maxname = len > *maxname ? len : *maxname;
		}
	}

	return lst;
}

static bool interrupted;

static void sigint_handler(UNUSED int sig) {
	interrupted = true;
}

int measure_mode() {
	char buf[BUFSZ];
	int maxname = 0;
	powercap_list_t * powercap_list = get_powercap(&maxname);
	hwmon_list_t * coretemp_list = get_coretemp(&maxname);
	char degstr[5] = " C";

	char * format_env = getenv("FORMAT");
	bool csv = format_env != NULL && !strcmp(format_env, "csv");
	char * sleep_env = getenv("SLEEP");
	double sleep_double = sleep_env != NULL ? atof(sleep_env) : 0;
	if (sleep_double <= 0) {
		sleep_double = 1;
	}
	struct timespec sleep;
	sleep.tv_sec = (time_t) sleep_double;
	sleep.tv_nsec = (suseconds_t) ((sleep_double - sleep.tv_sec) * 1000000000.);

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
	} else {
		/* clear the screen */
		printf("\033[H\033[J");
	}
	while (!interrupted) {
		if (!csv) {
			/* move the cursor */
			printf("\033[H");
			fflush(0);
		}
		bool nl = false;
		if (csv) {
			struct timespec csv_now;
			clock_gettime(CLOCK_MONOTONIC, &csv_now);
			double csv_diff = (csv_now.tv_sec - csv_start.tv_sec) +
				(csv_now.tv_nsec - csv_start.tv_nsec) / 1000000000.;
			bool nll = false;
			CSV_SEPARATOR((bool *) &nl, nll);
			printf("%.03f", csv_diff);
		}
		print_powercap_next(powercap_list, maxname, buf, &nl, NULL, csv);
		print_hwmon_next(coretemp_list, maxname, buf, degstr, &nl, NULL, csv);
		print_cpufreq(maxname, buf, &nl, csv);
		if (csv) {
			printf("\n");
		}
		if (!interrupted) {
			nanosleep(&sleep, NULL);
		}
	}

	while (powercap_list) {
		powercap_list_t * next = powercap_list->next;
		free(powercap_list->name);
		free(powercap_list->dir);
		free(powercap_list);
		powercap_list = next;
	}
	while (coretemp_list) {
		hwmon_list_t * next = coretemp_list->next;
		free(coretemp_list->name);
		free(coretemp_list->dir);
		free(coretemp_list);
		coretemp_list = next;
	}

	return 0;
}
