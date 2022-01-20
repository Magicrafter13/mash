#ifndef COMMAND_H
#define COMMAND_H

#include <stdio.h>
#include "suftree.h"

enum _arg_type {
	ARG_NULL,
	ARG_BASIC_STRING,
	ARG_VARIABLE,
	//ARG_SUBSHELL, if/when $() is implemented
	ARG_COMPLEX_STRING
};

struct _arg {
	enum _arg_type type;
	union {
		char * str;
		struct _arg * sub;
	};
};

struct _command {
	size_t c_len, c_size;
	char * c_buf;
	int c_argc;
	struct _arg * c_argv;
	size_t c_builtin;
	struct _command * c_next;
	struct _command * c_if_true;
	struct _command * c_if_false;
};

typedef struct _command Command;

void commandSetBuiltins(SufTree*);

void commandSetVarFunc(char *(*)(const char*));

Command *commandInit();

int commandRead(Command*, FILE*);

void freeArg(struct _arg);

#endif
