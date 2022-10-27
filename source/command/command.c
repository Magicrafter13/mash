#define _POSIX_C_SOURCE 200809L // getline, strndup, strdup
#include "command.h"
#include "compatibility.h"
#include <readline/readline.h>
#include <string.h>

#define dupSpecialCommand(cmd) { \
	Command *new_loc = commandInit(); \
	*new_loc = *cmd; \
\
	*cmd = (Command){}; /* Zero out everything */ \
	cmd->c_len = new_loc->c_len; \
	cmd->c_size = new_loc->c_size; \
	cmd->c_buf = new_loc->c_buf; \
	cmd->c_next = new_loc; \
}

size_t shiftArg(Command *cmd) {
	freeArg(cmd->c_argv[0]);
	for (size_t i = 1; i < cmd->c_argc; ++i)
		cmd->c_argv[i - 1] = cmd->c_argv[i];
	return --cmd->c_argc;
}

int parseMultiline(Command *cmd, FILE *restrict istream, FILE *restrict ostream, AliasMap *aliases, char *PROMPT) {
	if (cmd->c_argc < 1 || cmd->c_argv[0].type != ARG_BASIC_STRING)
		return 0;

	if (!strcmp(cmd->c_argv[0].str, "do")) {
		if (cmd->c_argc == 1)
			cmd->c_type = CMD_DO;
		else {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
			cmd->c_type = CMD_DO;

			int ret = parseMultiline(cmd->c_next, istream, ostream, aliases, PROMPT);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_THEN:
					case CMD_ELSE:
					case CMD_FI:
						ret = 1;
						break;
					default:
						// Attempt to parse aliases
						if (aliases != NULL)
							aliasResolve(aliases, cmd->c_next);
						break;
				}
			}
			return ret;
		}
	}
	else if (!strcmp(cmd->c_argv[0].str, "then")) {
		if (cmd->c_argc == 1)
			cmd->c_type = CMD_THEN;
		else {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
			cmd->c_type = CMD_THEN;

			int ret = parseMultiline(cmd->c_next, istream, ostream, aliases, PROMPT);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_DONE:
					case CMD_THEN:
						ret = 1;
						break;
					default:
						// Attempt to parse aliases
						if (aliases != NULL)
							aliasResolve(aliases, cmd->c_next);
						break;
				}
			}
			return ret;
		}
	}
	else if (!strcmp(cmd->c_argv[0].str, "else")) {
		if (cmd->c_argc == 1)
			cmd->c_type = CMD_ELSE;
		else {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
			cmd->c_type = CMD_ELSE;

			int ret = parseMultiline(cmd->c_next, istream, ostream, aliases, PROMPT);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_DONE:
					case CMD_THEN:
					case CMD_ELSE:
						ret = 1;
						break;
					default:
						// Attempt to parse aliases
						if (aliases != NULL)
							aliasResolve(aliases, cmd->c_next);
						break;
				}
			}
			return ret;
		}
	}
	else if (!strcmp(cmd->c_argv[0].str, "done")) {
		if (cmd->c_argc > 1) {
			// TODO error length
			return 1;
		}
		cmd->c_type = CMD_DONE;
	}
	else if (!strcmp(cmd->c_argv[0].str, "fi")) {
		if (cmd->c_argc > 1) {
			// TODO error length
			return 1;
		}
		cmd->c_type = CMD_FI;
	}
	else if (!strcmp(cmd->c_argv[0].str, "while")) {
		// Shift out "while" arg if it is not alone
		if (cmd->c_argc > 1) {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
		}
		else {
			// Error if while had no extra arguments yet contained a pipe!
			if (cmd->c_io.out_pipe) { // Could test if c_next != NULL but this seems cleaner
				cmd->c_buf[0] = '|';
				return 1;
			}
			// Error if while had no extra arguments yet contained < or > redirection
			if (cmd->c_io.in_count > 0) {
				cmd->c_buf[0] = '<';
				return 1;
			}
			if (cmd->c_io.out_count > 0) {
				cmd->c_buf[0] = '>';
				return 1;
			}
		}

		// Save pointer to while command, and alloc new command for "do"
		cmd->c_type = CMD_WHILE;
		Command *const while_cmd = cmd, *test_cmd = cmd->c_next;
		// Parse anything that came after "while"
		if (test_cmd != NULL) {
			int res = parseMultiline(test_cmd, istream, ostream, aliases, PROMPT);
			if (res != 0)
				return res;
			test_cmd->c_parent = while_cmd;
			// Attempt to parse aliases
			if (aliases != NULL)
				aliasResolve(aliases, cmd->c_next);
			while (test_cmd->c_next != NULL) {
				test_cmd = test_cmd->c_next;
				test_cmd->c_parent = while_cmd;
			}
		}
		else
			test_cmd = while_cmd;

		// Read commands until "do"
		for (;;) {
			cmd = commandInit();
			cmd->c_len = test_cmd->c_len;
			cmd->c_size = test_cmd->c_size;
			cmd->c_buf = test_cmd->c_buf;

			const int parse_result = commandParse(cmd, istream, ostream, aliases, PROMPT);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				while_cmd->c_len = cmd->c_len;
				commandFree(cmd);
				free(cmd);
				return 1;
			}
			if (cmd->c_buf != while_cmd->c_buf) {
				while_cmd->c_size = cmd->c_size;
				while_cmd->c_buf = cmd->c_buf;
			}
			if (cmd->c_type == CMD_DO)
				break;
			cmd->c_parent = while_cmd;
			test_cmd->c_next = cmd;
			test_cmd = cmd;
			while (test_cmd->c_next != NULL) {
				test_cmd = test_cmd->c_next;
				test_cmd->c_parent = while_cmd;
			}
		}
		while_cmd->c_if_true = cmd; // CMD_DO
		while_cmd->c_cmds = while_cmd->c_next;
		while_cmd->c_next = NULL;
		// Jump over do's command chain (if it has one)
		while (cmd->c_next != NULL) {
			cmd = cmd->c_next;
			cmd->c_parent = while_cmd;
		}

		// Read commands until "done"
		Command *body_cmd = cmd;
		for (;;) {
			cmd = commandInit();
			cmd->c_len = body_cmd->c_len;
			cmd->c_size = body_cmd->c_size;
			cmd->c_buf = body_cmd->c_buf;

			const int parse_result = commandParse(cmd, istream, ostream, aliases, PROMPT);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				while_cmd->c_len = cmd->c_len;
				commandFree(cmd);
				free(cmd);
				return 1;
			}
			if (cmd->c_buf != while_cmd->c_buf) {
				while_cmd->c_size = cmd->c_size;
				while_cmd->c_buf = cmd->c_buf;
			}
			if (cmd->c_type == CMD_DONE)
				break;
			cmd->c_parent = while_cmd;
			body_cmd->c_next = cmd;
			while (body_cmd->c_next != NULL)
				body_cmd = body_cmd->c_next;
		}
		while_cmd->c_next = cmd; // CMD_DONE
		while_cmd->c_io = cmd->c_io;
		cmd->c_io = (CmdIO){};
	}
	else if (!strcmp(cmd->c_argv[0].str, "if")) {
		// Shift out "if" arg if it is not alone
		if (cmd->c_argc > 1) {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
		}
		else {
			// Error if while had no extra arguments yet contained a pipe!
			if (cmd->c_io.out_pipe) { // Could test if c_next != NULL but this seems cleaner
				cmd->c_buf[0] = '|';
				return 1;
			}
			// Error if while had no extra arguments yet contained < or > redirection
			if (cmd->c_io.in_count > 0) {
				cmd->c_buf[0] = '<';
				return 1;
			}
			if (cmd->c_io.out_count > 0) {
				cmd->c_buf[0] = '>';
				return 1;
			}
		}

		// Save pointer to if command, and alloc new command for "then"
		cmd->c_type = CMD_IF;
		Command *const if_cmd = cmd, *test_cmd = cmd->c_next;
		// Parse anything that came after "if"
		if (test_cmd != NULL) {
			int res = parseMultiline(test_cmd, istream, ostream, aliases, PROMPT);
			if (res != 0)
				return res;
			// Attempt to parse aliases
			if (aliases != NULL)
				aliasResolve(aliases, cmd->c_next);
			test_cmd->c_parent = if_cmd;
			while (test_cmd->c_next != NULL) {
				test_cmd = test_cmd->c_next;
				test_cmd->c_parent = if_cmd;
			}
		}
		else
			test_cmd = if_cmd;

		// Read commands until "then"
		for (;;) {
			cmd = commandInit();
			cmd->c_len = test_cmd->c_len;
			cmd->c_size = test_cmd->c_size;
			cmd->c_buf = test_cmd->c_buf;

			const int parse_result = commandParse(cmd, istream, ostream, aliases, PROMPT);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				if_cmd->c_len = cmd->c_len;
				commandFree(cmd);
				free(cmd);
				return 1;
			}
			if (cmd->c_buf != if_cmd->c_buf) {
				if_cmd->c_size = cmd->c_size;
				if_cmd->c_buf = cmd->c_buf;
			}
			if (cmd->c_type == CMD_THEN)
				break;
			cmd->c_parent = if_cmd;
			test_cmd->c_next = cmd;
			test_cmd = cmd;
			while (test_cmd->c_next != NULL) {
				test_cmd = test_cmd->c_next;
				test_cmd->c_parent = if_cmd;
			}
		}
		if_cmd->c_if_true = cmd; // CMD_THEN
		if_cmd->c_cmds = if_cmd->c_next;
		if_cmd->c_next = NULL;
		// Jump over then's command chain (if it has one)
		while (cmd->c_next != NULL) {
			cmd = cmd->c_next;
			cmd->c_parent = if_cmd;
		}

		// Read commands until "fi"
		Command *body_cmd = cmd;
		for (;;) {
			cmd = commandInit();
			cmd->c_len = body_cmd->c_len;
			cmd->c_size = body_cmd->c_size;
			cmd->c_buf = body_cmd->c_buf;

			const int parse_result = commandParse(cmd, istream, ostream, aliases, PROMPT);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				if_cmd->c_len = cmd->c_len;
				commandFree(cmd);
				free(cmd);
				return 1;
			}
			if (cmd->c_buf != if_cmd->c_buf) {
				if_cmd->c_size = cmd->c_size;
				if_cmd->c_buf = cmd->c_buf;
			}
			if (cmd->c_type == CMD_ELSE) {
				if (if_cmd->c_if_false != NULL) {
					//original->c_len = error_length;// + cmd->c_len;
					if_cmd->c_buf[0] = 'e'; // TODO parse error near `c' should actually print string, not just single char...
					return 1;
				}
				if_cmd->c_if_false = body_cmd = cmd;
				while (body_cmd->c_next != NULL)
					body_cmd = body_cmd->c_next;
				continue;
			}
			else if (cmd->c_type == CMD_FI)
				break;
			cmd->c_parent = if_cmd;
			body_cmd->c_next = cmd;
			body_cmd = cmd;
			while (body_cmd->c_next != NULL)
				body_cmd = body_cmd->c_next;
		}
		if_cmd->c_next = cmd; // CMD_FI
		if_cmd->c_io = cmd->c_io;
		cmd->c_io = (CmdIO){};
	}
	return 0;
}

