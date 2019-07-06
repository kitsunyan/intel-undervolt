#include "config.h"
#include "modes.h"
#include "scaling.h"
#include "undervolt.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool read_apply_mode(bool write, bool trigger) {
	bool nl = false;
	struct config_t * config = load_config(NULL, &nl);
	bool success = true;
	unsigned int i;

	if (config) {
		if (trigger && !config->enable) {
			fprintf(stderr, "Triggers are disabled\n");
			return false;
		} else {
			success &= undervolt(config, &nl, write);
			for (i = 0; i < ARRAY_SIZE(config->power); i++) {
				success &= power_limit(config, i, &nl, write);
			}
			success &= tjoffset(config, &nl, write);

			free_config(config);
			return success;
		}
	} else {
		fprintf(stderr, "Failed to setup the program\n");
		return false;
	}
}

static bool reload_config;

static void sigusr1_handler(UNUSED int sig) {
	reload_config = true;
}

int daemon_mode() {
	struct config_t * config = load_config(NULL, NULL);
	struct sigaction act;
	int i = 0;
	bool undervolt_done = false;
	bool power_done = false;
	bool tjoffset_done = false;
	struct cpu_policy_t * cpu_policy = NULL;

	if (config && config->interval <= 0) {
		fprintf(stderr, "Interval is not specified\n");
		free_config(config);
		config = NULL;
	}

	if (config) {
		memset(&act, 0, sizeof(struct sigaction));
		act.sa_handler = sigusr1_handler;
		sigaction(SIGUSR1, &act, NULL);

		reload_config = false;
		while (true) {
			if (reload_config) {
				reload_config = false;
				printf("Reloading configuration\n");
				config = load_config(config, NULL);
				if (!config) {
					break;
				}
			}

			if (config->hwp_hints && !cpu_policy) {
				cpu_policy = cpu_policy_init();
			}

			for (i = 0; config->daemon_actions && i < config->daemon_actions->count; i++) {
				struct daemon_action_t * daemon_action = array_get(config->daemon_actions, i);
				switch (daemon_action->kind) {
					case DAEMON_ACTION_KIND_UNDERVOLT: {
						if (!daemon_action->once || !undervolt_done) {
							undervolt(config, NULL, true);
						}
						undervolt_done = true;
						break;
					}
					case DAEMON_ACTION_KIND_POWER: {
						if (!daemon_action->once || !power_done) {
							for (i = 0; i < (int) ARRAY_SIZE(config->power); i++) {
								power_limit(config, i, NULL, true);
							}
						}
						power_done = true;
						break;
					}
					case DAEMON_ACTION_KIND_TJOFFSET: {
						if (!daemon_action->once || !tjoffset_done) {
							tjoffset(config, NULL, true);
						}
						tjoffset_done = true;
						break;
					}
				}
			}

			if (cpu_policy) {
				if (config->hwp_hints) {
					cpu_policy_update(cpu_policy, config->hwp_hints);
				} else {
					cpu_policy_free(cpu_policy);
					cpu_policy = NULL;
				}
			}

			usleep(config->interval * 1000);
		}
	}

	if (cpu_policy) {
		cpu_policy_free(cpu_policy);
	}

	if (!config) {
		fprintf(stderr, "Failed to setup the program\n");
		return false;
	} else {
		free_config(config);
		return true;
	}
}
