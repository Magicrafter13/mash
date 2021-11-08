#ifndef COMMAND_H
#define COMMAND_H

#include <stdio.h>
#include "suftree.h"

enum _command_type {
	CMD_UNKNOWN,
	CMD_REGULAR,
	CMD_EMPTY,
	CMD_BUILTIN
};

struct _command {
	size_t c_len, c_size;
	char * c_buf;
	int c_argc;
	char ** c_argv;
	enum _command_type c_type;
	size_t c_builtin;
};

typedef struct _command Command;

void commandSetBuiltins(SufTree*);

Command commandInit();

int commandRead(Command*, FILE*);

#endif
