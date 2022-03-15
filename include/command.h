#ifndef COMMAND_H
#define COMMAND_H

#include "command_structures.h"
#include "hashTable.h"

/*
 * Commands
 */

Command *commandInit();
int commandParse(Command*, FILE*restrict, FILE*restrict);
CmdArg argdup(CmdArg);
void commandFree(Command*);
void freeArg(CmdArg);

/*
 * Aliases
 */

typedef struct _alias_map AliasMap;
struct _alias_map {
	unsigned long long buckets;
	hashTable *map;
};

typedef struct _alias Alias;
struct _alias {
	char *str;
	int argc;
	CmdArg *args;
};

AliasMap *aliasInit();
void aliasFree(AliasMap*);
void aliasResolve(AliasMap*, Command*);
Alias *aliasAdd(AliasMap*, char*, char*);
int aliasRemove(AliasMap*, char*);
int aliasPrint(AliasMap*, char*, FILE *restrict);
void aliasList(AliasMap*, FILE *restrict);

#endif
