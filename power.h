#ifndef __POWER_H__
#define __POWER_H__

#include "util.h"

struct rapl_device_t {
	char * name;
	float power;
};

struct rapl_t {
	struct array_t * devices;
};

struct rapl_t * rapl_init();
void rapl_measure(struct rapl_t * rapl);
void rapl_free(struct rapl_t * rapl);

#endif
