#define _POSIX_C_SOURCE 200809L // strdup
#include <string.h>
#include "alias.h"

AliasMap *aliasInit() {
	AliasMap *info = malloc(sizeof (AliasMap));
	info->buckets = 16;
	info->map = createTable(info->buckets);
	return info;
}

void aliasFree(AliasMap *info) {
	for (unsigned long long bucket = 0; bucket < info->buckets; ++bucket) {
		for (struct node *node = info->map[bucket].next; node != NULL; node = node->next) {
			Alias *alias = node->entry.data;
			free(alias->str);
			for (size_t i = 0; i < alias->argc; ++i)
				freeArg(alias->args[i]);
			free(alias->args);
			free(alias);
		}
		free_nodes(info->map[bucket].next);
	}
	free(info->map);
	free(info);
}

void aliasResolve(AliasMap *info, Command *cmd) {
	// Cannot check for alias from this command
	if (cmd->c_argv[0].type != ARG_BASIC_STRING)
		return;

	TableEntry *entry = tableSearch(info->map, info->buckets, cmd->c_argv[0].str);
	// Command is not a known alias
	if (entry == NULL)
		return;

	// Update command
	Alias *alias = entry->data;
	int temp_argc = cmd->c_argc;
	cmd->c_argc += alias->argc - 1;
	cmd->c_argv = reallocarray(cmd->c_argv, cmd->c_argc + 1, sizeof (struct _arg));
	cmd->c_argv[cmd->c_argc] = cmd->c_argv[temp_argc]; // Copy ARG_NULL
	// Delete alias arg and shift remaining args
	freeArg(cmd->c_argv[0]);
	for (size_t i = 0; i < temp_argc - 1; ++i)
		cmd->c_argv[cmd->c_argc - i - 1] = cmd->c_argv[temp_argc - i - 1];
	for (size_t i = 0; i < alias->argc; ++i)
		cmd->c_argv[i] = argdup(alias->args[i]);
	// Check if new argv[0] is the same as the alias name. If not, run through again.
	// Shouldn't do this if they are the same, since it's perfectly normal to alias 'ls' to 'ls --color=auto' for example (infinite recursion).
	if (strcmp(cmd->c_argv[0].str, entry->key))
		aliasResolve(info, cmd);
}

Alias *aliasAdd(AliasMap *info, char *name, char *str) {
	TableEntry *entry;
	info->map = tableAdd(info->map, &info->buckets, name, &entry);

	// Alias already existed, so we must free some data
	Alias *alias = entry->data;
	if (alias != NULL) {
		free(alias->str);
		for (size_t i = 0; i < alias->argc; ++i)
			freeArg(alias->args[i]);
		free(alias->args);
	}
	// Otherwise create a new one
	else
		alias = entry->data = malloc(sizeof (Alias));
	// TODO handle NULL (OOM)

	alias->str = strdup(str);

	// Parse string into args (so we don't have to do that every single time the alias is called)
	Command temp = {};
	temp.c_size = (temp.c_len = strlen(str)) + 1;
	temp.c_buf = str;
	commandParse(&temp, NULL, NULL);
	alias->argc = temp.c_argc;
	alias->args = temp.c_argv;

	return alias;
}

int aliasRemove(AliasMap *info, char *name) {
	TableEntry *entry = tableSearch(info->map, info->buckets, name);
	if (entry == NULL)
		return 0;

	Alias *alias = entry->data;
	free(alias->str);
	for (size_t i = 0; i < alias->argc; ++i)
		freeArg(alias->args[i]);
	free(alias->args);
	free(alias);

	info->map = tableRemove(info->map, &info->buckets, name);

	return 1;
}

int aliasPrint(AliasMap *info, char *name, FILE *restrict stream) {
	TableEntry *entry = tableSearch(info->map, info->buckets, name);
	if (entry == NULL)
		return 1;

	fprintf(stream, "%s=%s\n", name, ((Alias*)entry->data)->str);
	return 0;
}

void aliasList(AliasMap *info, FILE *restrict stream) {
	for (unsigned long long bucket = 0; bucket < info->buckets; ++bucket) {
		long entries = info->map[bucket].size, entry = 0;
		if (entries > 0)
			for (struct node *node = info->map[bucket].next; entry < entries; ++entry, node = node->next)
				fprintf(stream, "%s=%s\n", node->entry.key, ((Alias*)node->entry.data)->str);
	}
}
