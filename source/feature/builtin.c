#define _POSIX_C_SOURCE 200112L
#include "mash.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint8_t export(size_t argc, void **ptr) {
	char **argv = (char**)ptr;

	if (argc < 2)
		return 1;

	size_t var_len;
	int overwrite = 0;
	for (var_len = 0; argv[1][var_len] != '\0'; var_len++) {
		if (argv[1][var_len] == '=') {
			overwrite = 1;
			break;
		}
	}
	if (!var_len)
		return 1;

	size_t val_len = strlen(&argv[1][var_len + 1]);
	char variable[var_len + 1], value[val_len + 1];
	strncpy(variable, argv[1], var_len);
	variable[var_len] = '\0';
	if (val_len)
		strcpy(value, &argv[1][var_len + 1]);
	value[val_len] = '\0';

	if (setenv(variable, value, overwrite) == -1) {
		int err = errno;
		fprintf(stderr, "%m\n");
		return err;
	}
	return 0;
}

uint8_t help(size_t argc, void **ptr) {
	//char **argv = (char**)ptr;

	fprintf(stderr, "Not implemented yet.\n");
	return 0;
}

uint8_t cd(size_t argc, void **ptr) {
	// TODO go to home dir when no args are given
	char **argv = (char**)ptr;

	if (chdir(argv[1]) == -1) {
		int err = errno;
		fprintf(stderr, "%m\n");
		return err;
	}

	return 0;
}
