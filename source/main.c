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

char *expandArgument(struct _arg);
uint8_t export(size_t, void**), help(size_t, void**), cd(size_t, void**), mash_if(size_t, void**);

#define BUILTIN_COUNT 4

char *const BUILTIN[BUILTIN_COUNT] = {
	"export",
	"help",
	"cd",
	"if"
};

uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, void**) = {
	export,
	help,
	cd,
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
	Command *cmd = commandInit();

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	for (;;) {
		if (cmd->c_next != NULL) {
			Command *next = cmd->c_next;
			free(cmd);
			cmd = next;
		}
		else {
			// Present prompt and read command
			if (interactive && !sourcing)
				fprintf(stderr, "$ ");
			fflush(stderr);

			int parse_result = commandRead(cmd, input_source);
			if (parse_result == -1) {
				if (errno > 11) {
					int err = errno;
					free(cmd->c_buf);
					while (cmd != NULL) {
						Command *temp_cmd = cmd->c_next;
						for (size_t v = 0; v < cmd->c_argc; ++v)
							freeArg(cmd->c_argv[v]);
						free(cmd->c_argv);
						free(cmd);
						cmd = temp_cmd;
					}
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
			if (parse_result) {
				fprintf(stderr, "   %*s\n", (int)cmd->c_len, "^");
				fprintf(stderr, "%s: parse error near `%c'\n", argv[0], cmd->c_buf[0]);
				while (cmd->c_next != NULL) {
					Command *temp_cmd = cmd->c_next;
					for (size_t v = 0; v < cmd->c_argc; ++v)
						freeArg(cmd->c_argv[v]);
					free(cmd->c_argv);
					free(cmd);
					cmd = temp_cmd;
				}
				continue;
			}
		}

		/*
		 * Execute command
		 */
		// Empty/blank command
		if (cmd->c_argc == 0)
			continue;

		// Expand command
		char *e_argv[cmd->c_argc + 1];
		for (size_t i = 0; i < cmd->c_argc; ++i)
			e_argv[i] = expandArgument(cmd->c_argv[i]);
		e_argv[cmd->c_argc] = NULL;

		// Exit shell
		if (!strcmp(e_argv[0], "exit")) {
			if (cmd->c_argc > 1) {
				int temp;
				sscanf(cmd->c_argv[1].str, "%u", &temp);
				cmd_exit = temp % 256;
			}
			else
				cmd_exit = 0;

			for (size_t v = 0; v < cmd->c_argc; ++v)
				freeArg(cmd->c_argv[v]);
			free(cmd->c_argv);

			if (!sourcing)
				break;
			fseek(input_source, 0, SEEK_END);
			continue;
		}

		// Execute builtin
		if (suftreeHas(&builtins, e_argv[0], &cmd->c_builtin)) {
			fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd->c_builtin]);
			fflush(stderr);
			BUILTIN_FUNCTION[cmd->c_builtin](cmd->c_argc, (void**)e_argv);

			for (size_t v = 0; v < cmd->c_argc; ++v) {
				free(e_argv[v]);
				freeArg(cmd->c_argv[v]);
			}
			free(cmd->c_argv);
			continue;
		}

		// Execute regular command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			/*fprintf(stderr, "Execing:\n");
			for (size_t i = 0; i < cmd->c_argc; ++i)
				fprintf(stderr, "%s ", e_argv[i]);
			fprintf(stderr, "\n");*/
			execvp(e_argv[0], e_argv);
			fprintf(stderr, "%m\n");
			fflush(stderr);
			for (size_t i = 0; i < cmd->c_argc; ++i)
				free(e_argv[i]);
			exit(errno);
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			if (interactive && !sourcing) {
				if (cmd->c_next == NULL) {
					fprintf(stderr, "Command exited with %" PRIu8 ".\n", cmd_exit);
					fflush(stderr);
				}
			}
		}

		for (size_t v = 0; v < cmd->c_argc; ++v)
			freeArg(cmd->c_argv[v]);
		free(cmd->c_argv);
	}

	/*for (size_t v = 0; v < cmd->c_argc; ++v)
		free(cmd->c_argv[v]);
	free(cmd->c_argv);*/
	if (cmd->c_buf != NULL)
		free(cmd->c_buf);
	free(cmd);

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

char *expandArgument(struct _arg arg) {
	switch (arg.type) {
		case ARG_BASIC_STRING:
			return strdup(arg.str);
		case ARG_VARIABLE:
			if (!strcmp(arg.str, "RANDOM")) {
				char number[12];
				sprintf(number, "%lu", random());
				return strdup(number);
			}

			char *value = getenv(arg.str);
			return strdup(value == NULL ? "" : value);
		case ARG_COMPLEX_STRING: {
			size_t sub_count = 0;
			while (arg.sub[sub_count].type != ARG_NULL)
				++sub_count;
			char *argv[sub_count];
			for (size_t i = 0; i < sub_count; ++i) {
				char *e = expandArgument(arg.sub[i]);
				if (e == NULL) {
					for (size_t f = 0; f < i; ++f)
						free(argv[f]);
					return NULL;
				}
				argv[i] = e;
			}

			// Find length of strings together
			size_t length = 0;
			for (size_t i = 0; i < sub_count; ++i)
				length += strlen(argv[i]);
			char *expanded_string = calloc(length + 1, sizeof (char));
			expanded_string[0] = '\0';
			for (size_t i = 0; i < sub_count; ++i) {
				strcat(expanded_string, argv[i]);
				free(argv[i]);
			}
			/*char fmt[sub_count * 2];
			for (size_t i = 0; i < sub_count; ++i) {
				fmt[i * 2] = '%';
				fmt[i * 2 + 1] = 's';
			}
			vasprintf(&expanded_string, fmt, argv);*/
			return expanded_string;
		}
		case ARG_NULL:
		default:
			return NULL;
	}
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

uint8_t cd(size_t argc, void **ptr) {
	char **argv = (char**)ptr;

	if (chdir(argv[1]) == -1) {
		int err = errno;
		fprintf(stderr, "%m\n");
		return err;
	}

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
