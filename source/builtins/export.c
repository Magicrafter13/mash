#define _GNU_SOURCE // strchrnul
#include "mash.h"
#include <stdio.h>
#include <string.h>

void b_export(uint8_t *cmd_exit, char **argv, int argc, Source *source, Variables *vars) {
	if (argc > 1) {
		char *equal_addr = strchrnul(argv[1], '='), *value = NULL;
		if (equal_addr[0] == '=') {
			equal_addr[0] = '\0';
			value = &equal_addr[1];
		}
		if (setvar(vars, argv[1], value, 1) == -1) {
			fprintf(stderr, "%s: export: %m\n", source->argv[0]);
			*cmd_exit = 1;
		}
	}
	else
		*cmd_exit = 1;
}