void freeCmdIO(CmdIO *io) {
	if (io->in_arg != NULL) {
		for (size_t i = 0; i < io->in_count; ++i)
			freeArg(io->in_arg[i]);
		free(io->in_arg);
		io->in_arg = NULL;
	}
	io->in_count = 0;
	if (io->out_arg != NULL) {
		for (size_t i = 0; i < io->out_count; ++i)
			freeArg(io->out_arg[i]);
		free(io->out_arg);
		io->out_arg = NULL;
	}
	io->out_count = 0;
}

ssize_t lengthRegular(char*);
ssize_t lengthSingleQuote(char*);
ssize_t lengthDoubleQuote(char*);
ssize_t lengthRegInDouble(char *);
ssize_t lengthDollarExp(char*);
int commandTokenize(Command*, FILE*restrict, FILE*restrict, AliasMap*, char*);

Command *commandInit() {
	Command *new_command = malloc(sizeof (Command));
	*new_command = (Command){
		.c_len = 0,
		.c_size = 0,
		.c_buf = NULL,
		.c_type = CMD_EMPTY,
		.c_argc = 0,
		.c_argv = NULL,
		.c_next = NULL,
		.c_if_true = NULL,
		.c_if_false = NULL,
		.c_parent = NULL,
		.c_io = (CmdIO){
			.in_count = 0,
			.out_count = 0,
			.in_arg = NULL,
			.out_arg = NULL,
			.in_file = NULL,
			.out_file = NULL
		}
	};
	return new_command;
}

