#include <stdlib.h>
#include <string.h>
#include "command.h"

char * const delim_char = " ";

SufTree * builtins;

int commandTokenize(Command*);

void commandSetBuiltins(SufTree *b) {
	builtins = b;
}

Command commandInit() {
	struct _command new_command;
	new_command.c_size = 0;
	new_command.c_buf = NULL;
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict stream) {
	// Read line
	cmd->c_len = getline(&cmd->c_buf, &cmd->c_size, stream);
	if (cmd->c_len == -1)
		return -1;

	// Parse Input (into tokens)
	if (cmd->c_buf[0] == '\n' || cmd->c_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
		cmd->c_type = CMD_EMPTY;
		return 0;
	}

	if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		cmd->c_buf[cmd->c_len-- - 1] = '\0';

	commandTokenize(cmd);

	/*cmd->c_argc = 1; // Get number of tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (cmd->c_buf[i] == delim_char[0])
			cmd->c_argc++;

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (char*)); // Construct argument array
	cmd->c_argv[0] = strtok(cmd->c_buf, delim_char);
	for (size_t t = 1; t < cmd->c_argc; t++)
		cmd->c_argv[t] = strtok(NULL, delim_char);
	cmd->c_argv[cmd->c_argc] = NULL;*/

	// Determine command type
	cmd->c_type = suftreeHas(builtins, cmd->c_argv[0], &cmd->c_builtin) ? CMD_BUILTIN : CMD_REGULAR;

	return 0;
}

int commandTokenize(Command *cmd) {
	cmd->c_argc = 1; // Get maximum number of possible tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (cmd->c_buf[i] == *delim_char)
			cmd->c_argc++;

	size_t token_ends[cmd->c_argc];
	cmd->c_argc = 0;
	for (size_t i = 0; i <= cmd->c_len; i++) {
		switch (cmd->c_buf[i]) {
			case '\0':
			case ' ': // can't use delim_char...
				token_ends[cmd->c_argc++] = i;
				break;
			case '\'':
				while (cmd->c_buf[++i] != '\'');
				token_ends[cmd->c_argc++] = i;
				break;
		}
	}

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (char*));
	cmd->c_argv[cmd->c_argc] = NULL;

	size_t i = 0;
	for (size_t t = 0; t < cmd->c_argc; t++) {
		switch (cmd->c_buf[i]) {
			case '\'':
				cmd->c_argv[t] = &cmd->c_buf[i + 1];
				cmd->c_buf[token_ends[t] - 1] = '\0';
				break;
			default:
				cmd->c_argv[t] = &cmd->c_buf[i];
				cmd->c_buf[token_ends[t]] = '\0';
				break;
		}
		i = token_ends[t] + 1;
	}

	return 0;
}
