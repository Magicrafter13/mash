#define _POSIX_C_SOURCE 200809L // fileno, getline
#include "mash.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void b_read(uint8_t *cmd_exit, FILE *filein, char **argv, Source *source, Variables *vars) {
	*cmd_exit = 0;
	char *value = NULL;
	size_t size = 0, bytes_read;
	if (filein == NULL)
		bytes_read = getline(&value, &size, stdin);
	else { // Cursed code to keep FILE position and fd offset in sync...
		off_t offset = lseek(fileno(filein), 0, SEEK_CUR);
		bytes_read = getline(&value, &size, filein);
		if (bytes_read > 0)
			lseek(fileno(filein), offset + bytes_read, SEEK_SET);
	}
	if (bytes_read == -1) {
		if (filein == NULL)
			clearerr(stdin);
		*cmd_exit = 1;
	}
	else {
		if (value[bytes_read - 1] == '\n')
			value[bytes_read - 1] = '\0';
		if (argv[1] != NULL) {
			if (setvar(vars, argv[1], value, 0) == -1) {
				*cmd_exit = errno;
				fprintf(stderr, "%s: read: %m\n", source->argv[0]);
			}
		}
	}
	free(value);
}
