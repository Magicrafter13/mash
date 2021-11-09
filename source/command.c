#include <stdlib.h>
#include <string.h>
#include "command.h"

char * const delim_char = " ";

SufTree * builtins;

char *(*getVarFunc)(const char*);

int commandTokenize(Command*, char*);

void commandSetBuiltins(SufTree *b) {
	builtins = b;
}

void commandSetVarFunc(char *(*func)(const char*)) {
	getVarFunc = func;
}

Command commandInit() {
	struct _command new_command;
	new_command.c_size = 0;
	new_command.c_buf = NULL;
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict stream) {
	// Read line
	static char *temp_buf;
	cmd->c_len = getline(&temp_buf, &(size_t){ 0 }, stream);
	if (cmd->c_len == -1) {
		free(temp_buf);
		return -1;
	}

	// Parse Input (into tokens)
	if (temp_buf[0] == '\n' || temp_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
		free(temp_buf);
		cmd->c_type = CMD_EMPTY;
		return 0;
	}
	if (temp_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		temp_buf[cmd->c_len-- - 1] = '\0';
	commandTokenize(cmd, temp_buf); // Determine tokens and save them into cmd->c_argv
	free(temp_buf); // Free the buffer we read into

	// Determine command type
	if (!strcmp(cmd->c_argv[0], "exit"))
		cmd->c_type = CMD_EXIT;
	else
		cmd->c_type = suftreeHas(builtins, cmd->c_argv[0], &cmd->c_builtin) ? CMD_BUILTIN : CMD_REGULAR;

	return 0;
}

int commandTokenize(Command *cmd, char *buf) {
	cmd->c_argc = 1; // Get maximum number of possible tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (buf[i] == *delim_char)
			cmd->c_argc++;

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (char*));
	cmd->c_argv[cmd->c_argc] = NULL;

	cmd->c_argc = 0;
	char temp_buf[512];
	for (size_t i = 0, j = 0, k; i <= cmd->c_len; i++) {
		switch (buf[i]) {
			case ' ': // can't use delim_char...
				buf[i] = '\0';
			case '\0':
				if (i == j) {
					j = i + 1;
					continue;
				}
				cmd->c_argv[cmd->c_argc++] = strndup(&buf[j], i - j);
				j = i + 1;
				break;
			case '\'':
				k = i;
				while (buf[++i] != '\'');
				cmd->c_argv[cmd->c_argc++] = strndup(&buf[k + 1], i - k - 1);
				j = i + 1;
				break;
			case '$':
				k = ++i;
				while (buf[i] != ' ' && buf[i] != '\0')
					i++;
				buf[i] = '\0';
				char *value = getVarFunc(&buf[k]);
				cmd->c_argv[cmd->c_argc++] = strdup(value == NULL ? "" : value);
				j = i + 1;
				break;
		}
	}

	/*size_t i = 0;
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
	}*/

	return 0;
}
