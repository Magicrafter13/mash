#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <inttypes.h>

#include "suftree.h"
#include "command.h"

uint8_t export(size_t, char**), help(size_t, char**);

#define BUILTIN_COUNT 2

char *const BUILTIN[BUILTIN_COUNT] = {
	"export",
	"help"
};

uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, char**) = {
	export,
	help
};

extern char **environ;

int main(int argc, char *argv[]) {
	// Determine shell type
	int login = argv[0][0] == '-';
	fprintf(stdout, "This mash is %sa login shell.\n", login ? "" : "not ");

	char *tty = ttyname(fileno(stdin));
	if (tty == NULL && errno != ENOTTY) {
		fprintf(stderr, "%m\n");
		return errno;
	}
	int interactive = argc == 1 && tty != NULL, sourcing = 0;
	fprintf(stdout, "This mash is %sinteractive.\n", interactive ? "" : "non-");

	// Initialize
	SufTree builtins = suftreeInit(BUILTIN[0], 0);
	for (size_t b = 1; b < BUILTIN_COUNT; b++)
		suftreeAdd(&builtins, BUILTIN[b], b);
	commandSetBuiltins(&builtins);

	FILE *input_source = stdin;

	const uid_t UID = getuid();
	struct passwd *PASSWD = getpwuid(UID);
	char *config_file;
	if (interactive) { // Source config
		char *temp = getenv("XDG_CONFIG_HOME");
		if (temp == NULL) {
			size_t len = strlen(PASSWD->pw_dir);
			config_file = calloc(len + 25, sizeof (char));
			strcpy(config_file, PASSWD->pw_dir);
			strcpy(&config_file[len], "/.config/mash/config.mash");
		}
		else {
			size_t len = strlen(temp);
			config_file = calloc(len + 17, sizeof (char));
			strcpy(config_file, temp);
			strcpy(&config_file[len], "/mash/config.mash");
		}
		if (access(config_file, R_OK) == 0) {
			sourcing = 1;
			input_source = fopen(config_file, "r");
			if (input_source == NULL) {
				fprintf(stderr, "%m\n");
				return errno;
			}
		}
	}
	else {
		if (argc > 1) {
			input_source = fopen(argv[1], "r");
			if (input_source == NULL) {
				fprintf(stderr, "%m\n");
				return errno;
			}
		}
	}

	// User prompt (main loop)
	// TODO make a struct for a command - can have an array of command history to call back on!
	Command cmd = commandInit();

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	for (;;) {
		// Present prompt and read command
		if (interactive && !sourcing)
			fprintf(stdout, "$ ");
		if (commandRead(&cmd, input_source) == -1) {
			if (errno > 11) {
				int err = errno;
				free(cmd.c_buf);
				suftreeFree(builtins.sf_gt);
				suftreeFree(builtins.sf_eq);
				suftreeFree(builtins.sf_lt);
				fprintf(stderr, "%s\n", strerror(err));
				return err;
			}
			// EOF
			if (sourcing) {
				sourcing = 0;
				fclose(input_source);
				input_source = stdin;
				continue;
			}
			if (!interactive)
				fclose(input_source);
			break;
		}

		// Execute command
		if (cmd.c_type == CMD_EMPTY)
			continue;
		if (cmd.c_type == CMD_BUILTIN) {
			fprintf(stdout, "Executing builtin '%s'\n", BUILTIN[cmd.c_builtin]);
			BUILTIN_FUNCTION[cmd.c_builtin](cmd.c_argc, cmd.c_argv);
			continue;
		}

		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			execvp(cmd.c_argv[0], cmd.c_argv);
			fprintf(stderr, "%m\n");
			exit(errno);
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			fprintf(stdout, "Command exited with %" PRIu8 ".\n", cmd_exit);
		}

		free(cmd.c_argv);
	}

	if (cmd.c_buf != NULL)
		free(cmd.c_buf);

	if (interactive)
		free(config_file);

	suftreeFree(builtins.sf_gt);
	suftreeFree(builtins.sf_eq);
	suftreeFree(builtins.sf_lt);

	fprintf(stdout, "\n");

	return cmd_exit;
}

uint8_t export(size_t argc, char *argv[]) {
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
		fprintf(stderr, "%m\n");
		return errno;
	}
	return 0;
}

uint8_t help(size_t argc, char *argv[]) {
	fprintf(stdout, "Not implemented yet.\n");
	return 0;
}
