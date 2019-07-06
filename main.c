#include "measure.h"
#include "modes.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum arg_mode {
	ARG_MODE_END,
	ARG_MODE_EMPTY,
	ARG_MODE_STRING,
	ARG_MODE_FLOAT
};

struct arg_t {
	char short_name;
	const char * long_name;
	enum arg_mode mode;
	bool (* check)(struct arg_t *);
	bool present;
	const char * value;
	float float_value;
};

#define ARG_END { '\0', NULL, ARG_MODE_END, NULL, false, NULL, 0 }
#define ARG_EMPTY(s, l, c) { s, l, ARG_MODE_EMPTY, c, false, NULL, 0 }
#define ARG_STRING(s, l, c, v) { s, l, ARG_MODE_STRING, c, false, v, 0 }
#define ARG_FLOAT(s, l, c, v) { s, l, ARG_MODE_FLOAT, c, false, #v, v }

static bool handle_arg(struct arg_t * arg, const char * value, bool * consume_next) {
	arg->present = true;
	if (arg->mode == ARG_MODE_END || arg->mode == ARG_MODE_EMPTY) {
		*consume_next = false;
	} else {
		if (!value) {
			if (arg->long_name) {
				fprintf(stderr, "Option '--%s' requires an argument.\n",
					arg->long_name);
			} else {
				fprintf(stderr, "Option '-%c' requires an argument.\n",
					arg->short_name);
			}
			return false;
		}
		arg->value = value;
		if (arg->mode == ARG_MODE_EMPTY) {
			arg->float_value = 1;
		} else if (arg->mode == ARG_MODE_FLOAT) {
			char * tmp = NULL;
			arg->float_value = strtof(value, &tmp);
			if (tmp && (tmp == value || tmp[0])) {
				if (value[0]) {
					fprintf(stderr, "Invalid number: %s.\n", value);
				} else {
					fprintf(stderr, "Invalid number.\n");
				}
				return false;
			}
		}
		*consume_next = true;
	}
	return !arg->check || arg->check(arg);
}

static bool parse_args(int argc, char ** argv, struct arg_t * args) {
	int i, j, k;
	bool consume_next;
	for (i = 0; i < argc; i++) {
		char * argi = argv[i];
		int len = strlen(argi);
		if (argi[0] == '-' && argi[1] == '-') {
			const char * name = &argi[2];
			const char * tmp = strstr(name, "=");
			size_t nlen = tmp ? (size_t) (tmp - name) : strlen(name);
			bool found = false;
			for (k = 0; args && args[k].mode != ARG_MODE_END; k++) {
				if (args[k].long_name && strlen(args[k].long_name) == nlen &&
					strn_eq_const(name, args[k].long_name, nlen)) {
					found = true;
					if (tmp) {
						if (!handle_arg(&args[k], &tmp[1], &consume_next)) {
							return false;
						}
						if (!consume_next) {
							fprintf(stderr, "Option '--%s' doesn't allow an"
								" argument.\n", args[k].long_name);
							return false;
						}
					} else {
						if (!handle_arg(&args[k], i + 1 >= argc
							? NULL : argv[i + 1], &consume_next)) {
							return false;
						}
						if (consume_next) {
							i++;
						}
					}
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "Invalid option: '--%s'.\n", name);
				return false;
			}
		} else if (argi[0] == '-' && argi[1]) {
			for (j = 1; j < len; j++) {
				bool found = false;
				for (k = 0; args && args[k].mode != ARG_MODE_END; k++) {
					if (argi[j] == args[k].short_name) {
						found = true;
						if (j + 1 >= len) {
							if (!handle_arg(&args[k], i + 1 >= argc
								? NULL : argv[i + 1], &consume_next)) {
								return false;
							}
							if (consume_next) {
								i++;
							}
						} else {
							if (!handle_arg(&args[k], &argi[j + 1],
								&consume_next)) {
								return false;
							}
							if (consume_next) {
								j = len;
							}
						}
						break;
					}
				}
				if (!found) {
					fprintf(stderr, "Invalid option: '-%c'.\n", argi[j]);
					return false;
				}
			}
		} else {
			fprintf(stderr, "Invalid option: '%s'.\n", argi);
			return false;
		}
	}
	return true;
}

static struct arg_t * arg(struct arg_t * args, const char * name) {
	int i;
	bool s = strlen(name) == 1;
	for (i = 0; args && args[i].mode != ARG_MODE_END; i++) {
		if ((s && args[i].short_name == name[0]) ||
			(!s && args[i].long_name && !strcmp(name, args[i].long_name))) {
			return &args[i];
		}
	}
	return NULL;
}

static bool arg_check_measure_format(struct arg_t * arg) {
	if (strcmp(arg->value, "terminal") && strcmp(arg->value, "csv")) {
		fprintf(stderr, "Available formats: terminal, csv.\n");
		return false;
	}
	return true;
}

static bool arg_check_measure_sleep(struct arg_t * arg) {
	if (arg->float_value <= 0) {
		fprintf(stderr, "Sleep interval should be greater than 0.\n");
		return false;
	}
	return true;
}

int main(int argc, char ** argv) {
	if (argc >= 2 && !strcmp(argv[1], "read")) {
		return parse_args(argc - 2, &argv[2], NULL) &&
			read_apply_mode(false, false) ? 0 : 1;
	} else if (argc >= 2 && !strcmp(argv[1], "apply")) {
		struct arg_t args[2] = {
			ARG_EMPTY('t', "trigger", NULL),
			ARG_END
		};
		return parse_args(argc - 2, &argv[2], args) &&
			read_apply_mode(true, arg(args, "trigger")->present) ? 0 : 1;
	} else if (argc >= 2 && !strcmp(argv[1], "measure")) {
		struct arg_t args[3] = {
			ARG_STRING('f', "format", arg_check_measure_format, "terminal"),
			ARG_FLOAT('s', "sleep", arg_check_measure_sleep, 1),
			ARG_END
		};
		return parse_args(argc - 2, &argv[2], args) &&
			measure_mode(!strcmp("csv", arg(args, "format")->value),
				arg(args, "sleep")->float_value) ? 0 : 1;
	} else if (argc >= 2 && !strcmp(argv[1], "daemon")) {
		return parse_args(argc - 2, &argv[2], NULL) &&
			daemon_mode() ? 0 : 1;
	} else if (parse_args(argc - 1, &argv[1], NULL)) {
		fprintf(stderr,
			"Usage: intel-undervolt MODE [OPTION]...\n"
			"  read                     read and display current values\n"
			"  apply                    apply values from config file\n"
			"  measure                  measure power consumption\n"
			"    -f, --format <format>  output format (terminal, csv)\n"
			"    -s, --sleep <interval> sleep interval in seconds\n"
			"  daemon                   run in daemon mode\n");
		return argc == 1 ? 0 : 1;
	} else {
		return 1;
	}
}
