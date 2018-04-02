/*
 * Copyright (C) 2018 kitsunyan <kitsunyan@inbox.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

typedef uint64_t data_t;

int main(int argc, char ** argv) {
	if (argc < 3 || argc > 4) {
		return 1;
	} else if (!strcmp("msr", argv[1])) {
		data_t addr = strtoull(argv[2], 0, 0);

		int fd = open("/dev/cpu/0/msr", O_RDWR | O_SYNC);
		if (fd < 0) {
			perror(NULL);
			return 1;
		}

		char buf[8];
		if (argc == 4) {
			data_t data = strtoull(argv[3], 0, 0);
			*(data_t *) buf = data;
			if (pwrite(fd, buf, 8, addr) != 8) {
				close(fd);
				fprintf(stderr, "Failed to write MSR\n");
				return 1;
			}
		} else {
			if (pread(fd, buf, 8, addr) != 8) {
				close(fd);
				fprintf(stderr, "Failed to read MSR\n");
				return 1;
			}
			data_t data = *(data_t *) buf;
			printf("0x%016llx\n", data);
		}

		close(fd);
		return 0;
	} else if (!strcmp("mem", argv[1])) {
		data_t addr = strtoull(argv[2], 0, 0);

		int fd = open("/dev/mem", O_RDWR | O_SYNC);
		if (fd < 0) {
			perror(NULL);
			return 1;
		}

		void * base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, addr & ~MAP_MASK);

		if (base == NULL || base == MAP_FAILED) {
			perror(NULL);
			return 1;
		}
		void * mem = base + (addr & MAP_MASK);

		if (argc == 4) {
			data_t data = strtoull(argv[3], 0, 0);
			*(data_t *) mem = data;
		} else {
			data_t data = *(data_t *) mem;
			printf("0x%016llx\n", data);
		}

		munmap(base, MAP_SIZE);
		close(fd);
		return 0;
	} else {
		return 1;
	}
}
