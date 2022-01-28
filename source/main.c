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

char *expandArgument(struct _arg, uint8_t);
uint8_t export(size_t, void**), help(size_t, void**), cd(size_t, void**), mash_if(size_t, void**);

#define BUILTIN_COUNT 3

char *const BUILTIN[BUILTIN_COUNT] = {
	"export",
	"help",
	"cd"
};

uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, void**) = {
	export,
	help,
	cd
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
	char *config_file = NULL;
	if (interactive) { // Source config
		char *temp = getenv("XDG_CONFIG_HOME");
		if (temp == NULL) {
			size_t len = strlen(PASSWD->pw_dir);
			config_file = calloc(len + 26, sizeof (char));
			strcpy(config_file, PASSWD->pw_dir);
			strcpy(&config_file[len], "/.config/mash/config.mash");
		}
		else {
			size_t len = strlen(temp);
			config_file = calloc(len + 18, sizeof (char));
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
	Command *cmd = NULL, *last_cmd = commandInit();

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	size_t cmd_builtin;

	for (;;) {
		if (cmd == NULL) {
			if (last_cmd->c_type == CMD_WHILE) {
				cmd = commandInit();
				cmd->c_buf = last_cmd->c_buf;
				cmd->c_size = last_cmd->c_size;
				commandFree(last_cmd);
			}
			else {
				cmd = last_cmd;
				if (cmd->c_next != NULL) {
					commandFree(cmd->c_next);
					cmd->c_next = NULL;
				}
				if (cmd->c_if_true != NULL) {
					commandFree(cmd->c_if_true);
					cmd->c_if_true = NULL;
				}
				if (cmd->c_if_false != NULL) {
					commandFree(cmd->c_if_false);
					cmd->c_if_false = NULL;
				}
				if (cmd->c_argv != NULL) {
					for (size_t i = 0; i < cmd->c_argc; ++i)
						freeArg(cmd->c_argv[i]);
					free(cmd->c_argv);
					cmd->c_argv = NULL;
				}
			}

			// Present prompt and read command
			if (interactive && !sourcing)
				fprintf(stderr, "$ ");
			fflush(stderr);

			int parse_result = commandParse(cmd, input_source);
			last_cmd = cmd;
			if (parse_result == -1) {
				if (errno > 11) {
					int err = errno;
					fprintf(stderr, "%s\n", strerror(err));
					fflush(stderr);
					cmd_exit = err;
					break;
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
				cmd->c_buf[0] = '\0';
				cmd = NULL;
				continue;
			}
			if (cmd->c_type == CMD_EMPTY)
				continue;
		}
		else {
			switch (cmd->c_type) {
				case CMD_FREED:
					fprintf(stderr, "CMD_FREED encountered!\n");
					break;
				case CMD_WHILE:
					cmd = !cmd_exit ? cmd->c_if_true : cmd->c_next;
					break;
				case CMD_IF:
					cmd = !cmd_exit ? cmd->c_if_true : cmd->c_if_false;
					break;
				case CMD_EMPTY:
				case CMD_REGULAR:
				default:
					cmd = cmd->c_next;
			}
			if (cmd == NULL)
				continue;
		}

		/*
		 * Execute command
		 */

		// Empty/blank command
		if (cmd->c_argc == 0)
			continue;

		int flow_control = 0; // SHOULD ONLY BE ZERO OR ONE
		if (cmd->c_argv[0].type == ARG_BASIC_STRING)
			if (!strcmp(cmd->c_argv[0].str, "while") || !strcmp(cmd->c_argv[0].str, "if"))
				flow_control = 1;
		// Expand command
		char *e_argv[cmd->c_argc + 1 - flow_control];
		for (size_t i = flow_control; i < cmd->c_argc; ++i)
			e_argv[i - flow_control] = expandArgument(cmd->c_argv[i], cmd_exit);
		e_argv[cmd->c_argc - flow_control] = NULL;
		/*fprintf(stderr, "Execing:\n");
		for (size_t i = 0; i < cmd->c_argc; ++i)
			fprintf(stderr, "%s ", e_argv[i]);
		fprintf(stderr, "\n");*/

		// Exit shell
		if (!strcmp(e_argv[0], "exit")) {
			if (cmd->c_argc > 1 + flow_control) {
				int temp;
				sscanf(e_argv[1], "%u", &temp);
				cmd_exit = temp % 256;
			}
			else
				cmd_exit = 0;

			for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
				free(e_argv[v]);

			if (!sourcing)
				break;
			fseek(input_source, 0, SEEK_END);
			continue;
		}

		// Execute builtin
		if (suftreeHas(&builtins, e_argv[0], &cmd_builtin)) {
			fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
			fflush(stderr);
			BUILTIN_FUNCTION[cmd_builtin](cmd->c_argc - flow_control, (void**)e_argv);

			for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
				free(e_argv[v]);
			continue;
		}

		// Execute regular command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			execvp(e_argv[0], e_argv);
			fprintf(stderr, "%m\n");
			fflush(stderr);

			// Free memory (I wish this wasn't all duplicated in the child to begin with...)
			for (size_t i = 0; i < cmd->c_argc - flow_control; ++i)
				free(e_argv[i]);
			cmd_exit = errno;
			break;
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
			for (size_t i = 0; i < cmd->c_argc - flow_control; ++i)
				free(e_argv[i]);
		}
	}

	if (cmd->c_buf != NULL)
		free(cmd->c_buf);
	commandFree(cmd);

	if (sourcing)
		fclose(input_source);

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

char *expandArgument(struct _arg arg, uint8_t cmd_exit) {
	switch (arg.type) {
		case ARG_BASIC_STRING:
			return strdup(arg.str);
		case ARG_VARIABLE:
			if (!strcmp(arg.str, "RANDOM")) {
				char number[12];
				sprintf(number, "%lu", random());
				return strdup(number);
			}
			else if (!strcmp(arg.str, "?")) {
				char number[3];
				sprintf(number, "%"PRIu8, cmd_exit);
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
				char *e = expandArgument(arg.sub[i], cmd_exit);
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