int commandRead(Command *cmd, FILE *restrict istream, FILE *restrict ostream, char *PROMPT) {
	if (istream == stdin) {
		if (cmd->c_buf != NULL)
			free(cmd->c_buf);
		cmd->c_buf = readline(PROMPT);
		if (cmd->c_buf == NULL)
			return -1;
		cmd->c_len = strlen(cmd->c_buf);
		cmd->c_size = 0;

		if (ostream != NULL) {
			fputs(cmd->c_buf, ostream);
			fputc('\n', ostream);
			fflush(ostream);
		}
	}
	else {
		if (cmd->c_size == 0 && cmd->c_buf != NULL)
			free(cmd->c_buf);
		cmd->c_len = getline(&cmd->c_buf, &cmd->c_size, istream);
		if (cmd->c_len == -1)
			return -1;

		if (ostream != NULL) {
			fputs(cmd->c_buf, ostream);
			fflush(ostream);
		}
	}

	if (cmd->c_buf[cmd->c_len - 1] == '\n') // If final character is a new line, replace it with a null terminator
		cmd->c_buf[cmd->c_len-- - 1] = '\0';

	return 0;
}

int commandParse(Command *cmd, FILE *restrict istream, FILE *restrict ostream, AliasMap *aliases, char *PROMPT) {
	Command *original = cmd;

	// Read line if buffer isn't empty
	if (cmd->c_buf == NULL || cmd->c_buf[0] == '\0')
		if (istream == NULL || commandRead(cmd, istream, ostream, PROMPT) == -1)
			return -1;
	if (cmd->c_buf[0] == '\0' || cmd->c_buf[0] == '#') { // Blank input, or a comment, just ignore and print another prompt.
		cmd->c_buf[0] = '\0';
		cmd->c_type = CMD_EMPTY;
		return 0;
	}

	size_t error_length = 0;
	// Parse Input (into tokens)
	if (commandTokenize(cmd, istream, ostream, aliases, PROMPT)) { // Determine tokens and save them into cmd->c_argv
		// Error parsing command.
		original->c_len = error_length + cmd->c_len;
		cmd->c_buf[0] = cmd->c_buf[cmd->c_len];
		return 1;
	}
	error_length += cmd->c_len + 1;

	int parse_result = parseMultiline(cmd, istream, ostream, aliases, PROMPT);
	if (parse_result == -1)
		return -1;
	if (parse_result) {
		original->c_len = error_length + cmd->c_len;
		return 1;
	}

	// Attempt to parse aliases
	if (aliases != NULL)
		aliasResolve(aliases, cmd);

	return 0;
}

