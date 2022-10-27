#define _DEFAULT_SOURCE // random
#define _POSIX_C_SOURCE 200809L // fileno
#include "command.h"
#include "mash.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

size_t cmd_builtin;

int fds[2];

pid_t cmd_pid = 1; // Don't default to 0 - will cause memory leak
_Bool killed = 0;

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

void kill_child(int sig) {
	killed = 1;
	kill(cmd_pid, SIGINT);
}

struct sigaction sigint_action = { .sa_handler = kill_child };
struct sigaction previous_action;

CmdSignal commandExecute(Command *cmd, AliasMap *aliases, Source **_source, Variables *vars, FILE **history_pool, uint8_t *cmd_exit) {
	// Empty/blank command, or skippable command (then, else, do)
	switch (cmd->c_type) {
		case CMD_WHILE:
			// Open IO files
			switch (openIOFiles(&cmd->c_io, *_source, vars, cmd_exit)) {
				case -1:
					return CSIG_EXIT;
				case 0:
					break;
				default:
					*cmd_exit = 1;
					return CSIG_DONE;
			}
			for (;;) {
				// Execute test commands
				_Bool cont = 0, brk = 0;
				for (Command *cur = cmd->c_cmds; !cont && cur != NULL; cur = cur->c_next) {
					CmdSignal res = commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit);
					closeIOFiles(&cur->c_io);
					switch (res) {
						case CSIG_BREAK:
							brk = 1;
						case CSIG_CONTINUE:
							cont = 1;
						case CSIG_DONE:
							break;
						case CSIG_EXEC:
							// TODO handle EXEC fail
						case CSIG_EXIT:
							return CSIG_EXIT;
					}
				}
				if (cont) {
					if (brk) {
						*cmd_exit = 0;
						break;
					}
					continue;
				}
				// Break if last command had non-zero exit status
				if (*cmd_exit != 0)
					break;
				// Otherwise run body commands
				CmdSignal res = commandExecute(cmd->c_if_true, aliases, _source, vars, history_pool, cmd_exit);
				switch (res) {
					case CSIG_BREAK:
						brk = 1;
					case CSIG_CONTINUE:
					case CSIG_DONE:
						break;
					case CSIG_EXEC:
						// TODO handle EXEC fail
					case CSIG_EXIT:
						closeIOFiles(&cmd->c_io);
						return CSIG_EXIT;
				}
				if (brk) {
					*cmd_exit = 0;
					break;
				}
			}
			closeIOFiles(&cmd->c_io);
			return CSIG_DONE;
		case CMD_IF:
			// Open IO files
			switch (openIOFiles(&cmd->c_io, *_source, vars, cmd_exit)) {
				case -1:
					return CSIG_EXIT;
				case 0:
					break;
				default:
					*cmd_exit = 1;
					return CSIG_DONE;
			}
			// Execute test commands
			if (cmd->c_cmds == NULL)
				*cmd_exit = 0;
			else {
				for (Command *cur = cmd->c_cmds; cur != NULL; cur = cur->c_next) {
					CmdSignal res = commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit);
					closeIOFiles(&cur->c_io);
					switch (res) {
						case CSIG_DONE:
							break;
						case CSIG_EXEC:
							// TODO handle exec fail
							res = CSIG_EXIT;
						case CSIG_EXIT:
						case CSIG_CONTINUE:
						case CSIG_BREAK:
							closeIOFiles(&cmd->c_io);
							return res;
					}
				}
			}
			// Execute next set of commands based on exit status
			Command *next = *cmd_exit == 0 ? cmd->c_if_true : cmd->c_if_false;
			if (next != NULL) {
				CmdSignal res = commandExecute(next, aliases, _source, vars, history_pool, cmd_exit);
				switch (res) {
					case CSIG_DONE:
						break;
					case CSIG_EXEC:
						// TODO handle exec fail
						res = CSIG_EXIT;
					case CSIG_EXIT:
					case CSIG_CONTINUE:
					case CSIG_BREAK:
						closeIOFiles(&cmd->c_io);
						return res;
				}
			}
			closeIOFiles(&cmd->c_io);
			return CSIG_DONE;
		case CMD_DO:
		case CMD_THEN:
		case CMD_ELSE:
			for (Command *cur = cmd->c_next; cur != NULL; cur = cur->c_next) {
				CmdSignal res = commandExecute(cur, aliases, _source, vars, history_pool, cmd_exit);
				switch (res) {
					case CSIG_DONE:
						break;
					case CSIG_EXEC:
						// TODO handle exec fail
						res = CSIG_EXIT;
					case CSIG_EXIT:
					case CSIG_CONTINUE:
					case CSIG_BREAK:
						closeIOFiles(&cur->c_io);
						return res;
				}
				while (cur->c_io.out_pipe)
					cur = cur->c_next;
			}
			return CSIG_DONE;
		case CMD_DONE:
		case CMD_FI:
			*cmd_exit = 0;
		case CMD_EMPTY:
			return CSIG_DONE;
		default:
			if (cmd->c_argc == 0) // TODO: do we need to check argc...?
				return CSIG_DONE;
	}

	// Set (or create) input and output files
	FILE *filein = NULL, *fileout = NULL;
	// Get this command's files if applicable, otherwise parent's (or none)
	if (cmd->c_io.in_count > 0) {
		if (openInputFiles(&cmd->c_io, *_source, vars, cmd_exit) == -1)
			return CSIG_EXIT;
		if (cmd->c_io.in_file == NULL) {
			*cmd_exit = 1;
			return CSIG_DONE;
		}
		filein = cmd->c_io.in_file;
	}
	else if (cmd->c_parent != NULL)
		filein = getParentInputFile(cmd);
	if (cmd->c_io.out_count > 0 || cmd->c_io.out_file) {
		if (openOutputFiles(&cmd->c_io, *_source, vars, cmd_exit) == -1)
			return CSIG_EXIT;
		if (cmd->c_io.out_file == NULL) {
			*cmd_exit = 1;
			return CSIG_DONE;
		}
		fileout = cmd->c_io.out_file[cmd->c_io.out_count];
		/*if (cmd->c_io.out_pipe)
			cmd->c_next->c_io.in_file = fileout;*/
	}
	else if (cmd->c_parent != NULL && cmd->c_parent->c_io.out_count > 0)
		fileout = getParentOutputFile(cmd);

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
					return CSIG_EXIT;
				}
				if (full_arg == NULL)
					return CSIG_DONE;
			}

			int ret = setvar(vars, cmd->c_argv[0].str, full_arg, 0);
			if (ret == -1) {
				fprintf(stderr, "%s: set variable: %m\n", source->argv[0]);
				*cmd_exit = 1;
			}
			if (cmd->c_argv[1].type != ARG_NULL)
				free(full_arg);
			return CSIG_DONE;
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
			return CSIG_EXIT;
		}
		if (full_arg == NULL) {
			for (size_t e = 0; e < i; ++e)
				free(e_argv[e]);
			return CSIG_DONE;
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

	// Continue
	if (!strcmp(e_argv[0], "continue")) {
		for (size_t i = 0; i < cmd->c_argc; ++i)
			free(e_argv[i]);
		*cmd_exit = 0;
		return CSIG_CONTINUE;
	}

	// Break
	else if (!strcmp(e_argv[0], "break")) {
		for (size_t i = 0; i < cmd->c_argc; ++i)
			free(e_argv[i]);
		*cmd_exit = 0;
		return CSIG_BREAK;
	}

	// Check for alias
	else if (!strcmp(e_argv[0], "alias"))
		b_alias(cmd_exit, e_argv, source, aliases);

	// Check for unalias
	else if (!strcmp(e_argv[0], "unalias"))
		b_unalias(cmd_exit, e_argv, cmd->c_argc, aliases);

	// Exit shell
	else if (!strcmp(e_argv[0], "exit"))
		return b_exit(cmd_exit, e_argv, cmd->c_argc, source);

	// Show help
	else if (!strcmp(e_argv[0], "help"))
		b_help(cmd_exit);

	// Change directory
	else if (!strcmp(e_argv[0], "cd"))
		b_cd(cmd_exit, e_argv, cmd->c_argc);

	// Export variable
	else if (!strcmp(e_argv[0], "export"))
		b_export(cmd_exit, e_argv, cmd->c_argc, source, vars);

	// Check for unset
	else if (!strcmp(e_argv[0], "unset"))
		b_unset(cmd_exit, e_argv, source, vars);

	// Check for dot (source file)
	else if (!strcmp(e_argv[0], "."))
		b_dot(cmd_exit, e_argv, cmd->c_argc, _source);

	// Check for read
	else if (!strcmp(e_argv[0], "read"))
		b_read(cmd_exit, filein, e_argv, source, vars);

	// Shift args
	else if (!strcmp(e_argv[0], "shift"))
		b_shift(cmd_exit, e_argv, cmd->c_argc, source);

	// Regular command
	else {
		// Check for exec
		if (!strcmp(e_argv[0], "exec")) { // TODO: don't exit if exec failed
			if (cmd->c_argc == 1) {
				fprintf(stderr, "%s: exec: requires at least one argument\n", source->argv[0]);
				for (size_t v = 0; v < cmd->c_argc; ++v)
					free(e_argv[v]);
				*cmd_exit = 1;
				return CSIG_DONE;
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
				return CSIG_EXIT;
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
					return CSIG_EXIT;
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
			return CSIG_EXIT;
		}

		// Handle SIGINT to kill the program instead of the shell.
		killed = 0;
		sigaction(SIGINT, &sigint_action, &previous_action);

		// If this command is being piped into another
		if (cmd->c_io.out_pipe) {
			close(pout[1]);

			// If the user is also redirecting the output to file(s), start a thread to read from the pipe and send to the next command, and to the files
			pthread_t thread_out;
			ThreadData data_out;
			if (fileout != NULL) {
				if (pipe(fds) == -1) {
					fprintf(stderr, "%s: fatal error creating pipe: %m\n", source->argv[0]);
					return CSIG_EXIT;
				}
				data_out = (ThreadData){ .read_fd = pout[0], .write_fd = fds[1], .run = 1, .file = fileout };
				pthread_create(&thread_out, NULL, threadOutput, &data_out);
			}
			else
				fds[0] = pout[0];
			int temp[2] = { fds[0], fds[1] };

			// Run next command
			CmdSignal res = commandExecute(cmd->c_next, aliases, _source, vars, history_pool, cmd_exit);
			closeIOFiles(&cmd->c_next->c_io);
			if (fileout != NULL) {
				//data_out.run = 0;
				pthread_join(thread_out, NULL);
				close(temp[0]);
				close(temp[1]);
			}
			close(pout[0]);
			switch (res) {
				case CSIG_DONE:
					break;
				case CSIG_EXEC:
					// TODO handle exec fail
					res = CSIG_EXIT;
				case CSIG_EXIT:
				case CSIG_CONTINUE:
				case CSIG_BREAK:
					return res;
			}
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
		if (killed) {
			sigaction(SIGINT, &previous_action, NULL);
			fputc('\n', stderr);
			*cmd_exit = 130; // SIGINT
			previous_action.sa_handler(SIGINT);
		}
		// Return -1 if command was exec
		if (e_argv[cmd->c_argc] != NULL) {
			for (size_t i = 0; i < cmd->c_argc - 1; ++i)
				free(e_argv[i]);
			free(e_argv[cmd->c_argc]);
			return CSIG_EXIT;
		}
	}
	for (size_t i = 0; i < cmd->c_argc; ++i)
		free(e_argv[i]);
	return CSIG_DONE;
}

// Math formula
typedef struct _math_operand MathOprnd;
typedef struct _math_operator MathOprtr;
struct _math_operand {
	int variable; // false == literal
	union {
		long long value;
		char *name;
	};
	struct _math_operator *operator_left;
	struct _math_operator *operator_right;
};
struct _math_operator {
	char type;
	struct _math_operand *operand_left;
	struct _math_operand *operand_right;
};

long long evaluateMath(CmdArg arg, Variables *vars) {
	// Allocate space and construct linked list
	MathOprnd root;
	if (arg.sub[0].type == ARG_MATH_OPERAND_VARIABLE) {
		root = (MathOprnd){ .variable = 1, .name = arg.sub[0].str };
	}
	else {
		root = (MathOprnd){ .variable = 0, .operator_left = NULL, .operator_right = NULL };
		int scanned;
		if (sscanf(arg.sub[0].str, "%Ld%n", &root.value, &scanned))
			if (arg.sub[0].str[scanned] != '\0')
				fprintf(stderr, "asdf: truncated number to %d digits\n", scanned - (arg.sub[0].str[0] == '-' ? 1 : 0));
	}
	for (size_t i = 1; arg.sub[i].type != ARG_NULL; i += 2) {
		MathOprtr *operator = malloc(sizeof (MathOprtr));
		MathOprnd *operand = malloc(sizeof (MathOprnd));
		*operator = (MathOprtr){ .type = arg.sub[i].str[0], .operand_left = NULL, .operand_right = operand };
		if (arg.sub[i + 1].type == ARG_MATH_OPERAND_VARIABLE)
			*operand = (MathOprnd){ .variable = 1, .name = arg.sub[i + 1].str };
		else {
			*operand = (MathOprnd){ .variable = 0, .operator_left = operator, .operator_right = NULL };
			int scanned;
			if (sscanf(arg.sub[i + 1].str, "%Ld%n", &operand->value, &scanned))
				if (arg.sub[i + 1].str[scanned] != '\0')
					fprintf(stderr, "asdf: truncated number to %d digits\n", scanned - (arg.sub[i + 1].str[0] == '-' ? 1 : 0));
		}
		if (i > 1) {
			operator->operand_left = root.operator_left->operand_right;
			root.operator_left = operator;
		}
		else {
			operator->operand_left = &root;
			root.operator_left = root.operator_right = operator;
		}
	}
	MathOprnd *operand = &root;
	for (;; operand = operand->operator_right->operand_right) {
		if (operand->variable) {
			operand->variable = 0;
			/*if (isdigit(arg.sub[i * 2].str[0])) {
				unsigned position;
				sscanf(arg.sub[i * 2].str)
			}*/
			if (!strcmp(operand->name, "RANDOM"))
				operand->value = random();
			else {
				long long number = 0;
				char *value = getvar(vars, operand->name);
				if (value != NULL) {
					int scanned;
					if (sscanf(value, "%Ld%n", &number, &scanned))
						if (value[scanned] != '\0')
							number = 0;
					operand->value = number;
				}
			}
		}
		if (operand->operator_right == NULL)
			break;
	}

	// pe(mdas)
	MathOprtr *operator = root.operator_right;
	while (operator != NULL) {
		switch (operator->type) {
			case '*':
				operator->operand_left->value *= operator->operand_right->value;
				break;
			case '/':
				if (operator->operand_right->value == 0) {
					// divide by zero
					fputs("divide by zero error\n", stderr);
					operator = root.operator_right;
					while (operator != NULL) {
						MathOprtr *next = operator->operand_right->operator_right;
						free(operator->operand_right);
						free(operator);
						operator = next;
					}
					return 0;
				}
				operator->operand_left->value /= operator->operand_right->value;
				break;
			case '%':
				operator->operand_left->value %= operator->operand_right->value;
				break;
			default:
				operator = operator->operand_right->operator_right;
				continue;
		}
		MathOprnd *previous = operator->operand_left;
		previous->operator_right = operator->operand_right->operator_right;
		free(operator->operand_right);
		free(operator);
		operator = previous->operator_right;
	}
	operator = root.operator_right;
	while (operator != NULL) {
		switch (operator->type) {
			case '+':
				operator->operand_left->value += operator->operand_right->value;
				break;
			case '-':
				operator->operand_left->value -= operator->operand_right->value;
				break;
			default:
				operator = operator->operand_right->operator_right;
				continue;
		}
		MathOprnd *previous = operator->operand_left;
		previous->operator_right = operator->operand_right->operator_right;
		free(operator->operand_right);
		free(operator);
		operator = previous->operator_right;
	}

	return root.value;
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
		case ARG_MATH: {
			char number[21];
			sprintf(number, "%Ld", evaluateMath(arg, vars));
			*str = strdup(number);
			return 0;
			/*size_t sub_count = 1; // operand count
			while (arg.sub[sub_count].type != ARG_NULL)
				if (arg.sub[sub_count].type == ARG_OPERATOR)
					++sub_count;
			char *sub_argv[sub_count];
			for (size_t i = 0)
			return 0;*/
		}
		case ARG_NULL:
		case ARG_MATH_OPERAND_NUMERIC:
		case ARG_MATH_OPERAND_VARIABLE:
		case ARG_MATH_OPERATOR:
		default:
			*str = NULL;
			return 0;
	}
}
