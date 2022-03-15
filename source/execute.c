#define _GNU_SOURCE // strchrnul
#include "mash.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

void *_DUMMY_PTR[1] = { NULL };

size_t cmd_builtin;

int fds[2];

pid_t cmd_pid = 1; // Don't default to 0 - will cause memory leak

typedef struct _thread_data ThreadData;
struct _thread_data {
	int read_fd;
	int write_fd;
	FILE *restrict file;
	_Bool run;
};

void *threadInput(void *ptr) {
	ThreadData *data = ptr;
	char buf[TMP_RW_BUFSIZE] = "";
	while (data->run) {
		ssize_t bytes_read = read(data->read_fd, buf, TMP_RW_BUFSIZE);
		if (bytes_read < 1) {
			// If user used a file for input, also read from it
			if (data->file != NULL)
				while (bytes_read = fread(buf, sizeof (char), TMP_RW_BUFSIZE, data->file), bytes_read > 0)
					write(data->write_fd, buf, bytes_read);
			break;
		}
		//fprintf(stderr, "threadInput got <%s> (%zd)\n", buf, bytes_read);
		write(data->write_fd, buf, bytes_read);
	}
	close(data->read_fd);
	close(data->write_fd);
	return NULL;
}

void *threadOutput(void *ptr) {
	ThreadData *data = ptr;
	char buf[TMP_RW_BUFSIZE] = "";
	while (data->run) {
		ssize_t bytes_read = read(data->read_fd, buf, TMP_RW_BUFSIZE);
		if (bytes_read < 1)
			break;
		write(data->write_fd, buf, bytes_read);
		// If user used a file for output, also write to it
		if (data->file != NULL)
			fwrite(buf, sizeof (char), bytes_read, data->file);
	}
	close(data->write_fd);
	return NULL;
}

