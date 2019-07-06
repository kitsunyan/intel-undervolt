#ifndef __STAT_H__
#define __STAT_H__

struct cpu_stat_t {
	float single_core;
	float multi_core;
};

struct cpu_stat_t * cpu_stat_init();
void cpu_stat_measure(struct cpu_stat_t * cpu_stat);
void cpu_stat_free(struct cpu_stat_t * cpu_stat);

#endif
