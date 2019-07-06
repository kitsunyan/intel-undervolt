#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "util.h"

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define MSR_ADDR_TEMPERATURE 0x1a2
#define MSR_ADDR_UNITS 0x606
#define MSR_ADDR_VOLTAGE 0x150

struct undervolt_t {
	int index;
	char * title;
	float value;
};

struct power_domain_t {
	const char * name;
	size_t mem_addr;
	int msr_addr;
};

static struct power_domain_t power_domains[1] = {
	{ "package", 0xfed159a0, 0x610 }
};

struct power_limit_value_t {
	int power;
	float time_window;
	bool enabled;
};

struct power_limit_t {
	bool apply;
	struct power_limit_value_t short_term;
	struct power_limit_value_t long_term;
	void * mem;
};

struct hwp_power_term_t {
	bool and;
	char * domain;
	bool greater;
	double power;
};

struct hwp_hint_t {
	bool force;
	bool load;
	bool load_multi;
	float load_threshold;
	bool power;
	struct array_t * hwp_power_terms;
	char * load_hint;
	char * normal_hint;
};

enum daemon_action_kind {
	DAEMON_ACTION_KIND_UNDERVOLT,
	DAEMON_ACTION_KIND_POWER,
	DAEMON_ACTION_KIND_TJOFFSET
};

struct daemon_action_t {
	enum daemon_action_kind kind;
	bool once;
};

struct config_t {
	int fd_msr;
	int fd_mem;
	bool enable;
	struct array_t * undervolts;
	struct power_limit_t power[ARRAY_SIZE(power_domains)];
	bool tjoffset_apply;
	float tjoffset;
	struct array_t * hwp_hints;
	int interval;
	struct array_t * daemon_actions;
};

void free_config(struct config_t * config);
struct config_t * load_config(struct config_t * old_config, bool * nl);

#endif
