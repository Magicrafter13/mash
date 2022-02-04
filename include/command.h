#ifndef COMMAND_H
#define COMMAND_H

#include <stdio.h>
#include "suftree.h"

enum _arg_type {
	ARG_NULL,
	ARG_BASIC_STRING,
	ARG_VARIABLE,
	ARG_SUBSHELL,
	ARG_COMPLEX_STRING
};

struct _arg {
	enum _arg_type type;
	union {
		char * str;
		struct _arg * sub;
	};
};

enum _cmd_type {
	CMD_FREED,   // Type to ignore in commandFree - useful for loops where child commands will point back to the loop they are in
	CMD_EMPTY,   // Command to ignore (comments, blank lines, etc)
	CMD_REGULAR,
	CMD_WHILE,
	CMD_IF
};

struct _command {
	size_t c_len, c_size;
	char * c_buf;
	enum _cmd_type c_type;
	int c_argc;
	struct _arg * c_argv;
	struct _command * c_next;
	struct _command * c_if_true;
	struct _command * c_if_false;
};

typedef struct _command Command;

void commandSetBuiltins(SufTree*);

void commandSetVarFunc(char *(*)(const char*));

Command *commandInit();

int commandRead(Command*, FILE*);

int commandParse(Command*, FILE*);

void commandFree(Command*);

void freeArg(struct _arg);

#endif