int commandExecute(Command *cmd, AliasMap *aliases, Source **_source, Variables *vars, FILE **history_pool, uint8_t *cmd_exit, SufTree *builtins) {
	// Empty/blank command, or skippable command (then, else, do)
	switch (cmd->c_type) {
		case CMD_WHILE:
			// Open IO files
			switch (openIOFiles(&cmd->c_io, *_source, vars, cmd_exit)) {
				case -1:
					return -1;
				case 0:
					break;
				default:
					*cmd_exit = 1;
					return 0;
			}
			for (;;) {
				// Execute test commands
				for (Command *cur = cmd->c_cmds; cur != NULL; cur = cur->c_next) {
					if (commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit, builtins) == -1) {
						closeIOFiles(&cur->c_io);
						return -1;
					}
					closeIOFiles(&cur->c_io);
				}
				// Break if last command had non-zero exit status
				if (*cmd_exit != 0)
					break;
				// Otherwise run body commands
				if (commandExecute(cmd->c_if_true, aliases, _source, vars, history_pool, cmd_exit, builtins) == -1) {
					closeIOFiles(&cmd->c_io);
					return -1;
				}
			}
			closeIOFiles(&cmd->c_io);
			return 0;
		case CMD_IF:
			// Open IO files
			switch (openIOFiles(&cmd->c_io, *_source, vars, cmd_exit)) {
				case -1:
					return -1;
				case 0:
					break;
				default:
					*cmd_exit = 1;
					return 0;
			}
			// Execute test commands
			if (cmd->c_cmds == NULL)
				*cmd_exit = 0;
			else {
				for (Command *cur = cmd->c_cmds; cur != NULL; cur = cur->c_next) {
					if (commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit, builtins) == -1) {
							closeIOFiles(&cur->c_io);
							return -1;
					}
					closeIOFiles(&cur->c_io);
				}
			}
			// Execute next set of commands based on exit status
			Command *next = *cmd_exit == 0 ? cmd->c_if_true : cmd->c_if_false;
			if (next != NULL) {
				if (commandExecute(*cmd_exit == 0 ? cmd->c_if_true : cmd->c_if_false, aliases, _source, vars, history_pool, cmd_exit, builtins) == -1) {
					closeIOFiles(&cmd->c_io);
					return -1;
				}
			}
			closeIOFiles(&cmd->c_io);
			return 0;
		case CMD_DO:
		case CMD_THEN:
		case CMD_ELSE:
			for (Command *cur = cmd->c_next; cur != NULL; cur = cur->c_next) {
				if (commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit, builtins) == -1)
					return -1;
				while (cur->c_io.out_pipe)
					cur = cur->c_next;
			}
			return 0;
		case CMD_DONE:
		case CMD_FI:
			*cmd_exit = 0;
		case CMD_EMPTY:
			return 0;
		default:
			if (cmd->c_argc == 0) // TODO: do we need to check argc...?
				return 0;
	}

	// Set (or create) input and output files
	FILE *filein = NULL, *fileout = NULL;
	// Get this command's files if applicable, otherwise parent's (or none)
	if (cmd->c_io.in_count > 0) {
		if (openInputFiles(&cmd->c_io, *_source, vars, cmd_exit) == -1)
			return -1;
		if (cmd->c_io.in_file == NULL) {
			*cmd_exit = 1;
			return 0;
		}
		filein = cmd->c_io.in_file;
	}
	else if (cmd->c_parent != NULL)
		filein = getParentInputFile(cmd);
	if (cmd->c_io.out_count > 0 || cmd->c_io.out_file) {
		if (openOutputFiles(&cmd->c_io, *_source, vars, cmd_exit) == -1)
			return -1;
		if (cmd->c_io.out_file == NULL) {
			*cmd_exit = 1;
			return 0;
		}
		fileout = cmd->c_io.out_file[cmd->c_io.out_count];
		/*if (cmd->c_io.out_pipe)
			cmd->c_next->c_io.in_file = fileout;*/
	}
	else if (cmd->c_parent != NULL && cmd->c_parent->c_io.out_count > 0)
		fileout = getParentOutputFile(cmd);

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
				if (expandArgument(&full_arg, cmd->c_argv[1], source, vars, cmd_exit) == -1) {
					*history_pool = NULL;
					return -1;
				}
				if (full_arg == NULL)
					return 0;
			}

			int ret = setvar(vars, cmd->c_argv[0].str, full_arg, 0);
			if (ret == -1) {
				fprintf(stderr, "%s: set variable: %m\n", source->argv[0]);
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
		char *full_arg;
		if (expandArgument(&full_arg, cmd->c_argv[i], source, vars, cmd_exit) == -1) {
			for (size_t e = 0; e < i; ++e)
				free(e_argv[e]);
			*history_pool = NULL;
			return -1;
		}
		if (full_arg == NULL) {
			for (size_t e = 0; e < i; ++e)
				free(e_argv[e]);
			return 0;
		}
		e_argv[i] = full_arg;
	}
	e_argv[cmd->c_argc] = NULL;

