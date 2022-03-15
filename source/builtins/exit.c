#include "mash.h"
#include <stdio.h>
#include <stdlib.h>

CmdSignal b_exit(uint8_t *cmd_exit, char **argv, int argc, Source *source) {
	if (argc > 1) {
		int temp;
		sscanf(argv[1], "%u", &temp);
		*cmd_exit = temp % 256;
	}
	else
		*cmd_exit = 0;

	for (size_t v = 0; v < argc; ++v)
		free(argv[v]);

	if (source->input == stdin)
		return CSIG_EXIT;
	else
		fseek(source->input, 0, SEEK_END);
	return CSIG_DONE;
}
