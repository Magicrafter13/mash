#ifndef COMMAND_H
#define COMMAND_H

#include <stdio.h>

enum _arg_type {
	ARG_NULL,
	ARG_BASIC_STRING,
	ARG_QUOTED_STRING,
	ARG_VARIABLE,
	ARG_SUBSHELL,
	ARG_QUOTED_SUBSHELL,
	ARG_COMPLEX_STRING
};

typedef struct _arg CmdArg;
struct _arg {
	enum _arg_type type;
	union {
		char *str;
		CmdArg *sub;
	};
};

typedef struct _cmd_io CmdIO;
struct _cmd_io {
	size_t in_count, out_count;
	CmdArg *in_arg, *out_arg;
	FILE *in_file, **out_file;
	_Bool in_pipe, out_pipe;
};

enum _cmd_type {
	CMD_EMPTY,   // Command to ignore (comments, blank lines, etc)
	CMD_REGULAR,
	CMD_WHILE, CMD_DO, CMD_DONE,
	CMD_IF, CMD_THEN, CMD_ELSE, CMD_FI,
};

typedef struct _command Command;
struct _command {
	size_t c_len, c_size;
	char *c_buf;
	enum _cmd_type c_type;
	int c_argc;
	CmdArg *c_argv;
	Command *c_next;
	Command *c_if_true;
	Command *c_if_false;
	Command *c_cmds;
	Command *c_parent;
	CmdIO c_io;
};

Command *commandInit();

int commandParse(Command*, FILE*restrict, FILE*restrict);

CmdArg argdup(CmdArg);

void commandFree(Command*);

void freeArg(CmdArg);

#endif
