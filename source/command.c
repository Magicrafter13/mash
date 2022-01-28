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
	new_command->c_type = CMD_REGULAR;
	new_command->c_argc = 0;
	new_command->c_argv = NULL;
	new_command->c_next = NULL;
	new_command->c_if_true = NULL;
	new_command->c_if_false = NULL;
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict stream) {
	cmd->c_len = getline(&cmd->c_buf, &cmd->c_size, stream);
	if (cmd->c_len == -1)
		return -1;

	if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		cmd->c_buf[cmd->c_len-- - 1] = '\0';

	return 0;
}

int commandParse(Command *cmd, FILE *restrict stream) {
	Command *original = cmd;

	// Read line if buffer isn't empty
	if (cmd->c_buf == NULL || cmd->c_buf[0] == '\0')
		if (commandRead(cmd, stream) == -1)
			return -1;
	if (cmd->c_buf[0] == '\n' || cmd->c_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
		cmd->c_buf[0] = '\0';
		cmd->c_type = CMD_EMPTY;
		//cmd->c_argc = 0;
		return 0;
	}

	size_t error_length = 0;
	do {
		// Parse Input (into tokens)
		if (commandTokenize(cmd, cmd->c_buf)) { // Determine tokens and save them into cmd->c_argv
			// Error parsing command.
			original->c_len = error_length + cmd->c_len;
			cmd->c_buf[0] = cmd->c_buf[cmd->c_len];
			return 1;
		}
		error_length += cmd->c_len + 1;

		if (cmd->c_argv[0].type == ARG_BASIC_STRING) {
			if (!strcmp(cmd->c_argv[0].str, "while")) {
				Command *const while_cmd = cmd;
				cmd->c_type = CMD_WHILE;
				cmd->c_if_true = cmd->c_next;
				cmd->c_next = NULL;

				// Make sure we have another command following this
				if (cmd->c_if_true == NULL) {
					cmd->c_if_true = commandInit();
					cmd->c_if_true->c_buf = cmd->c_buf;
					cmd->c_if_true->c_size = cmd->c_size;
				}
				cmd = cmd->c_if_true;

				const int parse_result = commandParse(cmd, stream);
				if (parse_result == -1)
					return -1;
				if (parse_result) {
					original->c_len = error_length + cmd->c_next->c_len;
					return 1;
				}

				// Check if command is do
				if (cmd->c_argv[0].type != ARG_BASIC_STRING || strcmp(cmd->c_argv[0].str, "do")) {
					original->c_len = error_length;
					return 1;
				}
				// Remove do argument
				freeArg(cmd->c_argv[0]);
				for (size_t i = 1; i < cmd->c_argc; ++i)
					cmd->c_argv[i - 1] = cmd->c_argv[i];
				--cmd->c_argc;

				// Parse commands until one has 'done' in it.
				for (int found_done = 0; !found_done;) {
					if (cmd->c_next == NULL) {
						cmd->c_next = commandInit();
						cmd->c_next->c_buf = cmd->c_buf;
						cmd->c_next->c_size = cmd->c_size;

						const int parse_result = commandParse(cmd->c_next, stream);
						if (parse_result == -1)
							return -1;
						if (parse_result) {
							original->c_len = error_length + cmd->c_next->c_len;
							return 1;
						}
					}
					while (cmd->c_next != NULL) {
						Command *const previous = cmd;
						cmd = cmd->c_next;
						if (cmd->c_argc == 0)
							continue;

						// Ignore other argument types
						if (cmd->c_argv[0].type != ARG_BASIC_STRING)
							continue;
						// Ignore if not 'done'
						if (strcmp(cmd->c_argv[0].str, "done"))
							continue;

						// Error if anything came after 'done'
						if (cmd->c_argc != 1) {
							original->c_len = error_length + cmd->c_len;
							cmd->c_buf[0] = cmd->c_buf[cmd->c_len];
							return 1;
						}

						// Point previous to while_cmd, and while_cmd to done's next
						previous->c_next = while_cmd;
						if (cmd->c_next != NULL)
							while_cmd->c_next = cmd->c_next;
						commandFree(cmd);
						cmd = while_cmd;
						while (cmd->c_next != NULL)
							cmd = cmd->c_next;
						found_done = 1;
						break;
					}
				}
			}
			else if (!strcmp(cmd->c_argv[0].str, "if")) {
				Command *const if_cmd = cmd;
				cmd->c_type = CMD_IF;
				cmd->c_if_true = cmd->c_next;
				cmd->c_next = NULL;

				// Make sure we have another command following this
				if (cmd->c_if_true == NULL) {
					cmd->c_if_true = commandInit();
					cmd->c_if_true->c_buf = cmd->c_buf;
					cmd->c_if_true->c_size = cmd->c_size;
				}
				cmd = cmd->c_if_true;

				const int parse_result = commandParse(cmd, stream);
				if (parse_result == -1)
					return -1;
				if (parse_result) {
					original->c_len = error_length + cmd->c_next->c_len;
					return 1;
				}

				// Check if command is then
				if (cmd->c_argv[0].type != ARG_BASIC_STRING || strcmp(cmd->c_argv[0].str, "then")) {
					original->c_len = error_length;
					return 1;
				}
				// Remove then argument
				freeArg(cmd->c_argv[0]);
				for (size_t i = 1; i < cmd->c_argc; ++i)
					cmd->c_argv[i - 1] = cmd->c_argv[i];
				--cmd->c_argc;

				// Parse commands until one has 'fi' in it. (Also check for 'else' and switch to if_false.)
				Command *last_true = NULL;
				for (int found_fi = 0; !found_fi;) {
					if (cmd->c_next == NULL) {
						cmd->c_next = commandInit();
						cmd->c_next->c_buf = cmd->c_buf;
						cmd->c_next->c_size = cmd->c_size;

						const int parse_result = commandParse(cmd->c_next, stream);
						if (parse_result == -1)
							return -1;
						if (parse_result) {
							original->c_len = error_length + cmd->c_next->c_len;
							return 1;
						}
					}
					while (cmd->c_next != NULL) {
						Command *const previous = cmd;
						cmd = cmd->c_next;
						if (cmd->c_argc == 0)
							continue;

						// Ignore other argument types
						if (cmd->c_argv[0].type != ARG_BASIC_STRING)
							continue;
						// Check for else
						if (!strcmp(cmd->c_argv[0].str, "else")) {
							// Check if we've already gotten an else
							if (if_cmd->c_if_false != NULL) {
								original->c_len = error_length;// + cmd->c_len;
								cmd->c_buf[0] = 'e';
								return 1;
							}

							// Save final statement in block, and point if_cmd to else
							last_true = previous;
							last_true->c_next = NULL;
							if_cmd->c_if_false = cmd;

							// Remove 'else' from command
							freeArg(cmd->c_argv[0]);
							for (size_t i = 1; i < cmd->c_argc; ++i)
								cmd->c_argv[i - 1] = cmd->c_argv[i];
							--cmd->c_argc;

							continue;
						}

						// Ignore if not 'fi'
						if (strcmp(cmd->c_argv[0].str, "fi"))
							continue;

						// Error if anything came after 'fi'
						if (cmd->c_argc != 1) {
							original->c_len = error_length;
							cmd->c_buf[0] = cmd->c_argv[1].str[0];
							return 1;
						}

						// Point final statement in true block, and false block, to fi's next
						if_cmd->c_next = cmd->c_next;
						if (last_true != NULL)
							last_true->c_next = cmd->c_next;
						previous->c_next = cmd->c_next;
						cmd->c_next = NULL;
						commandFree(cmd);
						cmd = if_cmd;
						while (cmd->c_next != NULL)
							cmd = cmd->c_next;
						found_fi = 1;
						break;
					}
				}
			}
		}

		//cmd = cmd->c_next;
	}
	while (cmd->c_buf[0] != '\0' && (cmd = cmd->c_next));

	//free(cmd->c_buf); // Free the buffer we read into

	return 0;
}

int commandTokenize(Command *cmd, char *buf) {
	cmd->c_type = CMD_REGULAR;

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

void commandFree(Command *cmd) {
	if (cmd == NULL || cmd->c_type == CMD_FREED)
		return;

	// Stop recursive calls to commandFree from causing a double free
	cmd->c_type = CMD_FREED;

	if (cmd->c_argv != NULL) {
		for (size_t i = 0; i < cmd->c_argc; ++i)
			freeArg(cmd->c_argv[i]);
		free(cmd->c_argv);
		cmd->c_argv = NULL;
	}

	if (cmd->c_next != NULL)
		commandFree(cmd->c_next);
	if (cmd->c_if_true != NULL)
		commandFree(cmd->c_if_true);
	if (cmd->c_if_false != NULL)
		commandFree(cmd->c_if_false);

	free(cmd);
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
