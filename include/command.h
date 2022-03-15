#ifndef COMMAND_H
#define COMMAND_H

#include "command_structures.h"

/*
 * Arguments
 */

CmdArg argdup(CmdArg);
void freeArg(CmdArg);

/*
 * Commands
 */

Command *commandInit();
int commandParse(Command*, FILE*restrict, FILE*restrict, AliasMap*);
void commandFree(Command*);

/*
 * Aliases
 */

AliasMap *aliasInit();
void aliasFree(AliasMap*);
void aliasResolve(AliasMap*, Command*);
Alias *aliasAdd(AliasMap*, char*, char*);
int aliasRemove(AliasMap*, char*);
int aliasPrint(AliasMap*, char*, FILE *restrict);
void aliasList(AliasMap*, FILE *restrict);

#endif
