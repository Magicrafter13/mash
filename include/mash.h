#include "command.h"
#include "hashTable.h"
#include <pwd.h>
#include <stdint.h>

#define _VMAJOR 1
#define _VMINOR 0
#define TMP_RW_BUFSIZE 4096

char *expandArgument(CmdArg, int, char**);

/*
 * Mash file utilities
 */

FILE *open_config(struct passwd*);
FILE *open_history(struct passwd*);
FILE *openInputFiles(CmdIO, int, char**);
FILE **openOutputFiles(CmdIO, int, char**);
void closeIOFiles(CmdIO*);
FILE *getParentInputFile(Command*);
FILE *getParentOutputFile(Command*);

/*
 * Input data
 */

typedef struct _cmd_source Source;
struct _cmd_source {
	FILE *input;
	FILE *output;
	size_t argc;
	char **argv;
	Source *prev, *next;
};

Source *sourceInit();
void sourceSet(Source*, FILE*restrict, size_t, char**);
Source *sourceAdd(Source*, FILE*restrict, size_t, char**);
Source *sourceClose(Source*);
void sourceFree(Source*);

/*
 * Built-ins
 */

uint8_t export(size_t, void**);
uint8_t help(size_t, void**);
uint8_t cd(size_t, void**);

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
