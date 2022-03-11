#define _POSIX_C_SOURCE 200112L
#include "mash.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *const BUILTIN[BUILTIN_COUNT] = {
	"help",
	"cd"
};

uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, void**) = {
	help,
	cd
};

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

	fputs("Not implemented yet.\n", stderr);
	return 0;
}

uint8_t cd(size_t argc, void **ptr) {
	char **argv = (char**)ptr;
	char *newdir = NULL;
	switch (argc) {
		case 1: {
			char *env_home = getenv("HOME"); // TODO: replace with getvar - I'd do this now, but I think I'll be retiring the builtins soon
			if (env_home == NULL) {
				// Step 1
				fputs("HOME not set.\n", stderr);
				return 1;
			}
			// Step 2
			newdir = env_home;
			break;
		}
		case 2:
			newdir = argv[1];
			break;
		default:
			return 1;
	}

	// Step 3
	/*if (newdir[0] != '/') {
		// Step 4
		char *component_end = strchr(newdir, '/');
		if (!(newdir[0] == '.' && (component_end == &newdir[1] || (component_end == &newdir[2] && newdir[1] == '.')))) {
			// Step 5
			// TODO implement Step 5
		}

		// Step 6
		// Already done?
	}*/

	// Step 7
	/*if (strcmp(newdir, "-P")) {
		// TODO implement Step 8
		// TODO implement Step 9
	}*/

	// Step 10
	if (chdir(newdir) == -1) {
		int err = errno;
		fprintf(stderr, "%m\n");
		return err;
	}
	char newpath[PATH_MAX];
	setenv("PWD", getcwd(newpath, PATH_MAX), 1);

	return 0;
}
