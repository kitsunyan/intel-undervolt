#ifndef __SCALING_H__
#define __SCALING_H__

#include "util.h"

struct cpu_policy_t;

struct cpu_policy_t * cpu_policy_init();
void cpu_policy_update(struct cpu_policy_t * cpu_policy, struct array_t * hwp_hints);
void cpu_policy_free(struct cpu_policy_t * cpu_policy);

#endif
