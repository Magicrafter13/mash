#include <stdlib.h>
#include <string.h>
#include "command.h"

char * const delim_char = " ";

SufTree * builtins;

char *(*getVarFunc)(const char*);

size_t lengthRegular(char*);
size_t lengthSingleQuote(char*);
size_t lengthDoubleQuote(char*);
size_t lengthRegInDouble(char *);
size_t lengthDollarExp(char*);
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
		if (stream == NULL || commandRead(cmd, stream) == -1)
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

						// Ignore irrelevant command types
						if (cmd->c_type != CMD_REGULAR)
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

						// Point final statement in true block, and false block, to fi (which will become a CMD_EMPTY)
						if_cmd->c_next = cmd;
						if (last_true != NULL)
							last_true->c_next = cmd;
						previous->c_next = cmd;
						cmd->c_type = CMD_EMPTY;
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

size_t lengthRegular(char *buf) {
	size_t l = 0;
	for (char c; c = buf[l], c != '\0'; ++l) {
		switch (c) {
			case '\'':
			case '"':
			case '$':
			case ' ':
			case '\t':
			case '\n':
				return l;
			case '\\':
				c = buf[++l];
				switch (c) {
					case ' ':
					case '\\':
						break;
					default:
						return l - 1;
				}
		}
	}
	return l;
}
size_t lengthSingleQuote(char *buf) {
	char c;
	if (buf[0] == '\'')
		for (size_t l = 1; c = buf[l], c != '\0'; ++l)
			if (c == '\'')
				return l + 1;
	return 0;
}

size_t lengthDoubleQuote(char *buf) {
	if (buf[0] != '"')
		return 0;
	size_t l = 1;
	for (char c; c = buf[l], c != '"'; ++l) {
		size_t temp = 0;
		switch (c) {
			case '\0':
				temp = 0;
				break;
			case '$':
				temp = lengthDollarExp(&buf[l]);
				break;
			default:
				temp = lengthRegInDouble(&buf[l]);
				break;
		}
		if (temp == 0)
			return 0;
		l += temp;
	}
	return l;
}

size_t lengthRegInDouble(char *buf) {
	size_t l = 0;
	for (char c; c = buf[l], c != '"'; ++l) {
		switch (c) {
			case '\0':
				return 0;
			case '\\':
				++l;
				break;
			case '$':
				return l;
		}
	}
	return l;
}

size_t lengthDollarExp(char *buf) {
	if (buf[0] != '$')
		return 0;
	size_t l = 1;
	for (char c; c = buf[l], c != '\0'; ++l) {
		switch (c) {
			case '$':
			case '?':
				return l > 1 ? l : 2;
			case '(':
				if (l > 1)
					return l;
				++l;
				while (c = buf[l], c != ')') {
					size_t temp = 1;
					switch (c) {
						case '\0':
							temp = 0;
							break;
						case '\'':
							temp = lengthSingleQuote(&buf[l]);
							break;
						case '$':
							temp = lengthDollarExp(&buf[l]);
							break;
						case '"':
							temp = lengthDoubleQuote(&buf[l]);
							break;
					}
					if (temp == 0)
						return 0;
					l += temp;
				}
				return l + 1;
			default:
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
					continue;
				return l;
		}
	}
	return l;
}

