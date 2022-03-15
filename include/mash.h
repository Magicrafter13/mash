#ifndef MASH_H
#define MASH_H

#include "command.h"
#include "hashTable.h"
#include "suftree.h"
#include <pwd.h>
#include <stdint.h>
#include <sys/types.h>

#define _VMAJOR 1
#define _VMINOR 0
#define TMP_RW_BUFSIZE 4096

typedef struct _cmd_source Source;
struct _cmd_source {
	FILE *input;
	FILE *output;
	size_t argc;
	char **argv;
	Source *prev, *next;
};

typedef struct _shell_var Variables;
struct _shell_var {
	unsigned long long buckets;
	hashTable *map;
};

extern void *_DUMMY_PTR[];

/*
 * Entry Point!
 */
int main(int, char*[]);

/*
 * Command execution
 */

int commandExecute(Command*, AliasMap*, Source**, Variables*, FILE**, uint8_t*, SufTree*);
int expandArgument(char**, CmdArg, Source*, Variables*, uint8_t*);

/*
 * Mash file utilities
 */

FILE *open_config(struct passwd*, char*);
FILE *open_history(struct passwd*, char*, Variables*);
int mktmpfile(_Bool, char**, Variables*);
int openInputFiles(CmdIO*, Source*, Variables*, uint8_t*);
int openOutputFiles(CmdIO*, Source*, Variables*, uint8_t*);
int openIOFiles(CmdIO*, Source*, Variables*, uint8_t*);
void closeIOFiles(CmdIO*);
FILE *getParentInputFile(Command*);
FILE *getParentOutputFile(Command*);

/*
 * Input data
 */

Source *sourceInit();
void sourceSet(Source*, FILE*restrict, size_t, char**);
Source *sourceAdd(Source*, FILE*restrict, size_t, char**);
Source *sourceClose(Source*);
void sourceFree(Source*);
int sourceShift(Source*, int);

/*
 * Built-ins
 */

uint8_t export(size_t, void**);
uint8_t help(size_t, void**);
uint8_t cd(size_t, void**);

#define BUILTIN_COUNT 2

extern char *const BUILTIN[BUILTIN_COUNT];

extern uint8_t (*BUILTIN_FUNCTION[BUILTIN_COUNT])(size_t, void**);

/*
 * Environment/Shell Variables
 */

Variables *variableInit();
void variableFree(Variables*);
void variableSet(Variables*, char*, char*);
char *variableGet(Variables*, char*);
void variableUnset(Variables*, char*);
int setvar(Variables*, char*, char*, _Bool);
char *getvar(Variables*, char*);
int unsetvar(Variables*, char*);
size_t varNameLength(char*);

/*
 * Prompt utilities
 */

void printPrompt(Variables*, Source*, struct passwd*, uid_t);

#endif
