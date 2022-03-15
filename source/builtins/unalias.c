#include "mash.h"
#include "command.h"
#include <stdio.h>

void b_unalias(uint8_t *cmd_exit, char **argv, int argc, AliasMap *aliases) {
	for (size_t v = 1; v < argc; ++v) {
		if (!aliasRemove(aliases, argv[v])) {
			fprintf(stderr, "No such alias `%s'\n", argv[v]);
			*cmd_exit = 1;
		}
	}
}
