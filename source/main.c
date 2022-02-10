#define _POSIX_C_SOURCE 200809L // fileno, strdup, mkstemp, setenv
#define _DEFAULT_SOURCE // srandom
#include "mash.h"
#include "suftree.h"
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

char *expandArgument(struct _arg);
int mktmpfile(_Bool, char**);

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

int cmd_stat;
uint8_t cmd_exit;

int main(int argc, char *argv[]) {
	// Set stderr to be line buffered
	setvbuf(stderr, NULL, _IOLBF, 0);

	// Determine shell type
	int login = argv[0][0] == '-';
	if (login)
		fprintf(stderr, "This mash is a login shell.\n");

	int interactive = argc == 1 && isatty(fileno(stdin)), sourcing = 0, subshell = 0;
	if (interactive)
		fprintf(stderr, "This mash is interactive.\n");

	char *subshell_cmd;

	// Initialize
	srandom(time(NULL));

	FILE *input_source = NULL;

	const uid_t UID = getuid();
	struct passwd *PASSWD = getpwuid(UID);
	if (interactive) { // Source config
		input_source = open_config(PASSWD);
		if (input_source != NULL)
			sourcing = 1;
	}
	else {
		if (argc > 1) {
			// Check what type of argument was passed
			if (argv[1][0] == '-') {
				// Long argument
				if (argv[1][1] == '-') {
					if (!strcmp(&argv[1][2], "version")) {
						fprintf(stdout, "mash %u.%u\nCompiled " __DATE__ " " __TIME__ "\n", _VMAJOR, _VMINOR);
						return 0;
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
						}
					}
				}
			}
			else {
				// (Presumed) file name
				input_source = fopen(argv[1], "r");
				if (input_source == NULL) {
					int err = errno;
					fprintf(stderr, "%m\n");
					return err;
				}
			}
		}
	}

	FILE *history_pool = NULL;

	SufTree builtins = suftreeInit(BUILTIN[0], 0);
	for (size_t b = 1; b < BUILTIN_COUNT; b++)
		suftreeAdd(&builtins, BUILTIN[b], b);

	AliasMap *aliases = aliasInit();

	// User prompt (main loop)
	// TODO make a struct for a command - can have an array of command history to call back on!
	Command *cmd = NULL, *last_cmd = commandInit();
	if (subshell) {
		last_cmd->c_len = strlen(subshell_cmd);
		last_cmd->c_buf = subshell_cmd;
		input_source = NULL;
	}

	pid_t cmd_pid = 1; // Don't default to 0 - will cause memory leak

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

			if (input_source == NULL) {
				if (interactive) {
					history_pool = tmpfile();
					if (history_pool == NULL)
						fprintf(stderr, "Cannot create temporary file, command history will not be recorded.\n");
					input_source = stdin;
				}
			}
			// Present prompt and read command
			if (interactive && !sourcing)
				fprintf(stderr, "$ ");
			fflush(stderr);

			int parse_result = commandParse(cmd, input_source, history_pool);
			last_cmd = cmd;
			if (parse_result == -1) {
				if (subshell)
					break;
				if (errno > 11) {
					int err = errno;
					fprintf(stderr, "%s\n", strerror(err));
					cmd_exit = err;
					break;
				}
				// EOF
				if (sourcing) {
					sourcing = 0;
					fclose(input_source);
					input_source = NULL;
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
		}
		else {
			switch (cmd->c_type) {
				case CMD_FREED:
					fprintf(stderr, "CMD_FREED encountered!\n");
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
		aliasResolve(aliases, cmd);
		// Expand command
		char *e_argv[cmd->c_argc + 1 - flow_control];
		for (size_t i = flow_control; i < cmd->c_argc; ++i) {
			char *full_arg = expandArgument(cmd->c_argv[i]);
			if (full_arg == NULL) {
				for (size_t e = 0; e < i; ++e)
					free(e_argv[e]);
				free(last_cmd->c_buf);
				commandFree(last_cmd);
				if (history_pool != NULL) {
					fclose(history_pool);
					history_pool = NULL;
				}
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
				aliasList(aliases, stdout);
			// List or set one alias
			else {
				char *equal_addr = strchr(e_argv[1], '=');
				// No equal sign, means to show an alias
				if (equal_addr == NULL)
					cmd_exit = aliasPrint(aliases, e_argv[1], stdout);
				else {
					size_t equals = equal_addr - e_argv[1];
					// Nothing to the left of the equal sign
					if (equals == 0)
						cmd_exit = 1;
					else {
						e_argv[1][equals] = '\0';
						aliasAdd(aliases, e_argv[1], &e_argv[1][equals + 1]);
					}
				}
			}
			for (size_t v = 0; v < cmd->c_argc - flow_control; ++v)
				free(e_argv[v]);
			continue;
		}

		// Check for unalias
		if (!strcmp(e_argv[0], "unalias")) {
			for (size_t v = 1; v < cmd->c_argc - flow_control; ++v) {
				if (!aliasRemove(aliases, e_argv[v])) {
					fprintf(stderr, "No such alias `%s'\n", e_argv[v]);
					cmd_exit = 1;
				}
				free(e_argv[v]);
			}
			free(e_argv[0]);
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
			//fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
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
			int err = errno;
			fprintf(stderr, "%m\n");

			// Free memory (I wish this wasn't all duplicated in the child to begin with...)
			for (size_t i = 0; i < cmd->c_argc - flow_control; ++i)
				free(e_argv[i]);
			fclose(history_pool);
			history_pool = NULL;

			cmd_exit = err;
			break;
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			for (size_t i = 0; i < cmd->c_argc - flow_control; ++i)
				free(e_argv[i]);
		}
	}

	if (!subshell && last_cmd->c_buf != NULL)
		free(last_cmd->c_buf);
	commandFree(last_cmd);

exit_cleanup:

	aliasFree(aliases);

	if (sourcing)
		fclose(input_source);

	suftreeFree(builtins.sf_gt);
	suftreeFree(builtins.sf_eq);
	suftreeFree(builtins.sf_lt);

	// Update history file
	if (interactive && history_pool != NULL) {
		FILE *history = open_history(PASSWD);
		if (history != NULL) {
			rewind(history_pool);
			char buffer[TMP_RW_BUFSIZE];
			size_t bytes_read;
			while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, history_pool), bytes_read != 0)
				fwrite(buffer, sizeof (char), bytes_read, history);
			fclose(history);
		}
		fclose(history_pool);
	}
	/*if (cmd_pid == 0)
		_Exit(cmd_exit);*/
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
			char *filepath;
			int sub_stdout = mktmpfile(0, &filepath); // Create temporary file, which we will redirect the output to.
			if (sub_stdout == -1)
				return NULL;

			pid_t subshell_pid = fork();
			// Run subshell
			if (subshell_pid == 0) {
				dup2(sub_stdout, STDOUT_FILENO);
				//close(STDIN_FILENO); // Huh... other shells don't do this
				char *argv[4] = {
					"mash",
					"-c",
					arg.str,
					NULL
				};
				int err = main(3, argv);

				close(sub_stdout);
				free(filepath);

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
			char buffer[TMP_RW_BUFSIZE];
			memset(buffer, 0, TMP_RW_BUFSIZE);
			int read_return;
			// TODO: if a word is caught on the TMP_RW_BUFSIZE byte boundary it will be split into 2 arguments - need to fix this (and already know how...)
			while ((read_return = read(sub_stdout, buffer, TMP_RW_BUFSIZE)) > 0 ) {
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
			unlink(filepath); // TODO: consider stdio's tmpfile?
			free(filepath);
			if (out_end > 0 && (sub_output[out_end - 1] == ' ' || sub_output[out_end - 1] == '\n'))
				--out_end;
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
			return expanded_string;
		}
		case ARG_NULL:
		default:
			return NULL;
	}
}

int mktmpfile(_Bool hidden, char **path) {
	size_t path_size = 13 + (hidden ? 1 : 0);
	char *temp_dir = getenv("TMPDIR");
	if (temp_dir == NULL)
		path_size += 4;
	else
		path_size += strlen(temp_dir);

	char *template = calloc(path_size, sizeof (char));
	template[0] = '\0';
	strcat(template, temp_dir == NULL ? "/tmp" : temp_dir);
	strcat(template, hidden ? "/.mash.XXXXXX" : "/mash.XXXXXX");

	int sub_stdout = mkstemp(template); // Create temporary file, which we will redirect the output to.
	if (sub_stdout == -1) {
		fprintf(stderr, "%m\n");
		free(template);
	}
	else
		*path = template;
	return sub_stdout;
}
