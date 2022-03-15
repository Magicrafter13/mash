#include "command.h"
#include "mash.h"
#include <stdio.h>
#include <string.h>

void b_alias(uint8_t *cmd_exit, char **argv, Source *source, AliasMap *aliases) {
	*cmd_exit = 0;
	// List all aliases
	if (argv[1] == NULL)
		aliasList(aliases, stdout);
	// List or set one alias
	else {
		char *equal_addr = strchr(argv[1], '=');
		// No equal sign, means to show an alias
		if (equal_addr == NULL)
			*cmd_exit = aliasPrint(aliases, argv[1], stdout);
		else {
			size_t equals = equal_addr - argv[1];
			// Nothing to the left of the equal sign
			if (equals == 0)
				*cmd_exit = 1;
			else {
				argv[1][equals] = '\0';
				if (aliasAdd(aliases, argv[1], &argv[1][equals + 1]) == NULL) {
					fprintf(stderr, "%s: alias: error parsing string\n", source->argv[0]);
					*cmd_exit = 1;
				}
			}
		}
	}
}
