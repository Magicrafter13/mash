#include "mash.h"
#include <stdio.h>

void b_shift(uint8_t *cmd_exit, char **argv, int argc, Source *source) {
	int amount = 1;
	if (argc > 1) {
		int left, right;
		sscanf(argv[1], "%n%d%n", &left, &amount, &right);
	}
	*cmd_exit = sourceShift(source, amount);
}