int commandTokenize(Command *cmd, char *buf) {
	cmd->c_type = CMD_REGULAR;

	cmd->c_argc = 1; // Get maximum number of possible tokens
	for (size_t i = 0; i < cmd->c_len; i++)
		if (buf[i] == *delim_char)
			cmd->c_argc++;

	cmd->c_argv = calloc(cmd->c_argc + 1, sizeof (struct _arg));
	for (size_t i = 0; i < cmd->c_argc + 1; ++i)
		cmd->c_argv[i].type = ARG_NULL;

	cmd->c_argc = 0;
	int done = 0, semicolon = 0, inDoubleQuote = 0;
	size_t current;
	for (current = 0; current <= cmd->c_len && !done; current++) {
		switch (buf[current]) {
			case '\'': {
				if (inDoubleQuote)
					goto stupid_single_quote_goto;
				size_t quote_len = lengthSingleQuote(&buf[current]);
				// TODO: HANDLE UNCLOSED QUOTE!
				/*if (quote_len == 0) {
					// do what?
				}*/
				struct _arg new_arg = { .type = ARG_BASIC_STRING, .str = strndup(&buf[current + 1], quote_len - 2) };
				struct _arg *cur_arg = &cmd->c_argv[cmd->c_argc];
				switch (cur_arg->type) {
					case ARG_NULL:
						*cur_arg = new_arg;
						break;
					case ARG_COMPLEX_STRING: {
						size_t arr_len = 0;
						while (cur_arg->sub[arr_len].type != ARG_NULL)
							++arr_len;
						cur_arg->sub = reallocarray(cur_arg->sub, arr_len + 2, sizeof (struct _arg));
						cur_arg->sub[arr_len + 1] = cur_arg->sub[arr_len];
						cur_arg->sub[arr_len] = new_arg;
						break;
					}
					default: {
						struct _arg new_complex = { .type = ARG_COMPLEX_STRING, .sub = calloc(3, sizeof (struct _arg)) };
						new_complex.sub[0] = *cur_arg;
						new_complex.sub[1] = new_arg;
						new_complex.sub[2] = (struct _arg){ .type = ARG_NULL };
						*cur_arg = new_complex;
					}
				}

				current += quote_len - 1;
				break;
			}
			case '"':
				inDoubleQuote = inDoubleQuote ? 0 : 1;
				break;
			case '$': {
				size_t dollar_len = lengthDollarExp(&buf[current]);
				struct _arg new_arg;
				if (dollar_len == 1) {
					new_arg = (struct _arg){ .type = ARG_BASIC_STRING, .str = strdup("$") };
				}
				else {
					if (buf[current + 1] == '(')
						new_arg = (struct _arg){ .type = inDoubleQuote ? ARG_QUOTED_SUBSHELL : ARG_SUBSHELL, .str = strndup(&buf[current + 2], dollar_len - 3) };
					else
						new_arg = (struct _arg){ .type = ARG_VARIABLE, .str = strndup(&buf[current + 1], dollar_len - 1) };
				}
				// TODO: HANDLE BAD DOLLAR EXP
				struct _arg *cur_arg = &cmd->c_argv[cmd->c_argc];
				switch (cur_arg->type) {
					case ARG_NULL:
						*cur_arg = new_arg;
						break;
					case ARG_COMPLEX_STRING: {
						size_t arr_len = 0;
						while (cur_arg->sub[arr_len].type != ARG_NULL)
							++arr_len;
						cur_arg->sub = reallocarray(cur_arg->sub, arr_len + 2, sizeof (struct _arg));
						cur_arg->sub[arr_len + 1] = cur_arg->sub[arr_len];
						cur_arg->sub[arr_len] = new_arg;
						break;
					}
					default: {
						struct _arg new_complex = { .type = ARG_COMPLEX_STRING, .sub = calloc(3, sizeof (struct _arg)) };
						new_complex.sub[0] = *cur_arg;
						new_complex.sub[1] = new_arg;
						new_complex.sub[2] = (struct _arg){ .type = ARG_NULL };
						*cur_arg = new_complex;
					}
				}

				current += dollar_len - 1;
				break;
			}
			case ';': // End of this command
				if (current == 0) {
					cmd->c_len = 0;
					return -1;
				}
				semicolon = 1;
			case '\0':
				done = 1;
			case ' ': // can't use delim_char...
			case '\t':
			case '\n':
				if (!inDoubleQuote) {
					if (cmd->c_argv[cmd->c_argc].type != ARG_NULL)
						++cmd->c_argc;
					break;
				}
				if (done) {
					if (!semicolon)
						break;
					done = semicolon = 0;
				}
			default: {
stupid_single_quote_goto:;
				size_t reg_len = inDoubleQuote ? lengthRegInDouble(&buf[current]) : lengthRegular(&buf[current]);
				struct _arg new_arg = (struct _arg){ .type = ARG_BASIC_STRING, .str = strndup(&buf[current], reg_len) };
				// TODO: HANDLE 0?
				struct _arg *cur_arg = &cmd->c_argv[cmd->c_argc];
				switch (cur_arg->type) {
					case ARG_NULL:
						*cur_arg = new_arg;
						break;
					case ARG_COMPLEX_STRING: {
						size_t arr_len = 0;
						while (cur_arg->sub[arr_len].type != ARG_NULL)
							++arr_len;
						cur_arg->sub = reallocarray(cur_arg->sub, arr_len + 2, sizeof (struct _arg));
						cur_arg->sub[arr_len + 1] = cur_arg->sub[arr_len];
						cur_arg->sub[arr_len] = new_arg;
						break;
					}
					default: {
						struct _arg new_complex = { .type = ARG_COMPLEX_STRING, .sub = calloc(3, sizeof (struct _arg)) };
						new_complex.sub[0] = *cur_arg;
						new_complex.sub[1] = new_arg;
						new_complex.sub[2] = (struct _arg){ .type = ARG_NULL };
						*cur_arg = new_complex;
					}
				}

				current += reg_len - 1;
				break;
			}
		}
	}
	--current;

	if (semicolon)
		++current;
	memmove(buf, &buf[current], cmd->c_len - current + 1);
	if (semicolon) {
		cmd->c_next = commandInit();
		cmd->c_next->c_buf = cmd->c_buf;
		cmd->c_next->c_size = cmd->c_size;
		cmd->c_next->c_len -= current;
		cmd->c_len = current;
	}

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
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL:
			free(a.str);
			break;
		case ARG_COMPLEX_STRING:
			for (size_t i = 0; a.sub[i].type != ARG_NULL; ++i)
				freeArg(a.sub[i]);
			free(a.sub);
			break;
		case ARG_NULL:
		default:
			break;
	}
}
