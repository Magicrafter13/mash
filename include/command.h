#ifndef COMMAND_H
#define COMMAND_H

#include "command_structures.h"

Command *commandInit();

int commandParse(Command*, FILE*restrict, FILE*restrict);

CmdArg argdup(CmdArg);

void commandFree(Command*);

void freeArg(CmdArg);

#endif