ssize_t lengthRegular(char *buf) {
	ssize_t l = 0;
	for (char c; c = buf[l], c != '\0'; ++l) {
		switch (c) {
			case '=':
				++l;
			case '\'':
			case '"':
			case '$':
			case ' ':
			case '\t':
			case '\n':
			case ';':
			case '<':
			case '>':
			case '|':
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

ssize_t lengthVariable(char *buf) {
	char c = buf[0];
	switch (c) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': {
			size_t l = 0;
			while (c = buf[++l], c >= '0' && c <= '9');
			return l;
		}
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
		case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
		case '_':
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
		case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
			break;
		case '\0':
		default:
			return 0;
	}
	size_t l = 0;
	while (c = buf[++l], c != '\0')
		if (strchr("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz", c) == NULL)
			break;
	return l;
}

ssize_t lengthMath(char *buf) {
	ssize_t l = 0;
	int operator = 0;
	char c;
	while (c = buf[l], c != ')') {
		if (c == '\0')
			return 0;
		if (c == ' ') {
			++l;
			continue;
		}
		if (operator) {
			switch (c) {
				case '+':
				case '-':
				case '*':
				case '/':
				case '%':
					operator = 0;
					break;
				default:
					return 0;
			}
			++l;
		}
		else {
			ssize_t temp;
			if (c == '(') {
				++l;
				temp = lengthMath(&buf[l]);
				++l;
			}
			else
				temp = lengthVariable(&buf[l]);
			if (temp == 0)
				return 0;
			l += temp;
			operator = 1;
		}
	}
	return operator ? l : 0; // If expecting an operand, the expression is incomplete
}

ssize_t lengthDollarExp(char *buf) {
	if (buf[0] != '$')
		return 0;
	ssize_t l = 1;
	for (char c; c = buf[l], c != '\0'; ++l) {
		switch (c) {
			case '$':
			case '?':
			case '#':
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
						case '(':
							// $(()) math
							if (l == 2) {
								++l;
								temp = lengthMath(&buf[l]);
								if (buf[l + temp] != ')')
									return 0;
								++l;
								/*temp = 1;
								while (c = buf[l + temp], c != ')') { // replace with strchr or whatever?
									if (c == '\0')
										return 0;
									++temp;
								}
								if (buf[l + ++temp] != ')')
									return 0;*/
							}
							break;
					}
					if (temp == 0)
						return 0;
					l += temp;
				}
				return l + 1;
			default:
				return lengthVariable(&buf[l]) + 1;
				/*if (c >= '0' && c <= '9') {
					if (l == 1) {
						while (c = buf[++l], c >= '0' && c <= '9');
						return l;
					}
				}
				else {
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
						continue;
					return l;
				}*/
		}
	}
	return l;
}

