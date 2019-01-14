#ifndef __UNDERVOLT_H__
#define __UNDERVOLT_H__

#include "config.h"

#include <stdbool.h>

bool undervolt(config_t * config, bool * nl, bool write);
bool power_limit(config_t * config, int index, bool * nl, bool write);
bool tjoffset(config_t * config, bool * nl, bool write);

#endif
