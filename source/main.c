#define _POSIX_C_SOURCE 200809L // fileno, strdup
#define _DEFAULT_SOURCE // srandom
#include "mash.h"
#include "suftree.h"
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char *argv[]) {
	uint8_t cmd_exit;

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
		FILE *config = open_config(PASSWD, argv[0]);
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
	AliasMap *aliases = aliasInit();
	Variables *vars = variableInit();

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
			if (interactive && source->input == stdin && (last_cmd->c_size > 0 ? last_cmd->c_buf[0] == '\0' : 1)) {
				char *PROMPTCMD = getvar(vars, "PROMPT_COMMAND");
				if (PROMPTCMD != NULL) {
					Command promptcmd = { .c_len = strlen(PROMPTCMD), .c_buf = strdup(PROMPTCMD) };
					int parse_result = commandParse(&promptcmd, NULL, NULL);
					_Bool isChild = 0;
					switch (parse_result) {
						case -1:
							if (errno > 11)
								fprintf(stderr, "%s: %m\n", source->argv[0]);
							else
								fprintf(stderr, "%s: PROMPT_COMMAND: syntax error, command not complete\n", source->argv[0]);
							break;
						case 0:
							if (commandExecute(&promptcmd, aliases, &source, vars, &history_pool, &cmd_exit, &builtins) == -1)
								isChild = 1;
							break;
						default:
							fprintf(stderr, "   %*s\n", (int)promptcmd.c_len, "^");
							fprintf(stderr, "%s: PROMPT_COMMAND: parse error near `%c'\n", argv[0], promptcmd.c_buf[0]);
							break;
					}
					commandFree(&promptcmd);
					free(promptcmd.c_buf);
					if (isChild)
						break;
				}
				printPrompt(vars, source, PASSWD, UID);
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
			Command *previous = cmd;
			closeIOFiles(&cmd->c_io);
			switch (cmd->c_type) {
				case CMD_FREED:
					fprintf(stderr, "CMD_FREED encountered!\n");
					break;
				case CMD_WHILE:
					if (!cmd_exit)
						cmd = cmd->c_if_true;
					else {
						closeIOFiles(&cmd->c_block_io);
						cmd = cmd->c_next;
					}
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
			// TODO this is ugly, I hate it, and it has a bug
			if ((previous->c_type == CMD_IF && previous->c_next == cmd)) // Last command was if, and it failed
				closeIOFiles(&previous->c_block_io);
			else {
				if (previous->c_parent != NULL && previous->c_parent->c_type == CMD_IF && previous->c_parent->c_next == cmd)
					closeIOFiles(&previous->c_block_io);
			}
			// If command is now NULL, we need to jump back to the top so we can free memory
			if (cmd == NULL)
				continue;
		}

		/*
		 * Execute command
		 */
		if (commandExecute(cmd, aliases, &source, vars, &history_pool, &cmd_exit, &builtins) == -1)
			break;
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
		FILE *history = open_history(PASSWD, argv[0], vars);
		if (history != NULL) {
			rewind(history_pool);
			char buffer[TMP_RW_BUFSIZE];
			size_t bytes_read;
			while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, history_pool), bytes_read != 0)
				fwrite(buffer, sizeof (char), bytes_read, history);
			fclose(history);
		}
	}

	variableFree(vars);
	sourceFree(source); // This will close history_pool

	return cmd_exit;
}