CmdArg *parseMath(char *buf, size_t *length) {
	// go until ) found

	// find number of operands/operators in top expression, by finding (operators * 2 + 1)
	size_t operators = 0;
	for (size_t i = 0; buf[i] != ')'; ++i) {
		switch (buf[i]) {
			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
				++operators;
				break;
			case '(':
				++i;
				ssize_t temp = lengthMath(&buf[i]);
				// Assume greater than 0, since validation should have already occured.
#ifdef DEBUG
				fprintf(stderr, "lengthMath returned <= 0 from parseMath...\n");
#endif
				i += temp;
				break;
		}
	}
#ifdef DEBUG
	fprintf(stderr, "Math expression has %lu operators\n", operators);
#endif

	CmdArg *args = calloc(operators * 2 + 2, sizeof (CmdArg));
	args[operators * 2 + 1].type = ARG_NULL;
	// once again, we are assuming everything has already been validated, if validation failed this WILL make mistakes
	*length = 0;
	while (buf[*length] == ' ') // skip whitespace
		++*length;
	size_t temp = lengthVariable(&buf[*length]);
	args[0] = (CmdArg){
		.type = isdigit(buf[*length]) ? ARG_MATH_OPERAND_NUMERIC : ARG_MATH_OPERAND_VARIABLE,
		.str = strndup(&buf[*length], temp)
	};
	*length += temp;
	for (size_t pair = 0; pair < operators; ++pair) {
		// construct operator
		while (buf[*length] == ' ') // skip whitespace
			++*length;
		size_t operator_index = pair * 2 + 1; // 1, 3, 5, 7, ...
		args[operator_index] = (CmdArg) { .type = ARG_MATH_OPERATOR, .str = malloc(sizeof (char)) };
		args[operator_index].str[0] = buf[(*length)++];

		// construct operand
		while (buf[*length] == ' ') // skip whitespace
			++*length;
		size_t operand_index = operator_index + 1; // 2, 4, 6, 8, ...
		if (buf[*length] == '(') {
			args[operand_index] = (CmdArg){ .type = ARG_MATH, .sub = parseMath(&buf[++*length], &temp) };
			*length += temp + 1;
		}
		else {
			temp = lengthVariable(&buf[*length]);
			args[operand_index] = (CmdArg){
				.type = isdigit(buf[*length]) ? ARG_MATH_OPERAND_NUMERIC : ARG_MATH_OPERAND_VARIABLE,
				.str = strndup(&buf[*length], temp)
			};
			*length += temp;
		}
	}
	while (buf[*length] == ' ') // skip whitespace
		++*length;

	return args;
}

