#include <errno.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "suftree.h"
#include "command.h"

uint8_t export(size_t, void**), help(size_t, void**), mash_if(size_t, void**);

#define BUILTIN_COUNT 3

char *const BUILTIN[BUILTIN_COUNT] = {
	"export",
	"help",
	"if"
};

uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, void**) = {
	export,
	help,
	mash_if
};

extern char **environ;

int main(int argc, char *argv[]) {
	// Determine shell type
	int login = argv[0][0] == '-';
	fprintf(stderr, "This mash is %sa login shell.\n", login ? "" : "not ");
	fflush(stderr);

	int interactive = argc == 1 && isatty(fileno(stdin)), sourcing = 0;
	fprintf(stderr, "This mash is %sinteractive.\n", interactive ? "" : "non-");
	fflush(stderr);

	// Initialize
	srandom(time(NULL));
	SufTree builtins = suftreeInit(BUILTIN[0], 0);
	for (size_t b = 1; b < BUILTIN_COUNT; b++)
		suftreeAdd(&builtins, BUILTIN[b], b);
	commandSetBuiltins(&builtins);
	commandSetVarFunc(&getenv);

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
				fflush(stderr);
				return errno;
			}
		}
	}
	else {
		if (argc > 1) {
			input_source = fopen(argv[1], "r");
			if (input_source == NULL) {
				fprintf(stderr, "%m\n");
				fflush(stderr);
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
		if (cmd.c_next != NULL) {
			free(cmd.c_buf);
			Command *next = cmd.c_next;
			cmd = *next;
			free(next);
		}
		else {
			// Present prompt and read command
			if (interactive && !sourcing)
				fprintf(stderr, "$ ");
			fflush(stderr);

			if (commandRead(&cmd, input_source) == -1) {
				if (errno > 11) {
					int err = errno;
					free(cmd.c_buf);
					suftreeFree(builtins.sf_gt);
					suftreeFree(builtins.sf_eq);
					suftreeFree(builtins.sf_lt);
					fprintf(stderr, "%s\n", strerror(err));
					fflush(stderr);
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
		}

		// Execute command
		if (cmd.c_type == CMD_EMPTY)
			continue;
		if (cmd.c_type == CMD_EXIT) {
			if (cmd.c_argc > 1) {
				int temp;
				sscanf(cmd.c_argv[1], "%u", &temp);
				cmd_exit = temp % 256;
			}
			else
				cmd_exit = 0;
			if (!sourcing)
				break;
			fseek(input_source, 0, SEEK_END);
			continue;
		}
		if (cmd.c_type == CMD_BUILTIN) {
			fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd.c_builtin]);
			fflush(stderr);
			BUILTIN_FUNCTION[cmd.c_builtin](cmd.c_argc, (void**)cmd.c_argv);
			continue;
		}

		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			execvp(cmd.c_argv[0], cmd.c_argv);
			fprintf(stderr, "%m\n");
			fflush(stderr);
			exit(errno);
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			if (interactive && !sourcing) {
				if (cmd.c_next == NULL) {
					fprintf(stderr, "Command exited with %" PRIu8 ".\n", cmd_exit);
					fflush(stderr);
				}
			}
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

	fprintf(stderr, "\n");
	fflush(stderr);

	/*if (!interactive)
		fclose(stdout);*/
	return cmd_exit;
}

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
		fprintf(stderr, "%m\n");
		fflush(stderr);
		return errno;
	}
	return 0;
}

uint8_t help(size_t argc, void **ptr) {
	//char **argv = (char**)ptr;

	fprintf(stderr, "Not implemented yet.\n");
	fflush(stderr);
	return 0;
}

uint8_t mash_if(size_t argc, void **ptr) {
	Command **cmds = (Command**)ptr;

	// Condition to test
	Command *test = cmds[0];

	// Run test

	// If condition was true
	if (1) {
		for (size_t i = 1; i < argc; ++i) {
		}
	}
	// If condition was false
	else {
		for (size_t i = argc; cmds[i] != NULL; ++i) {
		}
	}

	return 0;
}
