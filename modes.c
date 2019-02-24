#include "config.h"
#include "modes.h"
#include "scaling.h"
#include "undervolt.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool read_apply_mode(bool write) {
	bool nl = false;
	config_t * config = load_config(NULL, &nl);
	bool success = true;
	unsigned int i;

	if (config) {
		success &= undervolt(config, &nl, write);
		for (i = 0; i < ARRAY_SIZE(config->power); i++) {
			success &= power_limit(config, i, &nl, write);
		}
		success &= tjoffset(config, &nl, write);

		free_config(config);
		return success;
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
	config_t * config = load_config(NULL, NULL);
	struct sigaction act;
	unsigned int i = 0;
	cpu_policy_t * cpu_policy = NULL;

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

			for (i = 0; i < ARRAY_SIZE(config->power); i++) {
				power_limit(config, i, NULL, true);
			}
			tjoffset(config, NULL, true);

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