int commandTokenize(Command *cmd, FILE *restrict istream, FILE *restrict ostream, AliasMap *aliases, char *PROMPT) {
	char *buf = cmd->c_buf;
	/*
	 * end: current parse index - when finished it will point one char past the end of the command
	 * argc: how many args this command has
	 * input_count: number of input files (<)
	 * output_count: number of output files (>)
	 * done: indicates we've finished parsing this command (there may be more after it)
	 * whitespace: whether or not the last character was whitespace
	 * need_file: whether we are waiting for a filename argument (for < or >)
	 * has_pipe: command ends with a pipe, so we need to create the next command and parse it
	 */
	size_t end = 0, argc = 0, input_count = 0, output_count = 0;
	_Bool done = 0, whitespace = 1, need_file = 0, has_pipe = 0;
	while (end <= cmd->c_len && !done) {
		_Bool parse_run = 1;
		switch (buf[end]) {
			case '|':
				has_pipe = 1;
			case ';': // End of this command
				if (end == 0) {
					cmd->c_len = end;
					return -1;
				}
				++end;
			case '\0':
				if (need_file) {
					cmd->c_len = --end;
					return -1;
				}
				if (end == 0)
					return 0;
				done = 1;
				--end;
			case ' ':
			case '\t':
			case '\n':
				if (!whitespace) {
					if (!need_file)
						++argc;
					whitespace = 1;
				}
				parse_run = 0;
				break;
			case '<':
				++input_count;
				--output_count;
			case '>':
				++output_count;

				if (need_file) {
					cmd->c_len = end;
					return -1;
				}

				if (whitespace)
					whitespace = 0;
				else
					++argc; // no whitespace between previous argument and < ?

				switch (buf[end + 1]) {
					case ';':
					case '<':
					case '>':
						++end;
					case '\n':
					case '\0':
						cmd->c_len = end;
						return -1;
					case ' ':
					case '\t':
						need_file = 1;
						break;
					default:
						--argc;
				}
				parse_run = 0;
				break;
			default:
				if (need_file) {
					need_file = 0;
					--argc;
				}
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
					if (argc == 0 && !need_file && buf[end + len - 1] == '=')
						++argc;
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
	fprintf(stderr, "Argc: %zu\n", argc);
#endif
	cmd->c_argc = argc;
	if (cmd->c_argc == 0) {
		memmove(buf, &buf[end], cmd->c_len - end + 1);
		return 0;
	}
	argc = 0;
	cmd->c_io = (CmdIO){ .in_count = input_count, .out_count = output_count };
	input_count = 0;
	output_count = 0;

	// Allocate space for arguments
	cmd->c_argv = calloc(cmd->c_argc, sizeof (CmdArg));
	for (size_t i = 0; i < cmd->c_argc; ++i)
		cmd->c_argv[i].type = ARG_NULL;
	if (cmd->c_io.in_count) {
		cmd->c_io.in_arg = calloc(cmd->c_io.in_count, sizeof (CmdArg));
		for (size_t i = 0; i < cmd->c_io.in_count; ++i)
			cmd->c_io.in_arg[i].type = ARG_NULL;
	}
	if (cmd->c_io.out_count) {
		cmd->c_io.out_arg = calloc(cmd->c_io.out_count, sizeof (CmdArg));
		for (size_t i = 0; i < cmd->c_io.out_count; ++i)
			cmd->c_io.out_arg[i].type = ARG_NULL;
	}

	// Parse and set each argument structure
	need_file = 0;
	_Bool inDoubleQuote = 0, need_input;
	for (size_t current = 0; current < end; ++current) {
		_Bool parse_regular = 0;
		CmdArg *cur_arg = need_file
			? (need_input ? &cmd->c_io.in_arg[input_count] : &cmd->c_io.out_arg[output_count])
			: &cmd->c_argv[argc], new_arg = { .type = ARG_NULL };
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

				new_arg = (CmdArg){ .type = ARG_QUOTED_STRING, .str = strndup(&buf[current + 1], quote_len - 2) };
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
					if (buf[current + 1] == '(') {
						if (buf[current + 2] == '(') {
							size_t dummy;
							new_arg = (CmdArg){ .type = ARG_MATH, .sub = parseMath(&buf[current + 3], &dummy) };
						}
						else
							new_arg = (CmdArg){ .type = inDoubleQuote ? ARG_QUOTED_SUBSHELL : ARG_SUBSHELL, .str = strndup(&buf[current + 2], dollar_len - 3) };
					}
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
			case '<':
				if (!inDoubleQuote) {
					if ((argc < cmd->c_argc || need_file) && cur_arg->type != ARG_NULL) {
						if (need_file)
							need_input ? ++input_count : ++output_count;
						else
							++argc;
					}
					need_file = 1;
					need_input = 1;
					continue;
				}
				parse_regular = 1;
				break;
			case '>':
				if (!inDoubleQuote) {
					if ((argc < cmd->c_argc || need_file) && cur_arg->type != ARG_NULL) {
						if (need_file)
							need_input ? ++input_count : ++output_count;
						else
							++argc;
					}
					need_file = 1;
					need_input = 0;
					continue;
				}
				parse_regular = 1;
				break;
			case '|':
			case ';': // End of this command
				if (inDoubleQuote)
					parse_regular = 1;
			case '\0':
				break;
			case ' ':
			case '\t':
			case '\n':
				if (!inDoubleQuote) {
					if (cur_arg->type != ARG_NULL) {
						if (need_file) {
							need_input ? ++input_count : ++output_count;
							need_file = 0;
						}
						else
							++argc;
					}
					break;
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
			if (argc == 0 && !need_file && !inDoubleQuote && buf[current + reg_len - 1] == '=')
				++argc;

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
	cmd->c_len -= end;
	cmd->c_type = CMD_REGULAR;

	// Read next command if applicable (pipe, &&, ||)
	if (has_pipe) {
		Command *next = cmd->c_next = commandInit();
		next->c_len = cmd->c_len;
		next->c_size = cmd->c_size;
		next->c_buf = cmd->c_buf;
		const int parse_result = commandParse(next, istream, ostream, aliases, PROMPT);
		if (next->c_buf != cmd->c_buf) {
			cmd->c_size = next->c_size;
			cmd->c_buf = next->c_buf;
		}
		switch (parse_result) {
			case 0:
				break;
			default:
				cmd->c_len = next->c_len;
			case -1:
				return parse_result;
		}
		switch (next->c_type) {
			case CMD_DO: case CMD_DONE:
			case CMD_THEN: case CMD_ELSE: case CMD_FI:
				// TODO: set error length
				return 1;
			default:
				break;
		}
		cmd->c_io.out_pipe = 1;
		next->c_io.in_pipe = 1;
	}

	return 0;
}

CmdArg argdup(CmdArg a) {
	switch (a.type) {
		case ARG_BASIC_STRING:
		case ARG_QUOTED_STRING:
		case ARG_VARIABLE:
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL:
		case ARG_MATH_OPERAND_NUMERIC:
		case ARG_MATH_OPERAND_VARIABLE:
			return (CmdArg){ .type = a.type, .str = strdup(a.str) };
		case ARG_COMPLEX_STRING:
		case ARG_MATH: {
			size_t sub_len = 0;
			while (a.sub[sub_len++].type != ARG_NULL);
			CmdArg new_arg = { .type = a.type, .sub = calloc(sub_len, sizeof (CmdArg)) };
			for (size_t i = 0; i < sub_len; ++i)
				new_arg.sub[i] = argdup(a.sub[i]);
			return new_arg;
		}
		case ARG_MATH_OPERATOR: {
			char *c = malloc(sizeof (char));
			*c = *a.str;
			return (CmdArg){ .type = ARG_MATH_OPERATOR, .str = c };
		}
		case ARG_NULL:
			return (CmdArg){ .type = ARG_NULL };
	}
	return (CmdArg){};
}

void commandFree(Command *cmd) {
	if (cmd->c_argv != NULL) {
		for (size_t i = 0; i < cmd->c_argc; ++i)
			freeArg(cmd->c_argv[i]);
		free(cmd->c_argv);
		cmd->c_argv = NULL;
		cmd->c_argc = 0;
	}

	if (cmd->c_next != NULL) {
		commandFree(cmd->c_next);
		free(cmd->c_next);
		cmd->c_next = NULL;
	}
	if (cmd->c_if_true != NULL) {
		commandFree(cmd->c_if_true);
		free(cmd->c_if_true);
		cmd->c_if_true = NULL;
	}
	if (cmd->c_if_false != NULL) {
		commandFree(cmd->c_if_false);
		free(cmd->c_if_false);
		cmd->c_if_false = NULL;
	}
	if (cmd->c_cmds != NULL) {
		commandFree(cmd->c_cmds);
		free(cmd->c_cmds);
		cmd->c_cmds = NULL;
	}

	cmd->c_parent = NULL;

	freeCmdIO(&cmd->c_io);
}

void freeArg(CmdArg a) {
	switch (a.type) {
		case ARG_BASIC_STRING:
		case ARG_QUOTED_STRING:
		case ARG_VARIABLE:
		case ARG_SUBSHELL:
		case ARG_QUOTED_SUBSHELL:
		case ARG_MATH_OPERATOR:
		case ARG_MATH_OPERAND_NUMERIC:
		case ARG_MATH_OPERAND_VARIABLE:
			free(a.str);
			break;
		case ARG_COMPLEX_STRING:
		case ARG_MATH:
			for (size_t i = 0; a.sub[i].type != ARG_NULL; ++i)
				freeArg(a.sub[i]);
			free(a.sub);
			break;
		case ARG_NULL:
		default:
			break;
	}
}
