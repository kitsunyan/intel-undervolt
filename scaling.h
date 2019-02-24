#ifndef __SCALING_H__
#define __SCALING_H__

#include "util.h"

typedef void cpu_policy_t;

cpu_policy_t * cpu_policy_init();
void cpu_policy_update(cpu_policy_t * cpu_policy, array_t * hwp_hints);
void cpu_policy_free(cpu_policy_t * cpu_policy);

#endif
