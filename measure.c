#include "measure.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define POWERCAP "/sys/class/powercap"
#define BUFSZ 80

typedef struct {
	void * next;
	char * name;
	char * dir;
	int64_t last;
	struct timespec time;
} rapl_list_t;

static void print_next(rapl_list_t * ilst, int maxname, char * buf) {
	if (!ilst) {
		/* clear the screen */
		printf("\033[H\033[J");
		fflush(0);
		return;
	}
	print_next(ilst->next, maxname, buf);

	sprintf(buf, POWERCAP "/%s/energy_uj", ilst->dir);
	int fd = open(buf, O_RDONLY);
	if (fd >= 0) {
		int count = read(fd, buf, BUFSZ - 1);
		if (count > 0) {
			buf[buf[count - 1] == '\n' ? count - 1 : count] = '\0';
			int64_t val = (int64_t) atoll(buf);
			struct timespec tnow;
			clock_gettime(CLOCK_MONOTONIC, &tnow);
			if (ilst->last > 0) {
				struct timespec tdiff;
				tdiff.tv_sec = tnow.tv_sec - ilst->time.tv_sec;
				tdiff.tv_nsec = tnow.tv_nsec - ilst->time.tv_nsec;
				while (tdiff.tv_nsec < 0) {
					tdiff.tv_nsec += 1000000000;
					tdiff.tv_sec--;
				}
				int64_t diff = (int64_t) (tdiff.tv_sec * 1000000000 +
					tdiff.tv_nsec);
				double dval = (double) (val - ilst->last) * 1000 / diff;
				int len = strlen(ilst->name);
				write(0, ilst->name, len);
				write(0, ":", 1);
				int i;
				for (i = len - 1; i < maxname; i++) {
					write(0, " ", 1);
				}
				printf("%7.03f W\n", ilst->name, dval);
			}
			ilst->last = val;
			ilst->time = tnow;
		}
		close(fd);
	}
}

static bool interrupted;

static void sigint_handler(int sig) {
	interrupted = true;
}

int measure_mode() {
	char buf[BUFSZ];
	rapl_list_t * glst = NULL;
	int maxname = 0;

	DIR * dir = opendir(POWERCAP);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open powercap directory\n");
		return 1;
	}
	struct dirent * dirent;
	while (dirent = readdir(dir)) {
		if (strstr(dirent->d_name, ":") && strlen(dirent->d_name) <= 30) {
			sprintf(buf, POWERCAP "/%s/name", dirent->d_name);
			int fd = open(buf, O_RDONLY);
			if (fd >= 0) {
				int count = read(fd, buf, BUFSZ - 1);
				if (count > 1) {
					int nlen = buf[count - 1] == '\n' ? count - 2 : count - 1;
					buf[nlen + 1] = '\0';
					rapl_list_t * nlst = malloc(sizeof(rapl_list_t));
					nlst->next = glst;
					glst = nlst;
					nlst->name = malloc(nlen + 1);
					memcpy(nlst->name, buf, nlen + 1);
					int dlen = strlen(dirent->d_name);
					nlst->dir = malloc(dlen + 1);
					memcpy(nlst->dir, dirent->d_name, dlen + 1);
					nlst->last = 0;
					maxname = nlen > maxname ? nlen : maxname;
				}
				close(fd);
			}
		}
	}
	closedir(dir);

	interrupted = false;
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = sigint_handler;
	sigaction(SIGINT, &act, NULL);

	while (!interrupted) {
		print_next(glst, maxname, buf);
		sleep(1);
	}

	while (glst) {
		rapl_list_t * nlst = glst->next;
		free(glst->name);
		free(glst->dir);
		free(glst);
		glst = nlst;
	}

	return 0;
}
