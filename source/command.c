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

Command *commandInit() {
	struct _command *new_command = malloc(sizeof (Command));
	new_command->c_size = 0;
	new_command->c_buf = NULL;
	new_command->c_next = NULL;
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict stream) {
	Command *original = cmd;

	// Read line
	cmd->c_len = getline(&cmd->c_buf, &cmd->c_size, stream);
	if (cmd->c_len == -1) {
		//free(cmd->c_buf);
		return -1;
	}

	size_t error_length = 0;
	do {
		// Parse Input (into tokens)
		if (cmd->c_buf[0] == '\n' || cmd->c_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
			free(cmd->c_buf);
			cmd->c_type = CMD_EMPTY;
			return 0;
		}
		if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
			cmd->c_buf[cmd->c_len-- - 1] = '\0';
		if (commandTokenize(cmd, cmd->c_buf)) { // Determine tokens and save them into cmd->c_argv
			// Error parsing command.
			original->c_len = error_length + cmd->c_len;
			cmd->c_buf[0] = cmd->c_buf[cmd->c_len];
			return 1;
		}
		error_length += cmd->c_len + 1;

		// Determine command type
		if (cmd->c_argv[0].type == ARG_BASIC_STRING && !strcmp(cmd->c_argv[0].str, "exit"))
			cmd->c_type = CMD_EXIT;
		/*else if (!strcmp(cmd->c_argv[0], "if")) {
			cmd->c_type = CMD_BUILTIN;

			cmd->c_next = commandInit();
			*cmd->c_next = *cmd;
			++cmd->c_next->c_argv;

			cmd->c_if_true = commandInit();
			cmd->c_if_false = commandInit();
		}*/
		else {
			if (cmd->c_argv[0].type != ARG_BASIC_STRING)
				cmd->c_type = CMD_INDETERMINATE;
			else
				cmd->c_type = CMD_REGULAR;
		}

		//cmd = cmd->c_next;
	}
	while (cmd->c_buf[0] != '\0' && (cmd = cmd->c_next));

	//free(cmd->c_buf); // Free the buffer we read into

	return 0;
}

int commandTokenize(Command *cmd, char *buf) {
	cmd->c_argc = 1; // Get maximum number of possible tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (buf[i] == *delim_char)
			cmd->c_argc++;

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (struct _arg));

	cmd->c_argc = 0;
	//char temp_buf[512];
	int semicolon = 0;
	size_t current = 0;
	for (size_t next = 0, start; current <= cmd->c_len && !semicolon; current++) {
		switch (buf[current]) {
			case ';': // End of this command
				if (current == 0) {
					cmd->c_len = 0;
					return -1;
				}
				semicolon = 1;
			case ' ': // can't use delim_char...
				buf[current] = '\0';
			case '\0':
				if (current == next) {
					next = current + 1;
					continue;
				}
				cmd->c_argv[cmd->c_argc++] = (struct _arg){ .type = ARG_BASIC_STRING, .str = strndup(&buf[next], current - next)};
				next = current + 1;
				break;
			case '\'':
				start = current;
				while (buf[++current] != '\'');
				cmd->c_argv[cmd->c_argc++] = (struct _arg) { .type = ARG_BASIC_STRING, .str = strndup(&buf[start + 1], current - start - 1) };
				next = current + 1;
				break;
			case '$':
				start = ++current;
				while (buf[current] != ' ' && buf[current] != '\0')
					current++;
				buf[current] = '\0';
				cmd->c_argv[cmd->c_argc++] = (struct _arg){ .type = ARG_VARIABLE, .str = strdup(&buf[start]) };
				next = current + 1;
				break;
		}
	}
	cmd->c_argv[cmd->c_argc].type = ARG_NULL;

	if (semicolon)
		++current;
	memmove(buf, &buf[current - 1], cmd->c_len - current + 2);
	if (semicolon) {
		cmd->c_next = commandInit();
		cmd->c_next->c_buf = cmd->c_buf;
		cmd->c_next->c_size = cmd->c_size;
		cmd->c_next->c_len = cmd->c_len - current + 1;
		cmd->c_len = current - 2;
		/*cmd->c_next->c_len = cmd->c_len - current + 1;
		cmd->c_len = current - 2;*/
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

void freeArg(struct _arg a) {
	switch (a.type) {
		case ARG_BASIC_STRING:
		case ARG_VARIABLE:
			free(a.str);
			break;
		case ARG_COMPLEX_STRING:
			for (size_t i = 0; a.sub[i].type != ARG_NULL; ++i)
				freeArg(a.sub[i]);
			break;
		case ARG_NULL:
		default:
			break;
	}
}
