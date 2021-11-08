#include <stdlib.h>
#include <string.h>
#include "command.h"

char * const delim_char = " ";

SufTree * builtins;

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
	if (cmd->c_buf[0] == '\n') { // Blank input, just ignore and print another prompt.
		cmd->c_type = CMD_EMPTY;
		return 0;
	}

	if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		cmd->c_buf[cmd->c_len-- - 1] = '\0';

	cmd->c_argc = 1; // Get number of tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (cmd->c_buf[i] == delim_char[0])
			cmd->c_argc++;

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (char*)); // Construct argument array
	cmd->c_argv[0] = strtok(cmd->c_buf, delim_char);
	for (size_t t = 1; t < cmd->c_argc; t++)
		cmd->c_argv[t] = strtok(NULL, delim_char);
	cmd->c_argv[cmd->c_argc] = NULL;

	// Determine command type
	cmd->c_type = suftreeHas(builtins, cmd->c_argv[0], &cmd->c_builtin) ? CMD_BUILTIN : CMD_REGULAR;

	return 0;
}
