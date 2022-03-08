#define _POSIX_C_SOURCE 200809L // getline, strndup, strdup
#include "command.h"
#include "compatibility.h"
#include <string.h>

size_t shiftArg(Command *cmd) {
	freeArg(cmd->c_argv[0]);
	for (size_t i = 1; i < cmd->c_argc; ++i)
		cmd->c_argv[i - 1] = cmd->c_argv[i];
	return --cmd->c_argc;
}

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
/*void dupSpecialCommand(Command *cmd) {
	Command *new_loc = commandInit();
	*new_loc = *cmd;

	*cmd = (Command){}; // Zero out everything
	cmd->c_len = new_loc->c_len;
	cmd->c_size = new_loc->c_size;
	cmd->c_buf = new_loc->c_buf;
	cmd->c_next = new_loc;
}*/

int parseMultiline(Command *cmd, FILE *restrict istream, FILE *restrict ostream) {
	if (cmd->c_argc < 1 || cmd->c_argv[0].type != ARG_BASIC_STRING)
		return 0;

	if (!strcmp(cmd->c_argv[0].str, "do")) {
		if (cmd->c_argc == 1)
			cmd->c_type = CMD_DO;
		else {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
			cmd->c_type = CMD_DO;

			int ret = parseMultiline(cmd->c_next, istream, ostream);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_THEN:
					case CMD_ELSE:
					case CMD_FI:
						ret = 1;
						break;
					default:
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

			int ret = parseMultiline(cmd->c_next, istream, ostream);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_DONE:
					case CMD_THEN:
						ret = 1;
						break;
					default:
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

			int ret = parseMultiline(cmd->c_next, istream, ostream);
			if (ret == 0) {
				switch (cmd->c_next->c_type) {
					case CMD_DO:
					case CMD_DONE:
					case CMD_THEN:
					case CMD_ELSE:
						ret = 1;
						break;
					default:
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
			int res = parseMultiline(test_cmd, istream, ostream);
			if (res != 0)
				return res;
			test_cmd->c_parent = while_cmd;
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

			const int parse_result = commandParse(cmd, istream, ostream);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				commandFree(cmd);
				free(cmd);
				while_cmd->c_len = cmd->c_len;
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

			const int parse_result = commandParse(cmd, istream, ostream);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				commandFree(cmd);
				free(cmd);
				while_cmd->c_len = cmd->c_len;
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
			int res = parseMultiline(test_cmd, istream, ostream);
			if (res != 0)
				return res;
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

			const int parse_result = commandParse(cmd, istream, ostream);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				commandFree(cmd);
				free(cmd);
				if_cmd->c_len = cmd->c_len;
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

		// Parse anything that came after "then"
		/*if (cmd->c_argc > 1) {
			shiftArg(cmd);
			dupSpecialCommand(cmd);
			int res = parseMultiline(cmd->c_next, istream, ostream);
			if (res != 0)
				return res;
			do {
				cmd = cmd->c_next;
				cmd->c_parent = if_cmd;
			}
			while (cmd->c_next != NULL);
		}*/

		// Read commands until "fi"
		Command *body_cmd = cmd;
		for (;;) {
			cmd = commandInit();
			cmd->c_len = body_cmd->c_len;
			cmd->c_size = body_cmd->c_size;
			cmd->c_buf = body_cmd->c_buf;

			const int parse_result = commandParse(cmd, istream, ostream);
			if (parse_result == -1) {
				commandFree(cmd);
				free(cmd);
				return -1;
			}
			if (parse_result) {
				commandFree(cmd);
				free(cmd);
				if_cmd->c_len = cmd->c_len;
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
	if (io->in_file != NULL) {
		fclose(io->in_file);
		io->in_file = NULL;
	}
	io->in_count = 0;
	if (io->out_arg != NULL) {
		for (size_t i = 0; i < io->out_count; ++i)
			freeArg(io->out_arg[i]);
		free(io->out_arg);
		io->out_arg = NULL;
	}
	if (io->out_file != NULL) {
		for (size_t i = 0; i < io->out_count; ++i)
			if (i != io->out_count - 1 || !io->out_pipe)
				fclose(io->out_file[i]);
		free(io->out_file);
		io->out_file = NULL;
	}
	io->out_count = 0;
}

ssize_t lengthRegular(char*);
ssize_t lengthSingleQuote(char*);
ssize_t lengthDoubleQuote(char*);
ssize_t lengthRegInDouble(char *);
ssize_t lengthDollarExp(char*);
int commandTokenize(Command*, FILE*restrict, FILE*restrict);

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
	// Parse Input (into tokens)
	if (commandTokenize(cmd, istream, ostream)) { // Determine tokens and save them into cmd->c_argv
		// Error parsing command.
		original->c_len = error_length + cmd->c_len;
		cmd->c_buf[0] = cmd->c_buf[cmd->c_len];
		return 1;
	}
	error_length += cmd->c_len + 1;

	int parse_result = parseMultiline(cmd, istream, ostream);
	if (parse_result == -1)
		return -1;
	if (parse_result) {
		original->c_len = error_length + cmd->c_len;
		return 1;
	}

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

int commandTokenize(Command *cmd, FILE *restrict istream, FILE *restrict ostream) {
	char *buf = cmd->c_buf;
	/*
	 * end: current parse index - when finished it will point one char past the end of the command
	 * argc: how many args this command has
	 * input_count: number of input files (<)
	 * output_count: number of output files (>)
	 * done: indicates we've finished parsing this command (there may be more after it)
	 * whitespace: whether or not the last character was whitespace
	 * need_file: whether we are waiting for a filename argument (for < or >)
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
			case ' ': // can't use delim_char...
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
	fprintf(stderr, "Argc: %lu\n", argc);
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
			case ' ': // can't use delim_char...
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
		const int parse_result = commandParse(next, istream, ostream);
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
		/*next->c_io.in_arg = reallocarray(next->c_io.in_arg, ++next->c_io.in_count, sizeof (CmdArg));
		for (size_t i = next->c_io.in_count - 1; i > 0; --i)
			next->c_io.in_arg[i] = next->c_io.in_arg[i - 1];
		next->c_io.in_arg[0] = (CmdArg){ .type = ARG_PIPE };*/
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
			return (CmdArg){ .type = a.type, .str = strdup(a.str) };
		case ARG_COMPLEX_STRING: {
			size_t sub_len = 0;
			while (a.sub[sub_len++].type != ARG_NULL);
			CmdArg new_arg = { .type = ARG_COMPLEX_STRING, .sub = calloc(sub_len, sizeof (CmdArg)) };
			for (size_t i = 0; i < sub_len; ++i)
				new_arg.sub[i] = argdup(a.sub[i]);
			return new_arg;
		}
		case ARG_NULL:
			return (CmdArg){ .type = ARG_NULL };
	}
	return (CmdArg){};
}

void commandFree(Command *cmd) {
	// Stop recursive calls to commandFree from causing a double free
	cmd->c_type = CMD_FREED;

	if (cmd->c_argv != NULL) {
		for (size_t i = 0; i < cmd->c_argc; ++i)
			freeArg(cmd->c_argv[i]);
		free(cmd->c_argv);
		cmd->c_argv = NULL;
		cmd->c_argc = 0;
	}

	if (cmd->c_next != NULL && (cmd->c_parent == NULL || cmd->c_parent->c_next != cmd->c_next) && cmd->c_next->c_type != CMD_FREED) {
		commandFree(cmd->c_next);
		free(cmd->c_next);
	}
	if (cmd->c_if_true != NULL) {
		if (cmd->c_if_true->c_type != CMD_FREED) {
			commandFree(cmd->c_if_true);
			free(cmd->c_if_true);
		}
		cmd->c_if_true = NULL;
	}
	if (cmd->c_if_false != NULL) {
		if (cmd->c_if_false->c_type != CMD_FREED) {
			commandFree(cmd->c_if_false);
			free(cmd->c_if_false);
		}
		cmd->c_if_false = NULL;
	}
	if (cmd->c_cmds != NULL) {
		if (cmd->c_cmds->c_type != CMD_FREED) {
			commandFree(cmd->c_cmds);
			free(cmd->c_cmds);
		}
		cmd->c_cmds = NULL;
	}

	cmd->c_next = cmd->c_parent = NULL;

	freeCmdIO(&cmd->c_io);
}

void freeArg(CmdArg a) {
	switch (a.type) {
		case ARG_BASIC_STRING:
		case ARG_QUOTED_STRING:
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
