#include "stat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cpu_stat_value_t {
	long int idle;
	long int total;
};

struct cpu_stat_full_t {
	struct cpu_stat_t parent;
	int cpu_count;
	struct cpu_stat_value_t * values;
};

static void read_eol(FILE * file) {
	int c;
	while ((c = fgetc(file)) != EOF && c != '\n');
}

struct cpu_stat_t * cpu_stat_init() {
	FILE * file;
	char buf[80];
	int cpu_count = 0;
	int index;

	file = fopen("/proc/stat", "r");
	if (file) {
		while (fscanf(file, "%s", buf) == 1 && strstr(buf, "cpu") == buf) {
			read_eol(file);
			if (strlen(buf) > 3) {
				index = atoi(&buf[3]) + 1;
				cpu_count = cpu_count >= index ? cpu_count : index;
			}
		}
		fclose(file);
	}

	if (cpu_count > 0) {
		struct cpu_stat_full_t * full = malloc(sizeof(struct cpu_stat_full_t));
		struct cpu_stat_value_t * values = malloc(cpu_count *
			sizeof(struct cpu_stat_value_t));
		if (!full || !values) {
			if (full) {
				free(full);
			}
			if (values) {
				free(values);
			}
			fprintf(stderr, "No enough memory\n");
			return NULL;
		}
		memset(values, 0, cpu_count * sizeof(struct cpu_stat_value_t));
		full->parent.single_core = 0;
		full->parent.multi_core = 0;
		full->cpu_count = cpu_count;
		full->values = values;
		return &full->parent;
	} else {
		fprintf(stderr, "Failed to read /proc/stat\n");
		return NULL;
	}
}

void cpu_stat_measure(struct cpu_stat_t * cpu_stat) {
	if (cpu_stat) {
		struct cpu_stat_full_t * full = (struct cpu_stat_full_t *) cpu_stat;
		int index;
		int count;
		long int idle;
		long int total;
		long int value;
		float single_core = 0;
		float multi_core = 0;
		double load;
		FILE * file;
		char buf[80];

		file = fopen("/proc/stat", "r");
		if (file) {
			while (fscanf(file, "%s", buf) == 1 && strstr(buf, "cpu") == buf) {
				if (strlen(buf) > 3) {
					index = atoi(&buf[3]);
					if (index >= 0 && index < full->cpu_count) {
						count = 0;
						idle = 0;
						total = 0;
						while (fscanf(file, "%ld", &value) == 1) {
							count++;
							total += value;
							if (count == 4) {
								idle = value;
							}
						}

						if (count >= 4) {
							if (full->values[index].idle > 0 &&
								full->values[index].total > 0) {
								load = (double) (total -
									full->values[index].total -
									idle + full->values[index].idle) /
									(total - full->values[index].total);
								single_core = load > single_core
									? load : single_core;
								multi_core = multi_core > 0
									? multi_core + load : load;
							}
							full->values[index].idle = idle;
							full->values[index].total = total;
						}
					}
				} else {
					read_eol(file);
				}
			}
			fclose(file);
		}

		full->parent.single_core = single_core;
		full->parent.multi_core = multi_core;
	}
}

void cpu_stat_free(struct cpu_stat_t * cpu_stat) {
	if (cpu_stat) {
		struct cpu_stat_full_t * full = (struct cpu_stat_full_t *) cpu_stat;
		free(full->values);
		free(full);
	}
}
