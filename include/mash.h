#include "command.h"
#include "hashTable.h"
#include <pwd.h>
#include <stdint.h>

#define _VMAJOR 1
#define _VMINOR 0
#define TMP_RW_BUFSIZE 4096
/*
 * Mash file utilities
 */

FILE *open_config(struct passwd*);
FILE *open_history(struct passwd*);

/*
 * Built-ins
 */

uint8_t export(size_t, void**);
uint8_t help(size_t, void**);
uint8_t cd(size_t, void**);

/*
 * Aliases
 */

struct _alias_map {
	unsigned long long buckets;
	hashTable *map;
};
typedef struct _alias_map AliasMap;

struct _alias {
	char *str;
	int argc;
	struct _arg *args;
};
typedef struct _alias Alias;

AliasMap *aliasInit();
void aliasFree(AliasMap*);
void aliasResolve(AliasMap*, Command*);
Alias *aliasAdd(AliasMap*, char*, char*);
int aliasRemove(AliasMap*, char*);
int aliasPrint(AliasMap*, char*, FILE *restrict);
void aliasList(AliasMap*, FILE *restrict);
