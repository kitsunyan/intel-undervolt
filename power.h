#ifndef __POWER_H__
#define __POWER_H__

#include "util.h"

typedef struct {
	char * name;
	float power;
} rapl_device_t;

typedef struct {
	array_t * devices;
} rapl_t;

rapl_t * rapl_init();
void rapl_measure(rapl_t * rapl);
void rapl_free(rapl_t * rapl);

#endif
