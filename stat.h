#ifndef __STAT_H__
#define __STAT_H__

typedef struct {
	float single_core;
	float multi_core;
} cpu_stat_t;

cpu_stat_t * cpu_stat_init();
void cpu_stat_measure(cpu_stat_t * cpu_stat);
void cpu_stat_free(cpu_stat_t * cpu_stat);

#endif
