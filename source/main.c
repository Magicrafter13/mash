#define _POSIX_C_SOURCE 200809L // fileno, strdup, setenv
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

char *expandArgument(CmdArg, int, char**);
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

pid_t cmd_pid = 1; // Don't default to 0 - will cause memory leak
int cmd_stat;
uint8_t cmd_exit;

int main(int argc, char *argv[]) {
	// Set stderr to be line buffered
	setvbuf(stderr, NULL, _IOLBF, 0);

	// Determine shell type
	int login = argv[0][0] == '-';
	if (login)
		fprintf(stderr, "This mash is a login shell.\n");

	int interactive = argc == 1 && isatty(fileno(stdin)), subshell = 0;
	if (interactive)
		fprintf(stderr, "This mash is interactive.\n");

	// Create shell environment
	Source *source = sourceInit();
	sourceSet(source, stdin, argc, argv);

	// Initialize
	const uid_t UID = getuid();
	struct passwd *PASSWD = getpwuid(UID);
	char *subshell_cmd;
	FILE *history_pool = NULL;
	srandom(time(NULL));

	if (interactive) { // Source config
		history_pool = tmpfile();
		if (history_pool == NULL)
			fprintf(stderr, "Cannot create temporary file, command history will not be recorded.\n");
		source->output = history_pool;
		FILE *config = open_config(PASSWD);
		if (config != NULL)
			source = sourceAdd(source, config, argc, argv);
	}
	else {
		if (argc > 1) {
			// Check what type of argument was passed
			if (argv[1][0] == '-') {
				// Long argument
				if (argv[1][1] == '-') {
					if (!strcmp(&argv[1][2], "version")) {
						fprintf(stdout, "mash %u.%u\nCompiled " __DATE__ " " __TIME__ "\n", _VMAJOR, _VMINOR);
						sourceFree(source);
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
				FILE *script = fopen(argv[1], "r");
				if (script == NULL) {
					int err = errno;
					fprintf(stderr, "%m\n");
					sourceFree(source);
					return err;
				}
				sourceSet(source, script, argc - 1, &argv[1]);
			}
		}
	}

	// Further initialization after successful setup and argument parsing
	Command *cmd = NULL, *last_cmd = commandInit(); // TODO make a struct for a command - can have an array of command history to call back on!
	SufTree builtins = suftreeInit(BUILTIN[0], 0);
	for (size_t b = 1; b < BUILTIN_COUNT; b++)
		suftreeAdd(&builtins, BUILTIN[b], b);
	size_t cmd_builtin;
	AliasMap *aliases = aliasInit();

	// If this is a subshell, we need to setup last_cmd in a special way
	if (subshell) {
		last_cmd->c_len = strlen(subshell_cmd);
		last_cmd->c_buf = subshell_cmd;
		sourceSet(source, NULL, 1, argv);
	}

	// User prompt (main loop)
	for (;;) {
		// cmd == NULL tells us we need to free the current command chain, then read more
		if (cmd == NULL) {
			commandFree(last_cmd);
			cmd = last_cmd;

			// Present prompt and read command
			if (interactive && source->input == stdin) {
				fprintf(stderr, "$ ");
				fflush(stderr);
			}

			int parse_result = commandParse(cmd, source->input, source->output);
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
				if (source->input != stdin && source->prev != NULL) { // Sourcing a file
					cmd = NULL;
					source = sourceClose(source);
					continue;
				}
				if (interactive)
					fprintf(stderr, "\n");
				break;
			}
			if (parse_result) {
				fprintf(stderr, "   %*s\n", (int)cmd->c_len, "^");
				fprintf(stderr, "%s: parse error near `%c'\n", argv[0], cmd->c_buf[0]); // TODO need to fix this
				cmd->c_buf[0] = '\0';
				cmd = NULL;
				continue;
			}
		}
		// Otherwise, we should advance to the next command
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
			// If command is now NULL, we need to jump back to the top so we can free memory
			if (cmd == NULL)
				continue;
		}

		/*
		 * Execute command
		 */

		// Empty/blank command
		if (cmd->c_type == CMD_EMPTY || cmd->c_argc == 0) // TODO: do we need to check argc...?
			continue;

		// Attempt to parse aliases
		aliasResolve(aliases, cmd);
		// Expand command into string array
		_Bool expanded = 1;
		char *e_argv[cmd->c_argc + 1];
		for (size_t i = 0; i < cmd->c_argc; ++i) {
			char *full_arg = expandArgument(cmd->c_argv[i], source->argc, source->argv);
			if (full_arg == NULL) {
				for (size_t e = 0; e < i; ++e)
					free(e_argv[e]);
				expanded = 0;
				// If we are a child that returned from the fork in expandArgument
				if (cmd_pid == 0)
					history_pool = NULL;
				break;
			}
			e_argv[i] = full_arg;
		}
		if (!expanded)
			break;
		e_argv[cmd->c_argc] = NULL;

#ifdef DEBUG
		fprintf(stderr, "Execing:\n");
		for (size_t i = 0; i < cmd->c_argc; ++i)
			fprintf(stderr, "%s ", e_argv[i]);
		fprintf(stderr, "\n");
#endif

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
			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			continue;
		}

		// Check for unalias
		if (!strcmp(e_argv[0], "unalias")) {
			for (size_t v = 1; v < cmd->c_argc; ++v) {
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
			if (cmd->c_argc > 1) {
				int temp;
				sscanf(e_argv[1], "%u", &temp);
				cmd_exit = temp % 256;
			}
			else
				cmd_exit = 0;

			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);

			if (source->input == stdin)
				break;
			else
				fseek(source->input, 0, SEEK_END);
			continue;
		}

		// Execute builtin
		if (suftreeHas(&builtins, e_argv[0], &cmd_builtin)) {
#ifdef DEFINE
			fprintf(stderr, "Executing builtin '%s'\n", BUILTIN[cmd_builtin]);
#endif
			BUILTIN_FUNCTION[cmd_builtin](cmd->c_argc, (void**)e_argv);

			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			continue;
		}

		// Check for dot (source file)
		if (!strcmp(e_argv[0], ".")) {
			FILE *script = fopen(e_argv[1], "r");
			if (script != NULL)
				source = sourceAdd(source, script, cmd->c_argc - 1, &e_argv[1]);
			else {
				fprintf(stderr, "%m\n");
				cmd_exit = 1;
			}
			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			continue;
		}

		// Check for read
		if (!strcmp(e_argv[0], "read")) {
			char *value = NULL;
			size_t size = 0;
			size_t bytes_read = getline(&value, &size, stdin);
			if (bytes_read == -1) {
				clearerr(stdin);
				cmd_exit = 1;
			}
			else {
				if (value[bytes_read - 1] == '\n')
					value[bytes_read - 1] = '\0';
				if (e_argv[1] != NULL) {
					if (setenv(e_argv[1], value, 1) == -1) {
						cmd_exit = errno;
						fprintf(stderr, "%m\n");
					}
				}
			}
			free(value);
			for (size_t v = 0; v < cmd->c_argc; ++v)
				free(e_argv[v]);
			continue;
		}

		FILE *fileout[cmd->c_output_count + 1];
		_Bool output_error = 0;
		// Open output files if applicable
		if (cmd->c_output_count > 0) {
			size_t i;
			for (i = 0; i < cmd->c_output_count; ++i) {
				// Get filename (path)
				char *opath = expandArgument(cmd->c_output_file[i], argc, argv);
				if (opath == NULL) {
					fprintf(stderr, "%s: error expanding argument, possibly related error message: %m\n", argv[0]);
					output_error = 1;
					break;
				}

				// Open file
				FILE *ofile = fopen(opath, "w");
				if (ofile == NULL) {
					fprintf(stderr, "%s: %m: %s\n", argv[0], opath);
					free(opath);
					output_error = 1;
					break;
				}
				free(opath);

				// Add to array
				fileout[i] = ofile;
			}
			if (output_error) {
				for (size_t j = 0; j < i; ++j)
					fclose(fileout[j]);
				for (size_t i = 0; i < cmd->c_argc; ++i)
					free(e_argv[i]);
				cmd_exit = 1;
				continue;
			}
			fileout[i] = tmpfile();
		}

		// Execute regular command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// If input was redirected, read files now
			FILE *filein = NULL;
			_Bool input_error = 0;
			if (cmd->c_input_count > 0) {
				filein = tmpfile(); for (size_t i = 0; i < cmd->c_input_count; ++i) {
					// Get filename (path)
					char *ipath = expandArgument(cmd->c_input_file[i], argc, argv);
					if (ipath == NULL) {
						fprintf(stderr, "%s: error expanding argument, possibly related error message: %m\n", argv[0]);
						input_error = 1;
						break;
					}

					// Open file
					FILE *ifile = fopen(ipath, "r");
					if (ifile == NULL) {
						fprintf(stderr, "%s: %m: %s\n", argv[0], ipath);
						free(ipath);
						input_error = 1;
						break;
					}
					free(ipath);

					// Copy contents to temporary file
					char buffer[TMP_RW_BUFSIZE];
					size_t bytes_read;
					while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, ifile), bytes_read != 0)
						fwrite(buffer, sizeof (char), bytes_read, filein);
					fclose(ifile);
				}
				rewind(filein);
				if (input_error)
					fclose(filein);
			}

			int err = 1;
			if (!input_error) {
				// Change stdin if user redirected input
				if (filein != NULL)
					dup2(fileno(filein), STDIN_FILENO);
				// Change stdout if user redirected output
				if (cmd->c_output_count > 0)
					dup2(fileno(fileout[cmd->c_output_count]), STDOUT_FILENO);

				// TODO consider manual search of the path
				execvp(e_argv[0], e_argv);
				err = errno;
				fprintf(stderr, "%m\n");

				// Close redirected input
				if (filein != NULL)
					fclose(filein);
			}

			if (cmd->c_output_count > 0) {
				for (size_t i = 0; i <= cmd->c_output_count; ++i)
					fclose(fileout[i]);
			}

			// Free memory (I wish this wasn't all duplicated in the child to begin with...)
			for (size_t i = 0; i < cmd->c_argc; ++i)
				free(e_argv[i]);
			history_pool = NULL;

			cmd_exit = err;
			break;
		}

		// While the main process waits for the child to exit
		waitpid(cmd_pid, &cmd_stat, 0);
		cmd_exit = WEXITSTATUS(cmd_stat);
		if (cmd->c_output_count > 0) {
			rewind(fileout[cmd->c_output_count]);
			char buffer[TMP_RW_BUFSIZE];
			size_t bytes_read;
			while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, fileout[cmd->c_output_count]), bytes_read > 0)
				for (size_t i = 0; i < cmd->c_output_count; ++i)
					fwrite(buffer, sizeof (char), bytes_read, fileout[i]);
			for (size_t i = 0; i <= cmd->c_output_count; ++i)
				fclose(fileout[i]);
		}
		for (size_t i = 0; i < cmd->c_argc; ++i)
			free(e_argv[i]);
	}

	if (!subshell && last_cmd->c_buf != NULL)
		free(last_cmd->c_buf);
	commandFree(last_cmd);
	free(last_cmd);

	aliasFree(aliases);

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
	}

	sourceFree(source); // This will close history_pool

	/*if (cmd_pid == 0)
		_Exit(cmd_exit);*/
	return cmd_exit;
}

char *expandArgument(CmdArg arg, int argc, char **argv) {
	switch (arg.type) {
		case ARG_BASIC_STRING:
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
				sprintf(number, "%"PRIu8, cmd_exit);
				return strdup(number);
			}
			if (!strcmp(arg.str, "$")) {
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
			char *sub_argv[sub_count];
			for (size_t i = 0; i < sub_count; ++i) {
				char *e = expandArgument(arg.sub[i], argc, argv);
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