#ifdef DEBUG
	fputs("Execing:\n", stderr);
	for (size_t i = 0; i < cmd->c_argc; ++i)
		fprintf(stderr, "%s ", e_argv[i]);
	fputc('\n', stderr);
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
	}

	// Check for unalias
	else if (!strcmp(e_argv[0], "unalias")) {
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
	else if (!strcmp(e_argv[0], "exit")) {
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
	else if (suftreeHas(builtins, e_argv[0], &cmd_builtin)) {
#ifdef DEFINE
		fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
#endif
		BUILTIN_FUNCTION[cmd_builtin](cmd->c_argc, (void**)e_argv);
	}

	// Export variable
	else if (!strcmp(e_argv[0], "export")) {
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
	}

	// Check for unset
	else if (!strcmp(e_argv[0], "unset")) {
		for (size_t i = 1; e_argv[i] != NULL; ++i) {
			if (unsetvar(vars, e_argv[i]) == -1) {
				fprintf(stderr, "%s: unset: %m\n", source->argv[0]);
				*cmd_exit = 1;
			}
		}
	}

	// Check for dot (source file)
	else if (!strcmp(e_argv[0], ".")) {
		FILE *script = fopen(e_argv[1], "r");
		if (script != NULL)
			*_source = sourceAdd(source, script, cmd->c_argc - 1, &e_argv[1]);
		else {
			fprintf(stderr, "%s: .: %m\n", source->argv[0]);
			*cmd_exit = 1;
		}
	}

	// Check for read
	else if (!strcmp(e_argv[0], "read")) {
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
			if (e_argv[1] != NULL) {
				if (setvar(vars, e_argv[1], value, 0) == -1) {
					*cmd_exit = errno;
					fprintf(stderr, "%s: read: %m\n", source->argv[0]);
				}
			}
		}
		free(value);
	}

	// Shift args
	else if (!strcmp(e_argv[0], "shift")) {
		int amount = 1;
		if (cmd->c_argc > 1) {
			int left, right;
			sscanf(e_argv[1], "%n%d%n", &left, &amount, &right);
		}
		*cmd_exit = sourceShift(source, amount);
	}

	// Regular command
	else {
		// Check for exec
		if (!strcmp(e_argv[0], "exec")) { // TODO: don't exit if exec failed
			if (cmd->c_argc == 1) {
				fprintf(stderr, "%s: exec: requires at least one argument\n", source->argv[0]);
				for (size_t v = 0; v < cmd->c_argc; ++v)
					free(e_argv[v]);
				*cmd_exit = 1;
				return 0;
			}
			char *exec = e_argv[0];
			for (size_t i = 0; i < cmd->c_argc; ++i)
				e_argv[i] = e_argv[i + 1];
			e_argv[cmd->c_argc] = exec;
		}

		// Setup pipe
		int pin[2] = { fds[0], -1 }, pout[2] = { -1, -1 };
		if (cmd->c_io.out_pipe) {
			if (pipe(pout) == -1) {
				fprintf(stderr, "%s: fatal error creating pipe: %m\n", source->argv[0]);
				return -1;
			}
		}

		// If this command is getting piped into by another
		pthread_t thread_in;
		ThreadData data_in;
		if (cmd->c_io.in_pipe) {
			// If the user is also redirecting the input from file(s), start a thread to read from the pipe, and then read from the files
			if (filein != NULL) {
				if (pipe(pin) == -1) {
					fprintf(stderr, "%s: fatal error creating pipe: %m\n", source->argv[0]);
					return -1;
				}
				data_in = (ThreadData){ .read_fd = fds[0], .write_fd = pin[1], .run = 1, .file = filein };
				pthread_create(&thread_in, NULL, threadInput, &data_in);
			}
		}

		// Execute regular command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// Close unused pipe ends, or change stdin and stdout if user redirected them
			if (cmd->c_io.in_pipe) {
				close(fds[1]);
				if (filein != NULL)
					close(pin[1]);
				dup2(pin[0], STDIN_FILENO); // Read from pin
			}
			else if (filein != NULL)
				dup2(fileno(filein), STDIN_FILENO);
			if (cmd->c_io.out_pipe) {
				close(pout[0]);
				dup2(pout[1], STDOUT_FILENO); // Write to pout
			}
			else if (fileout != NULL)
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

		// If this command is being piped into another
		if (cmd->c_io.out_pipe) {
			close(pout[1]);

			// If the user is also redirecting the output to file(s), start a thread to read from the pipe and send to the next command, and to the files
			pthread_t thread_out;
			ThreadData data_out;
			if (fileout != NULL) {
				if (pipe(fds) == -1) {
					fprintf(stderr, "%s: fatal error creating pipe: %m\n", source->argv[0]);
					return -1;
				}
				data_out = (ThreadData){ .read_fd = pout[0], .write_fd = fds[1], .run = 1, .file = fileout };
				pthread_create(&thread_out, NULL, threadOutput, &data_out);
			}
			else
				fds[0] = pout[0];
			int temp[2] = { fds[0], fds[1] };

			// Run next command
			int res = commandExecute(cmd->c_next, aliases, _source, vars, history_pool, cmd_exit, builtins);
			closeIOFiles(&cmd->c_next->c_io);
			if (fileout != NULL) {
				//data_out.run = 0;
				pthread_join(thread_out, NULL);
				close(temp[0]);
				close(temp[1]);
			}
			close(pout[0]);
			if (res == -1)
				return -1;
		}

		// While the main process waits for the child to exit
		int cmd_stat;
		waitpid(cmd_pid, &cmd_stat, 0);
		if (cmd->c_io.in_pipe) {
			if (filein != NULL) {
				data_in.run = 0;
				pthread_join(thread_in, NULL);
			}
			close(pin[0]);
		}
		if (filein != NULL) { // Cursed code to keep FILE position and fd offset in sync...
			ssize_t offset = lseek(fileno(filein), 0, SEEK_CUR);
			//fprintf(stderr, "FILE fd offset is now %zi (stream %ld)\n", offset, ftell(filein));
			fseek(filein, offset, SEEK_SET);
			fflush(filein);
			//fprintf(stderr, "FILE fd offset is now %zi (stream %ld)\n", offset, ftell(filein));
		}
		// Set exit status (unless we piped, as the next programs exit status is used)
		if (!cmd->c_io.out_pipe)
			*cmd_exit = WEXITSTATUS(cmd_stat);
		// Return -1 if command was exec
		if (e_argv[cmd->c_argc] != NULL) {
			for (size_t i = 0; i < cmd->c_argc - 1; ++i)
				free(e_argv[i]);
			free(e_argv[cmd->c_argc]);
			return -1;
		}
	}
	for (size_t i = 0; i < cmd->c_argc; ++i)
		free(e_argv[i]);
	return 0;
}

