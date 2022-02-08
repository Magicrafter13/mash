#ifndef ALIAS_H
#define ALIAS_H

#include "compatibility.h"
#include "hashTable.h"
#include "command.h"

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

int aliasPrint(AliasMap*, char*, FILE *restrict);

void aliasList(AliasMap*, FILE *restrict);

#endif
