#define _POSIX_C_SOURCE 200809L // getline, strndup, strdup
#include "command.h"
#include "compatibility.h"
#include <string.h>

ssize_t lengthRegular(char*);
ssize_t lengthSingleQuote(char*);
ssize_t lengthDoubleQuote(char*);
ssize_t lengthRegInDouble(char *);
ssize_t lengthDollarExp(char*);
int commandTokenize(Command*, char*);

Command *commandInit() {
	Command *new_command = malloc(sizeof (Command));
	*new_command = (Command){
		.c_size = 0,
		.c_buf = NULL,
		.c_type = CMD_EMPTY,
		.c_argc = 0,
		.c_argv = NULL,
		.c_next = NULL,
		.c_if_true = NULL,
		.c_if_false = NULL,
	};
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict istream, FILE *restrict ostream) {
	cmd->c_len = getline(&cmd->c_buf, &cmd->c_size, istream);
	if (cmd->c_len == -1)
		return -1;

	if (ostream != NULL) {
		fwrite(cmd->c_buf, sizeof (char), cmd->c_len, ostream);
		fflush(ostream);
	}

	if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		cmd->c_buf[cmd->c_len-- - 1] = '\0';

	return 0;
}

int commandParse(Command *cmd, FILE *restrict istream, FILE *restrict ostream) {
	Command *original = cmd;

	// Read line if buffer isn't empty
	if (cmd->c_buf == NULL || cmd->c_buf[0] == '\0')
		if (istream == NULL || commandRead(cmd, istream, ostream) == -1)
			return -1;
	if (cmd->c_buf[0] == '\0' || cmd->c_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
		cmd->c_buf[0] = '\0';
		cmd->c_type = CMD_EMPTY;
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
				// Shift out "while" arg
				freeArg(cmd->c_argv[0]);
				for (size_t i = 1; i < cmd->c_argc; ++i)
					cmd->c_argv[i - 1] = cmd->c_argv[i];
				// Abort if no arguments remain
				if (--cmd->c_argc == 0)
					return 1; // TODO set error length thing

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

				const int parse_result = commandParse(cmd, istream, ostream);
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

						const int parse_result = commandParse(cmd->c_next, istream, ostream);
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
				// Shift out "if" arg
				freeArg(cmd->c_argv[0]);
				for (size_t i = 1; i < cmd->c_argc; ++i)
					cmd->c_argv[i - 1] = cmd->c_argv[i];
				// Abort if no arguments remain
				if (--cmd->c_argc == 0)
					return 1; // TODO set error length thing

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

				const int parse_result = commandParse(cmd, istream, ostream);
				if (parse_result == -1)
					return -1;
				if (parse_result) {
					original->c_len = error_length + cmd->c_next->c_len;
					return 1;
				}

				// Check if command is then
				if (cmd->c_argc == 0 || cmd->c_argv[0].type != ARG_BASIC_STRING || strcmp(cmd->c_argv[0].str, "then")) {
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

						const int parse_result = commandParse(cmd->c_next, istream, ostream);
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
	}
	while (cmd->c_buf[0] != '\0' && (cmd = cmd->c_next));

	return 0;
}

ssize_t lengthRegular(char *buf) {
	ssize_t l = 0;
	for (char c; c = buf[l], c != '\0'; ++l) {
		switch (c) {
			case '\'':
			case '"':
			case '$':
			case ' ':
			case '\t':
			case '\n':
			case ';':
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

ssize_t lengthSingleQuote(char *buf) {
	if (buf[0] != '\'')
		return 0;
	ssize_t l = 1;
	for (char c; c = buf[l], c != '\0'; ++l)
		if (c == '\'')
			return l + 1;
	return -l;
}

ssize_t lengthDoubleQuote(char *buf) {
	if (buf[0] != '"')
		return 0;
	ssize_t l = 1;
	for (char c; c = buf[l], c != '"'; ++l) {
		ssize_t temp = 0;
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
		if (temp < 1)
			return temp - l;
		l += temp - 1;
	}
	return l + 1;
}

ssize_t lengthRegInDouble(char *buf) {
	ssize_t l = 0;
	for (char c; c = buf[l], c != '"'; ++l) {
		switch (c) {
			case '\0':
				return -l;
			case '\\':
				++l;
				break;
			case '$':
				return l;
		}
	}
	return l;
}

ssize_t lengthDollarExp(char *buf) {
	if (buf[0] != '$')
		return 0;
	ssize_t l = 1;
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
					ssize_t temp = 1;
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
				if (c >= '0' && c <= '9') {
					if (l == 1) {
						while (c = buf[++l], c >= '0' && c <= '9');
						return l;
					}
				}
				else {
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
						continue;
					return l;
				}
		}
	}
	return l;
}

int commandTokenize(Command *cmd, char *buf) {
	/*
	 * end: current parse index - when finished it will point one char past the end of the command
	 * argc: how many args this command has
	 * done: indicates we've finished parsing this command (there may be more after it)
	 * whitespace: whether or not the last character was whitespace
	 */
	size_t end = 0, argc = 0;
	_Bool done = 0, whitespace = 1;
	while (end <= cmd->c_len && !done) {
		_Bool parse_run = 1;
		switch (buf[end]) {
			case ';': // End of this command
				if (end == 0) {
					cmd->c_len = end;
					return -1;
				}
				++end;
			case '\0':
				if (end == 0)
					return 0;
				done = 1;
				--end;
			case ' ': // can't use delim_char...
			case '\t':
			case '\n':
				if (whitespace == 0) {
					++argc;
					whitespace = 1;
				}
				parse_run = 0;
				break;
			default:
				whitespace = 0;
		}
		if (parse_run) {
			ssize_t len;
			switch (buf[end]) {
				case '\'':
					len = lengthSingleQuote(&buf[end]);
					break;
				case '"':
					len = lengthDoubleQuote(&buf[end]);
					break;
				case '$':
					len = lengthDollarExp(&buf[end]);
					break;
				default:
					len = lengthRegular(&buf[end]);
			}
			if (len < 1) {
				cmd->c_len = end - len;
				return -1;
			}
			end += len;
		}
		else
			++end;
	}
#ifdef DEBUG
	fprintf(stderr, "Argc: %lu\n", argc);
#endif
	cmd->c_argc = argc;
	if (cmd->c_argc == 0) {
		memmove(buf, &buf[end], cmd->c_len - end + 1);
		return 0;
	}
	argc = 0;

	// Allocate space for arguments
	cmd->c_argv = calloc(cmd->c_argc, sizeof (CmdArg));
	for (size_t i = 0; i < cmd->c_argc; ++i)
		cmd->c_argv[i].type = ARG_NULL;

	// Parse and set each argument structure
	done = 0;
	_Bool inDoubleQuote = 0, semicolon = 0;
	for (size_t current = 0; argc < cmd->c_argc && current <= cmd->c_len && !done; current++) {
		_Bool parse_regular = 0;
		CmdArg *cur_arg = &cmd->c_argv[argc], new_arg = { .type = ARG_NULL };
		switch (buf[current]) {
			case '\'': {
				// Treat as normal character
				if (inDoubleQuote) {
					parse_regular = 1;
					break;
				}

				ssize_t quote_len = lengthSingleQuote(&buf[current]);
				if (quote_len < 1) {
					fprintf(stderr, "lenSQuote returned %zd!\nParsed from %zu, with buffer contents: \e[1;31m<\e[0m%s\e[1;31m>\e[0m", quote_len, current, buf);
					return 1;
				}

				new_arg = (CmdArg){ .type = ARG_BASIC_STRING, .str = strndup(&buf[current + 1], quote_len - 2) };
				current += quote_len - 1;
				break;
			}
			case '"':
				inDoubleQuote = inDoubleQuote ? 0 : 1;
				break;
			case '$': {
				size_t dollar_len = lengthDollarExp(&buf[current]);
				if (dollar_len < 1) {
					fprintf(stderr, "lenDExp returned %zd!\nParsed from %zu, with buffer contents: \e[1;31m<\e[0m%s\e[1;31m>\e[0m", dollar_len, current, buf);
					return 1;
				}

				if (dollar_len == 1) {
					new_arg = (CmdArg){ .type = ARG_BASIC_STRING, .str = strdup("$") };
				}
				else {
					if (buf[current + 1] == '(')
						new_arg = (CmdArg){ .type = inDoubleQuote ? ARG_QUOTED_SUBSHELL : ARG_SUBSHELL, .str = strndup(&buf[current + 2], dollar_len - 3) };
					else
						new_arg = (CmdArg){ .type = ARG_VARIABLE, .str = strndup(&buf[current + 1], dollar_len - 1) };
				}
				current += dollar_len - 1;
				break;
			}
			case '~': // TODO: ARG_HOME, for an easy way to do ~username
				if (!inDoubleQuote && cur_arg->type == ARG_NULL)
					*cur_arg = (CmdArg){ .type = ARG_VARIABLE, .str = strdup("HOME") };
				else
					parse_regular = 1;
				break;
			case ';': // End of this command
				if (current == 0) {
					fprintf(stderr, "I don't think this will ever be executed... if you see this, please tell me.\n"); // TODO: remove this eventually
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
					if (cur_arg->type != ARG_NULL)
						++argc;
					break;
				}
				if (done) {
					if (!semicolon)
						break;
					// Ignore semicolon if inside double quote
					done = semicolon = 0;
				}
			default:
				parse_regular = 1;
		}
		if (parse_regular) {
			size_t reg_len = inDoubleQuote ? lengthRegInDouble(&buf[current]) : lengthRegular(&buf[current]);
			if (reg_len < 1) {
				fprintf(stderr, "len%s returned %zd!\nParsed from %zu, with buffer contents: \e[1;31m<\e[0m%s\e[1;31m>\e[0m", inDoubleQuote ? "RnD" : "R", reg_len, current, buf);
				return 1;
			}

			new_arg = (CmdArg){ .type = ARG_BASIC_STRING, .str = strndup(&buf[current], reg_len) };
			current += reg_len - 1;
		}
		if (new_arg.type != ARG_NULL) {
			switch (cur_arg->type) {
				case ARG_NULL:
					*cur_arg = new_arg;
					break;
				case ARG_COMPLEX_STRING: {
					size_t arr_len = 0;
					while (cur_arg->sub[arr_len].type != ARG_NULL)
						++arr_len;
					cur_arg->sub = reallocarray(cur_arg->sub, arr_len + 2, sizeof (CmdArg));
					cur_arg->sub[arr_len + 1] = cur_arg->sub[arr_len];
					cur_arg->sub[arr_len] = new_arg;
					break;
				}
				default: {
					CmdArg new_complex = { .type = ARG_COMPLEX_STRING, .sub = calloc(3, sizeof (CmdArg)) };
					new_complex.sub[0] = *cur_arg;
					new_complex.sub[1] = new_arg;
					new_complex.sub[2] = (CmdArg){ .type = ARG_NULL };
					*cur_arg = new_complex;
				}
			}
		}
	}

	// Finish up with command, and initialize next if applicable
	memmove(buf, &buf[end], cmd->c_len - end + 1); // Remove command from buffer
	cmd->c_type = CMD_REGULAR;
	if (semicolon) {
		cmd->c_next = commandInit();
		cmd->c_next->c_buf = cmd->c_buf;
		cmd->c_next->c_size = cmd->c_size;
		cmd->c_next->c_len = cmd->c_len - end;
		cmd->c_len = end;
	}

	return 0;
}

CmdArg argdup(CmdArg a) {
	switch (a.type) {
		case ARG_BASIC_STRING:
		case ARG_VARIABLE:
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL:
			return (CmdArg){ .type = a.type, .str = strdup(a.str) };
		case ARG_COMPLEX_STRING: {
			size_t sub_len = 0;
			while (a.sub[sub_len++].type != ARG_NULL);
			CmdArg new_arg = { .type = ARG_COMPLEX_STRING, .sub = calloc(sub_len, sizeof (CmdArg)) };
			for (size_t i = 0; i <= sub_len; ++i)
				new_arg.sub[i] = argdup(a.sub[i]);
			return new_arg;
		}
		case ARG_NULL:
			return (CmdArg){ .type = ARG_NULL };
	}
	return (CmdArg){};
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

void freeArg(CmdArg a) {
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
