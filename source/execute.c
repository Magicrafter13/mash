#define _GNU_SOURCE // strchrnul
#include "mash.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

size_t cmd_builtin;

pid_t cmd_pid = 1; // Don't default to 0 - will cause memory leak
int cmd_stat;

int commandExecute(Command *cmd, AliasMap *aliases, Source **_source, Variables *vars, FILE **history_pool, uint8_t *cmd_exit, SufTree *builtins) {
	// Empty/blank command, or skippable command (then, else, do)
	switch (cmd->c_type) {
		case CMD_EMPTY:
		case CMD_DO:
		case CMD_DONE:
		case CMD_THEN:
		case CMD_ELSE:
		case CMD_FI:
			return 0;
		default:
			if (cmd->c_argc == 0) // TODO: do we need to check argc...?
				return 0;
	}

	// Attempt to parse aliases
	aliasResolve(aliases, cmd);

	Source *source = *_source;

	// Check if user is setting shell variable
	if (cmd->c_argv[0].type == ARG_BASIC_STRING) {
		size_t len = strlen(cmd->c_argv[0].str);
		if (cmd->c_argv[0].str[len - 1] == '=' &&
				len - varNameLength(cmd->c_argv[0].str) == 1) {
			cmd->c_argv[0].str[len - 1] = '\0';
			char *full_arg = "";
			if (cmd->c_argv[1].type != ARG_NULL) {
				full_arg = expandArgument(cmd->c_argv[1], source, vars, cmd_exit);
				if (full_arg == NULL) {
					if (cmd_pid == 0)
						*history_pool = NULL;
					return -1;
				}
			}

			int ret = setvar(vars, cmd->c_argv[0].str, full_arg, 0);
			if (ret == -1) {
				fprintf(stderr, "%s: %m\n", source->argv[0]);
				*cmd_exit = 1;
			}
			if (cmd->c_argv[1].type != ARG_NULL)
				free(full_arg);
			return 0;
		}
	}

	// Expand command into string array
	char *e_argv[cmd->c_argc + 1];
	for (size_t i = 0; i < cmd->c_argc; ++i) {
		char *full_arg = expandArgument(cmd->c_argv[i], source, vars, cmd_exit);
		if (full_arg == NULL) {
			for (size_t e = 0; e < i; ++e)
				free(e_argv[e]);
			// If we are a child that returned from the fork in expandArgument
			if (cmd_pid == 0) {
				*history_pool = NULL;
				return -1;
			}
			return 0;
		}
		e_argv[i] = full_arg;
	}
	e_argv[cmd->c_argc] = NULL;

#ifdef DEBUG
	fprintf(stderr, "Execing:\n");
	for (size_t i = 0; i < cmd->c_argc; ++i)
		fprintf(stderr, "%s ", e_argv[i]);
	fprintf(stderr, "\n");
#endif

	// Check for alias
	if (!strcmp(e_argv[0], "alias")) {
		*cmd_exit = 0;
		// List all aliases
		if (e_argv[1] == NULL)
			aliasList(aliases, stdout);
		// List or set one alias
		else {
			char *equal_addr = strchr(e_argv[1], '=');
			// No equal sign, means to show an alias
			if (equal_addr == NULL)
				*cmd_exit = aliasPrint(aliases, e_argv[1], stdout);
			else {
				size_t equals = equal_addr - e_argv[1];
				// Nothing to the left of the equal sign
				if (equals == 0)
					*cmd_exit = 1;
				else {
					e_argv[1][equals] = '\0';
					if (aliasAdd(aliases, e_argv[1], &e_argv[1][equals + 1]) == NULL) {
						fprintf(stderr, "%s: alias: error parsing string\n", source->argv[0]);
						*cmd_exit = 1;
					}
				}
			}
		}
		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Check for unalias
	if (!strcmp(e_argv[0], "unalias")) {
		for (size_t v = 1; v < cmd->c_argc; ++v) {
			if (!aliasRemove(aliases, e_argv[v])) {
				fprintf(stderr, "No such alias `%s'\n", e_argv[v]);
				*cmd_exit = 1;
			}
			free(e_argv[v]);
		}
		free(e_argv[0]);
		return 0;
	}

	// Exit shell
	if (!strcmp(e_argv[0], "exit")) {
		if (cmd->c_argc > 1) {
			int temp;
			sscanf(e_argv[1], "%u", &temp);
			*cmd_exit = temp % 256;
		}
		else
			*cmd_exit = 0;

		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);

		if (source->input == stdin)
			return -1;
		else
			fseek(source->input, 0, SEEK_END);
		return 0;
	}

	// Execute builtin
	if (suftreeHas(builtins, e_argv[0], &cmd_builtin)) {
#ifdef DEFINE
		fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
#endif
		BUILTIN_FUNCTION[cmd_builtin](cmd->c_argc, (void**)e_argv);

		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Export variable
	if (!strcmp(e_argv[0], "export")) {
		if (cmd->c_argc > 1) {
			char *equal_addr = strchrnul(e_argv[1], '='), *value = NULL;
			if (equal_addr[0] == '=') {
				equal_addr[0] = '\0';
				value = &equal_addr[1];
			}
			if (setvar(vars, e_argv[1], value, 1) == -1) {
				fprintf(stderr, "%s: export: %m\n", source->argv[0]);
				*cmd_exit = 1;
			}
		}
		else
			*cmd_exit = 1;

		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Check for unset
	if (!strcmp(e_argv[0], "unset")) {
		for (size_t i = 1; e_argv[i] != NULL; ++i) {
			if (unsetvar(vars, e_argv[i]) == -1) {
				fprintf(stderr, "%s: unset: %m\n", source->argv[0]);
				*cmd_exit = 1;
			}
		}
		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Check for dot (source file)
	if (!strcmp(e_argv[0], ".")) {
		FILE *script = fopen(e_argv[1], "r");
		if (script != NULL)
			*_source = sourceAdd(source, script, cmd->c_argc - 1, &e_argv[1]);
		else {
			fprintf(stderr, "%m\n");
			*cmd_exit = 1;
		}
		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Set (or create) input and output files
	// While and if may have io files that haven't been opened yet
	if (cmd->c_type == CMD_WHILE || cmd->c_type == CMD_IF) {
		if (cmd->c_block_io.in_count > 0 && cmd->c_block_io.in_file == NULL) {
			cmd->c_block_io.in_file = openInputFiles(cmd->c_block_io, source, vars, cmd_exit);
			if (cmd->c_block_io.in_file == NULL) {
				for (size_t v = 0; v < cmd->c_argc; ++v)
					free(e_argv[v]);
				*cmd_exit = 1;
				return 0;
			}
		}
		if (cmd->c_block_io.out_count > 0 && cmd->c_block_io.out_file == NULL) {
			cmd->c_block_io.out_file = openOutputFiles(cmd->c_block_io, source, vars, cmd_exit);
			if (cmd->c_block_io.out_file == NULL) {
				for (size_t v = 0; v < cmd->c_argc; ++v)
					free(e_argv[v]);
				*cmd_exit = 1;
				return 0;
			}
		}
	}
	FILE *filein = NULL, *fileout = NULL;
	// Get this command's files if applicable, otherwise parent's (or none)
	if (cmd->c_io.in_count > 0) {
		cmd->c_io.in_file = openInputFiles(cmd->c_io, source, vars, cmd_exit);
		if (cmd->c_io.in_file == NULL) {
			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			*cmd_exit = 1;
			return 0;
		}
		filein = cmd->c_io.in_file;
	}
	else if (cmd->c_parent != NULL)
		filein = getParentInputFile(cmd);
	if (cmd->c_io.out_count > 0) {
		cmd->c_io.out_file = openOutputFiles(cmd->c_io, source, vars, cmd_exit);
		if (cmd->c_io.out_file == NULL) {
			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			*cmd_exit = 1;
			return 0;
		}
		fileout = cmd->c_io.out_file[cmd->c_io.out_count];
	}
	else if (cmd->c_parent != NULL && cmd->c_parent->c_block_io.out_count > 0)
		fileout = getParentOutputFile(cmd);

	// Check for read
	if (!strcmp(e_argv[0], "read")) {
		char *value = NULL;
		size_t size = 0;
		size_t bytes_read = getline(&value, &size, filein == NULL ? stdin : filein);
		if (bytes_read == -1) {
			if (filein == NULL)
				clearerr(stdin);
			*cmd_exit = 1;
		}
		else {
			if (value[bytes_read - 1] == '\n')
				value[bytes_read - 1] = '\0';
			if (e_argv[1] != NULL) {
				if (setvar(vars, e_argv[1], value, 0) == -1) {
					*cmd_exit = errno;
					fprintf(stderr, "%m\n");
				}
			}
		}
		free(value);
		for (size_t v = 0; v < cmd->c_argc; ++v)
			free(e_argv[v]);
		return 0;
	}

	// Execute regular command
	cmd_pid = fork();
	// Forked process will execute the command
	if (cmd_pid == 0) {
		// Change stdin and stdout if user redirected them
		if (filein != NULL)
			dup2(fileno(filein), STDIN_FILENO);
		if (fileout != NULL)
			dup2(fileno(fileout), STDOUT_FILENO);
		// Change stdout if user redirected output
		if (fileout != NULL)
			dup2(fileno(fileout), STDOUT_FILENO);

		// TODO consider manual search of the path
		execvp(e_argv[0], e_argv);
		fprintf(stderr, "%s: %s: %m\n", source->argv[0], e_argv[0]);

		// Free memory (I wish this wasn't all duplicated in the child to begin with...)
		for (size_t i = 0; i < cmd->c_argc; ++i)
			free(e_argv[i]);
		*history_pool = NULL;

		*cmd_exit = 1;
		return -1;
	}

	// While the main process waits for the child to exit
	waitpid(cmd_pid, &cmd_stat, 0);
	*cmd_exit = WEXITSTATUS(cmd_stat);
	for (size_t i = 0; i < cmd->c_argc; ++i)
		free(e_argv[i]);
	return 0;
}

char *expandArgument(CmdArg arg, Source *source, Variables *vars, uint8_t *cmd_exit) {
	int argc = source->argc;
	char **argv = source->argv;
	switch (arg.type) {
		case ARG_BASIC_STRING:
		case ARG_QUOTED_STRING:
			return strdup(arg.str);
		case ARG_VARIABLE:
			if (arg.str[0] >= '0' && arg.str[0] <= '9') {
				unsigned position;
				sscanf(arg.str, "%u", &position);
				return strdup(position >= argc ? "" : argv[position]);
			}
			if (!strcmp(arg.str, "RANDOM")) {
				char number[12];
				sprintf(number, "%lu", random());
				return strdup(number);
			}
			if (!strcmp(arg.str, "?")) {
				char number[4];
				sprintf(number, "%"PRIu8, *cmd_exit);
				return strdup(number);
			}
			if (!strcmp(arg.str, "$")) {
				char number[21];
				sprintf(number, "%ld", (long)getpid());
				return strdup(number);
			}

			char *value = getvar(vars, arg.str);
			return strdup(value == NULL ? "" : value);
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL: {
			char *filepath;
			int sub_stdout = mktmpfile(0, &filepath, vars); // Create temporary file, which we will redirect the output to.
			if (sub_stdout == -1)
				return NULL;

			pid_t sub_pid = fork();
			// Run subshell
			if (sub_pid == 0) {
				dup2(sub_stdout, STDOUT_FILENO);
				char *argv[4] = {
					"mash",
					"-c",
					arg.str,
					NULL
				};
				int err = main(3, argv);

				cmd_pid = 0;

				close(sub_stdout);
				free(filepath);

				errno = err;
				return NULL;
			}
			// Wait for subshell to finish,
			waitpid(sub_pid, &cmd_stat, 0);
			*cmd_exit = WEXITSTATUS(cmd_stat);

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
			char *sub_argv[sub_count];
			for (size_t i = 0; i < sub_count; ++i) {
				char *e = expandArgument(arg.sub[i], source, vars, cmd_exit);
				if (e == NULL) {
					for (size_t f = 0; f < i; ++f)
						free(sub_argv[f]);
					return NULL;
				}
				sub_argv[i] = e;
			}

			// Find length of strings together
			size_t length = 0;
			for (size_t i = 0; i < sub_count; ++i)
				length += strlen(sub_argv[i]);
			char *expanded_string = calloc(length + 1, sizeof (char));
			expanded_string[0] = '\0';
			for (size_t i = 0; i < sub_count; ++i) {
				strcat(expanded_string, sub_argv[i]);
				free(sub_argv[i]);
			}
			return expanded_string;
		}
		case ARG_NULL:
		default:
			return NULL;
	}
}
