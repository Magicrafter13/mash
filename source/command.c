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
	new_command.c_next = NULL;
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict stream) {
	/*if (cmd->c_next != NULL) {
		commandFree(cmd->c_next);
		cmd->c_next = NULL;
	}*/

	// Read line
	static char *temp_buf;
	cmd->c_len = getline(&temp_buf, &(size_t){ 0 }, stream);
	if (cmd->c_len == -1) {
		free(temp_buf);
		return -1;
	}

	do {
		// Parse Input (into tokens)
		if (temp_buf[0] == '\n' || temp_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
			free(temp_buf);
			cmd->c_type = CMD_EMPTY;
			return 0;
		}
		if (temp_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
			temp_buf[cmd->c_len-- - 1] = '\0';
		commandTokenize(cmd, temp_buf); // Determine tokens and save them into cmd->c_argv

		// Determine command type
		if (!strcmp(cmd->c_argv[0], "exit"))
			cmd->c_type = CMD_EXIT;
		else
			cmd->c_type = suftreeHas(builtins, cmd->c_argv[0], &cmd->c_builtin) ? CMD_BUILTIN : CMD_REGULAR;

		cmd = cmd->c_next;
	}
	while (temp_buf[0] != '\0');

	free(temp_buf); // Free the buffer we read into

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
	int semicolon = 0;
	size_t current = 0;
	for (size_t next = 0, start; current <= cmd->c_len && !semicolon; current++) {
		switch (buf[current]) {
			case ';': // End of this command
				semicolon = 1;
			case ' ': // can't use delim_char...
				buf[current] = '\0';
			case '\0':
				if (current == next) {
					next = current + 1;
					continue;
				}
				cmd->c_argv[cmd->c_argc++] = strndup(&buf[next], current - next);
				next = current + 1;
				break;
			case '\'':
				start = current;
				while (buf[++current] != '\'');
				cmd->c_argv[cmd->c_argc++] = strndup(&buf[start + 1], current - start - 1);
				next = current + 1;
				break;
			case '$':
				start = ++current;
				while (buf[current] != ' ' && buf[current] != '\0')
					current++;
				buf[current] = '\0';
				if (!strcmp(&buf[start], "RANDOM")) {
					char number[12];
					sprintf(number, "%lu", random());
					cmd->c_argv[cmd->c_argc++] = strdup(number);
				}
				else {
					char *value = getVarFunc(&buf[start]);
					cmd->c_argv[cmd->c_argc++] = strdup(value == NULL ? "" : value);
				}
				next = current + 1;
				break;
		}
	}

	if (semicolon)
		++current;
	memmove(buf, &buf[current - 1], cmd->c_len - current + 2);
	if (semicolon) {
		cmd->c_next = malloc(sizeof (Command));
		*cmd->c_next = commandInit();
		cmd->c_next->c_len = cmd->c_len - current + 1;
		cmd->c_len = current - 2;
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

void commandFree(Command *cmd) {
	if (cmd->c_argv != NULL)
		free(cmd->c_argv);
	if (cmd->c_next != NULL)
		commandFree(cmd->c_next);
	free(cmd);
}
