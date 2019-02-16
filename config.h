#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "util.h"

#include <stdbool.h>
#include <stddef.h>

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define MSR_ADDR_TEMPERATURE 0x1a2
#define MSR_ADDR_UNITS 0x606
#define MSR_ADDR_VOLTAGE 0x150

typedef struct {
	int index;
	char * title;
	float value;
} undervolt_t;

typedef struct {
	const char * name;
	size_t mem_addr;
	int msr_addr;
} power_domain_t;

static power_domain_t power_domains[1] = {
	{ "package", 0xfed159a0, 0x610 }
};

typedef struct {
	int power;
	float time_window;
	bool enabled;
} power_limit_value_t;

typedef struct {
	bool apply;
	power_limit_value_t short_term;
	power_limit_value_t long_term;
	void * mem;
} power_limit_t;

typedef struct {
	int fd_msr;
	int fd_mem;
	array_t * undervolts;
	power_limit_t power[ARRAY_SIZE(power_domains)];
	bool tjoffset_apply;
	float tjoffset;
	int interval;
} config_t;

void free_config(config_t * config);
config_t * load_config(config_t * old_config, bool * nl);

#endif
