#include "config.h"
#include "modes.h"
#include "undervolt.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool read_apply_mode(bool write) {
	bool nl = false;
	config_t * config = load_config(NULL, &nl);
	if (config) {
		bool success = true;
		int i;
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

static void sigusr1_handler(int sig) {
	reload_config = true;
}

int daemon_mode() {
	config_t * config = load_config(NULL, NULL);
	if (config && config->interval <= 0) {
		fprintf(stderr, "Interval is not specified\n");
		free_config(config);
		config = NULL;
	}
	if (config) {
		struct sigaction act;
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

			int i = 0;
			for (i = 0; i < ARRAY_SIZE(config->power); i++) {
				power_limit(config, i, NULL, true);
			}
			tjoffset(config, NULL, true);

			usleep(config->interval * 1000);
		}
	}

	if (!config) {
		fprintf(stderr, "Failed to setup the program\n");
		return false;
	} else {
		free_config(config);
		return true;
	}
}
