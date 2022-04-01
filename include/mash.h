#ifndef MASH_H
#define MASH_H

#include "command_structures.h"
#include "hashTable.h"
#include <pwd.h>
#include <stdint.h>
#include <sys/types.h>

#define _VMAJOR 1
#define _VMINOR 0
#define TMP_RW_BUFSIZE 4096

typedef enum _cmd_signal CmdSignal;
enum _cmd_signal {
	CSIG_DONE,
	CSIG_EXIT,
	CSIG_EXEC,
	CSIG_CONTINUE,
	CSIG_BREAK
};

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

/*
 * Entry Point!
 */
int main(int, char*[]);

/*
 * Command execution
 */

CmdSignal commandExecute(Command*, AliasMap*, Source**, Variables*, FILE**, uint8_t*);
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

void b_alias(uint8_t*, char**, Source*, AliasMap*);
void b_cd(uint8_t*, char**, int);
void b_dot(uint8_t*, char**, int, Source**);
CmdSignal b_exit(uint8_t*, char**, int, Source*);
void b_export(uint8_t*, char**, int, Source*, Variables*);
void b_help(uint8_t*);
void b_read(uint8_t*, FILE*, char**, Source*, Variables*);
void b_shift(uint8_t*, char**, int, Source*);
void b_unalias(uint8_t*, char**, int, AliasMap*);
void b_unset(uint8_t*, char**, Source*, Variables*);

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

char *createPrompt(Variables*, Source*, struct passwd*, uid_t);

#endif
