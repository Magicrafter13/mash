#define _POSIX_C_SOURCE 200112L // setenv, PATH_MAX
#include "mash.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
//#include <string.h>
#include <unistd.h>

void b_cd(uint8_t *cmd_exit, char **argv, int argc) {
	char *newdir = NULL;
	switch (argc) {
		case 1: {
			char *env_home = getenv("HOME"); // TODO: replace with getvar - I'd do this now, but I think I'll be retiring the builtins soon
			if (env_home == NULL) {
				// Step 1
				fputs("HOME not set.\n", stderr);
				*cmd_exit = 1;
				return;
			}
			// Step 2
			newdir = env_home;
			break;
		}
		case 2:
			newdir = argv[1];
			break;
		default:
			*cmd_exit = 1;
			return;
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
		*cmd_exit = errno;
		fprintf(stderr, "%m\n");
		return;
	}
	char newpath[PATH_MAX];
	setenv("PWD", getcwd(newpath, PATH_MAX), 1);

	*cmd_exit = 0;
}
