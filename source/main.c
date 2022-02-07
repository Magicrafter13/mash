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

#define _VMAJOR 1
#define _VMINOR 0

char *expandArgument(struct _arg);
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

struct _alias {
	char *name;
	char *str;
	int argc;
	struct _arg *args;
};
typedef struct _alias Alias;

extern char **environ;

int cmd_stat;
uint8_t cmd_exit;

int main(int argc, char *argv[]) {
	// Determine shell type
	int login = argv[0][0] == '-';
	if (login) {
		fprintf(stderr, "This mash is a login shell.\n");
		fflush(stderr);
	}

	int interactive = argc == 1 && isatty(fileno(stdin)), sourcing = 0, subshell = 0;
	if (interactive) {
		fprintf(stderr, "This mash is interactive.\n");
		fflush(stderr);
	}

	char *subshell_cmd;

	// Initialize
	srandom(time(NULL));
	SufTree builtins = suftreeInit(BUILTIN[0], 0), aliases = suftreeInit("mmmm", 0);
	for (size_t b = 1; b < BUILTIN_COUNT; b++)
		suftreeAdd(&builtins, BUILTIN[b], b);
	size_t alias_count = 1;
	Alias *alias = malloc(sizeof (Alias));
	// TODO it's really stupid that we need at least one thing in the tree... should probably fix that but I'm lazy :)
	alias[0].name = strdup("mmmm");
	alias[0].str = strdup("mmmm");
	alias[0].argc = 1;
	alias[0].args = calloc(2, sizeof (struct _arg));
	alias[0].args[0] = (struct _arg){ .type = ARG_BASIC_STRING, .str = strdup("mmmm") };
	alias[0].args[1] = (struct _arg){ .type = ARG_NULL };

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
				cmd_exit = errno;
				goto exit_cleanup;
			}
		}
	}
	else {
		if (argc > 1) {
			// Check what type of argument was passed
			if (argv[1][0] == '-') {
				// Long argument
				if (argv[1][1] == '-') {
					if (!strcmp(&argv[1][2], "version")) {
						fprintf(stdout, "mash %u.%u\n", _VMAJOR, _VMINOR);
						goto exit_cleanup;
					}
				}
				// Regular argument
				else {
					char c;
					for (size_t i = 1; c = argv[1][i], c != '\0'; ++i) {
						switch (c) {
							case 'c':
								subshell = 1;
								subshell_cmd = argv[2];
								break;
							default:
								fprintf(stderr, "Unrecognized option '%c', ignoring.\n", c);
								fflush(stderr);
						}
					}
				}
			}
			else {
				// (Presumed) file name
				input_source = fopen(argv[1], "r");
				if (input_source == NULL) {
					fprintf(stderr, "%m\n");
					fflush(stderr);
					cmd_exit = errno;
					goto exit_cleanup;
				}
			}
		}
	}

	// User prompt (main loop)
	// TODO make a struct for a command - can have an array of command history to call back on!
	Command *cmd = NULL, *last_cmd = commandInit();
	if (subshell) {
		last_cmd->c_len = strlen(subshell_cmd);
		last_cmd->c_buf = subshell_cmd;
		input_source = NULL;
	}

	pid_t cmd_pid;

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
				if (subshell)
					break;
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
				fflush(stderr);
				cmd->c_buf[0] = '\0';
				cmd = NULL;
				continue;
			}
		}
		else {
			switch (cmd->c_type) {
				case CMD_FREED:
					fprintf(stderr, "CMD_FREED encountered!\n");
					fflush(stderr);
					break;
				case CMD_WHILE:
					cmd = !cmd_exit ? cmd->c_if_true : cmd->c_next;
					cmd_exit = 0;
					break;
				case CMD_IF:
					cmd = !cmd_exit ? cmd->c_if_true : cmd->c_if_false;
					cmd_exit = 0;
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
		if (cmd->c_type == CMD_EMPTY)
			continue;
		if (cmd->c_argc == 0) // TODO: do we need this...?
			continue;

		int flow_control = 0; // SHOULD ONLY BE ZERO OR ONE
		if (cmd->c_argv[0].type == ARG_BASIC_STRING)
			if (!strcmp(cmd->c_argv[0].str, "while") || !strcmp(cmd->c_argv[0].str, "if"))
				flow_control = 1;
		// Check if first arg is an alias
		size_t index;
check_alias:
		if (cmd->c_argv[0].type == ARG_BASIC_STRING && suftreeHas(&aliases, cmd->c_argv[0].str, &index)) {
			int temp_argc = cmd->c_argc;
			cmd->c_argc += alias[index].argc - 1;
			cmd->c_argv = reallocarray(cmd->c_argv, cmd->c_argc + 1, sizeof (struct _arg));
			cmd->c_argv[cmd->c_argc] = cmd->c_argv[temp_argc]; // Copy ARG_NULL
			// Delete alias arg and shift remaining args
			freeArg(cmd->c_argv[0]);
			for (size_t i = 0; i < temp_argc - 1; ++i)
				cmd->c_argv[cmd->c_argc - 1 - i] = cmd->c_argv[temp_argc - 1 - i];
			for (size_t i = 0; i < alias[index].argc; ++i)
				cmd->c_argv[i] = argdup(alias[index].args[i]);
			// Check if new argv[0] is the same as the alias name. If not, run through again. (Can't use while loop, since it's perfectly normal to alias ls to ls (but with extra args).)
			if (cmd->c_argv[0].type == ARG_BASIC_STRING && strcmp(cmd->c_argv[0].str, alias[index].name))
				goto check_alias;
		}
		// Expand command
		char *e_argv[cmd->c_argc + 1 - flow_control];
		for (size_t i = flow_control; i < cmd->c_argc; ++i) {
			char *full_arg = expandArgument(cmd->c_argv[i]);
			if (full_arg == NULL) {
				for (size_t e = 0; e < i; ++e)
					free(e_argv[e]);
				free(last_cmd->c_buf);
				commandFree(last_cmd);
				goto exit_cleanup; // Screw it, I'm using a goto and you can't stop me.
			}
			e_argv[i - flow_control] = full_arg;
		}
		e_argv[cmd->c_argc - flow_control] = NULL;

		/*fprintf(stderr, "Execing:\n");
		for (size_t i = 0; i < cmd->c_argc; ++i)
			fprintf(stderr, "%s ", e_argv[i]);
		fprintf(stderr, "\n");*/

		// Check for alias
		if (!strcmp(e_argv[0], "alias")) {
			cmd_exit = 0;
			// List all aliases
			if (e_argv[1] == NULL)
				for (size_t i = 0; i < alias_count; ++i)
					fprintf(stdout, "%s=%s\n", alias[i].name, alias[i].args[0].str);
			// List or set one alias
			else {
				char *equal_addr = strchr(e_argv[1], '=');
				size_t index;

				// No equal sign, means to show an alias
				if (equal_addr == NULL) {
					if (suftreeHas(&aliases, e_argv[1], &index))
						fprintf(stdout, "%s=%s\n", alias[index].name, alias[index].str);
					else
						cmd_exit = 1;
					for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
						free(e_argv[v]);
					continue;
				}

				size_t equals = equal_addr - e_argv[1];
				// Nothing to the left of the equal sign
				if (equals == 0) {
					cmd_exit = 1;
					for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
						free(e_argv[v]);
					continue;
				}

				e_argv[1][equals] = '\0';
				char *a_str = strdup(&e_argv[1][equals + 1]);
				// Alias already exists, so we only need to change its args
				if (suftreeHas(&aliases, e_argv[1], &index)) {
					free(alias[index].str);
					for (size_t i = 0; i <= alias[index].argc; ++i)
						freeArg(alias[index].args[i]);
					free(alias[index].args);
				}
				// New alias
				else {
					char *a_name = strdup(e_argv[1]);
					suftreeAdd(&aliases, a_name, alias_count);
					alias = reallocarray(alias, alias_count + 1, sizeof (Alias));
					index = alias_count++;
					alias[index].name = a_name;
				}

				alias[index].str = a_str;

				// Parse string into args (so we don't have to do that every single time the alias is called)
				Command temp;
				temp.c_size = (temp.c_len = strlen(a_str)) + 1;
				temp.c_buf = a_str;
				commandParse(&temp, NULL);
				alias[index].argc = temp.c_argc;
				alias[index].args = temp.c_argv;
			}
			for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
				free(e_argv[v]);
			continue;
		}

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
			/*fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
			fflush(stderr);*/
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
			/*if (interactive && !sourcing) {
				if (cmd->c_next == NULL) {
					fprintf(stderr, "Command exited with %" PRIu8 ".\n", cmd_exit);
					fflush(stderr);
				}
			}*/
			for (size_t i = 0; i < cmd->c_argc - flow_control; ++i)
				free(e_argv[i]);
		}
	}

	if (!subshell && last_cmd->c_buf != NULL)
		free(last_cmd->c_buf);
	commandFree(last_cmd);

