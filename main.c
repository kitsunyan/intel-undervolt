#include "modes.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv) {
	bool write;
	if (argc == 2 && !strcmp(argv[1], "read")) {
		return read_apply_mode(false) ? 0 : 1;
	} else if (argc == 2 && !strcmp(argv[1], "apply")) {
		return read_apply_mode(true) ? 0 : 1;
	} else if (argc == 2 && !strcmp(argv[1], "daemon")) {
		return daemon_mode() ? 0 : 1;
	} else {
		fprintf(stderr,
			"Usage: intel-undervolt COMMAND\n"
			"  read      Read and display current values\n"
			"  apply     Apply values from config file\n"
			"  daemon    Run in daemon mode\n");
		return argc == 1 ? 0 : 1;
	}
}
