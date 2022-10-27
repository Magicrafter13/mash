#ifndef COMMAND_DEFS_H
#define COMMAND_DEFS_H

#include "hashTable.h"
#include <stdio.h>

/*
 * Enum types
 */

// Arguments
enum _arg_type {
	ARG_NULL,
	ARG_BASIC_STRING,
	ARG_QUOTED_STRING,
	ARG_VARIABLE,
	ARG_SUBSHELL,
	ARG_QUOTED_SUBSHELL,
	ARG_COMPLEX_STRING,
	ARG_MATH,
	ARG_MATH_OPERAND_NUMERIC,
	ARG_MATH_OPERAND_VARIABLE,
	ARG_MATH_OPERATOR
};

// Commands
enum _cmd_type {
	CMD_EMPTY,   // Command to ignore (comments, blank lines, etc)
	CMD_REGULAR,
	CMD_WHILE, CMD_DO, CMD_DONE,
	CMD_IF, CMD_THEN, CMD_ELSE, CMD_FI,
};

/*
 * Data structures
 */

// Arguments
typedef struct _arg CmdArg;
struct _arg {
	enum _arg_type type;
	union {
		char *str;
		CmdArg *sub;
	};
};

// Command IO data
typedef struct _cmd_io CmdIO;
struct _cmd_io {
	size_t in_count, out_count;
	CmdArg *in_arg, *out_arg;
	FILE *in_file, **out_file;
	_Bool in_pipe, out_pipe;
};

// Commands
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

// Alias storage
typedef struct _alias_map AliasMap;
struct _alias_map {
	unsigned long long buckets;
	hashTable *map;
};

// Single alias
typedef struct _alias Alias;
struct _alias {
	char *str;
	int argc;
	CmdArg *args;
};

#endif
