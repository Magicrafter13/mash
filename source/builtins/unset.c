#include "mash.h"
#include <stdio.h>

void b_unset(uint8_t *cmd_exit, char **argv, Source *source, Variables *vars) {
	for (size_t i = 1; argv[i] != NULL; ++i) {
		if (unsetvar(vars, argv[i]) == -1) {
			fprintf(stderr, "%s: unset: %m\n", source->argv[0]);
			*cmd_exit = 1;
		}
	}
}