int expandArgument(char **str, CmdArg arg, Source *source, Variables *vars, uint8_t *cmd_exit) {
	switch (arg.type) {
		case ARG_BASIC_STRING:
		case ARG_QUOTED_STRING:
			*str = strdup(arg.str);
			return 0;
		case ARG_VARIABLE:
			if (arg.str[0] >= '0' && arg.str[0] <= '9') {
				unsigned position;
				sscanf(arg.str, "%u", &position);
				*str = strdup(position >= source->argc ? "" : source->argv[position]);
				return 0;
			}
			if (!strcmp(arg.str, "RANDOM")) {
				char number[12];
				sprintf(number, "%ld", random());
				*str = strdup(number);
				return 0;
			}
			if (!strcmp(arg.str, "?")) {
				char number[4];
				sprintf(number, "%"PRIu8, *cmd_exit);
				*str = strdup(number);
				return 0;
			}
			if (!strcmp(arg.str, "$")) {
				char number[21];
				sprintf(number, "%ld", (long)getpid());
				*str = strdup(number);
				return 0;
			}
			if (!strcmp(arg.str, "#")) {
				char number[8];
				sprintf(number, "%u", (unsigned)source->argc - 1);
				*str = strdup(number);
				return 0;
			}

			char *value = getvar(vars, arg.str);
			*str = strdup(value == NULL ? "" : value);
			return 0;
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL: {
			char *filepath;
			int sub_stdout = mktmpfile(0, &filepath, vars); // Create temporary file, which we will redirect the output to.
			if (sub_stdout == -1) {
				*str = NULL;
				return 0;
			}

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

				close(sub_stdout);
				free(filepath);

				errno = err;
				return -1;
			}
			// Wait for subshell to finish,
			int cmd_stat;
			waitpid(sub_pid, &cmd_stat, 0);
			*cmd_exit = WEXITSTATUS(cmd_stat);

			// Allocate memory for the file
			off_t size = lseek(sub_stdout, 0, SEEK_END);
			lseek(sub_stdout, 0, SEEK_SET);
			char *sub_output = *str = calloc(size + 1, sizeof (char));
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
			return 0;
		}
		case ARG_COMPLEX_STRING: {
			size_t sub_count = 0;
			while (arg.sub[sub_count].type != ARG_NULL)
				++sub_count;
			char *sub_argv[sub_count];
			for (size_t i = 0; i < sub_count; ++i) {
				char *e;
				if (expandArgument(&e, arg.sub[i], source, vars, cmd_exit) == -1) {
					for (size_t f = 0; f < i; ++f)
						free(sub_argv[f]);
					return -1;
				}
				if (e == NULL) {
					for (size_t f = 0; f < i; ++f)
						free(sub_argv[f]);
					*str = NULL;
					return 0;
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
			*str = expanded_string;
			return 0;
		}
		case ARG_NULL:
		default:
			*str = NULL;
			return 0;
	}
}
