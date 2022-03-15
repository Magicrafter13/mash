#include "mash.h"
#include <stdio.h>

void b_dot(uint8_t *cmd_exit, char **argv, int argc, Source **_source) {
	FILE *script = fopen(argv[1], "r");
	if (script != NULL)
		*_source = sourceAdd(*_source, script, argc - 1, &argv[1]);
	else {
		fprintf(stderr, "%s: .: %m\n", (*_source)->argv[0]);
		*cmd_exit = 1;
	}
}