exit_cleanup:

	if (sourcing)
		fclose(input_source);

	if (interactive)
		free(config_file);

	for (size_t i = 0; i < alias_count; ++i) {
		for (size_t a = 0; a <= alias[i].argc; ++a)
			freeArg(alias[i].args[a]);
		free(alias[i].args);
		free(alias[i].str);
		free(alias[i].name);
	}
	free(alias);

	suftreeFree(aliases.sf_gt);
	suftreeFree(aliases.sf_eq);
	suftreeFree(aliases.sf_lt);

	suftreeFree(builtins.sf_gt);
	suftreeFree(builtins.sf_eq);
	suftreeFree(builtins.sf_lt);

	/*fprintf(stderr, "\n");
	fflush(stderr);*/

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
			else if (!strcmp(arg.str, "?")) {
				char number[4];
				sprintf(number, "%"PRIu8, cmd_exit);
				return strdup(number);
			}
			else if (!strcmp(arg.str, "$")) {
				char number[21];
				sprintf(number, "%ld", (long)getpid());
				return strdup(number);
			}

			char *value = getenv(arg.str);
			return strdup(value == NULL ? "" : value);
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL: {
			size_t path_size = 0;
			char *temp_dir = getenv("TMPDIR");
			if (temp_dir == NULL)
				path_size = 17;
			else
				path_size = strlen(temp_dir) + 13;

			char template[path_size]; // TODO: instead of hardcoding /tmp, first try to read $TMPDIR
			template[0] = '\0';
			if (temp_dir == NULL)
				strcat(template, "/tmp");
			else
				strcat(template, temp_dir);
			strcat(template, "/mash.XXXXXX");

			int sub_stdout = mkstemp(template); // Create temporary file, which we will redirect the output to.
			if (sub_stdout == -1) {
				fprintf(stderr, "%m\n");
				fflush(stderr);
				return NULL;
			}

			pid_t subshell_pid = fork();
			// Run subshell
			if (subshell_pid == 0) {
				dup2(sub_stdout, STDOUT_FILENO);
				close(STDIN_FILENO);
				char *argv[4] = {
					"mash", // "/proc/self/exe"
					"-c",
					arg.str,
					NULL
				};
				int err = main(3, argv);
				/*execvp(argv[0], argv);
				fprintf(stderr, "%m\n");
				fflush(stderr);*/

				close(sub_stdout);

				errno = err;
				return NULL;
			}
			// Wait for subshell to finish,
			waitpid(subshell_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);

			// Allocate memory for the file
			off_t size = lseek(sub_stdout, 0, SEEK_END);
			lseek(sub_stdout, 0, SEEK_SET);
			char *sub_output = calloc(size + 1, sizeof (char));
			size_t out_end = 0;
			char buffer[512];
			memset(buffer, 0, 512);
			int read_return;
			// TODO: if a word is caught on the 512 byte boundary it will be split into 2 arguments - need to fix this (and already know how...)
			while ((read_return = read(sub_stdout, buffer, 512)) > 0 ) {
				// Regular subshells replace newlines and tabs with spaces, and truncate all spaces longer than 1.
				if (arg.type == ARG_SUBSHELL) {
					for (size_t i = 0; i < read_return; ++i) {
						int whitespace = 0;
						while (buffer[i] == '\n' || buffer[i] == '\t' || buffer[i] == ' ') {
							whitespace = 1;
							++i;
						}
						if (whitespace && out_end > 0) {
							if (i >= read_return)
								continue;
							sub_output[out_end++] = ' ';
						}
						sub_output[out_end++] = buffer[i];
					}
				}
				// But quoted subshells allow you to get the exact output from the command, unchanged
				else {
					strncpy(&sub_output[out_end], buffer, read_return);
					out_end += read_return;
				}
			}
			close(sub_stdout);
			unlink(template); // TODO: consider stdio's tmpfile?
			if (out_end > 0 && (sub_output[out_end - 1] == ' ' || sub_output[out_end - 1] == '\n'))
				--out_end;
			//while (out_end < size)
			sub_output[out_end] = '\0';
			return sub_output;
		}
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
	// TODO go to home dir when no args are given
	char **argv = (char**)ptr;

	if (chdir(argv[1]) == -1) {
		int err = errno;
		fprintf(stderr, "%m\n");
		return err;
	}

	return 0;
}
