#define _POSIX_C_SOURCE 200809L // strdup, setenv
#include "mash.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

Variables *variableInit() {
	Variables *vars = malloc(sizeof (Variables));
	vars->buckets = 16;
	vars->map = createTable(vars->buckets);
	return vars;
}

void variableFree(Variables *vars) {
	for (unsigned long long bucket = 0; bucket < vars->buckets; ++bucket) {
		for (Node *node = vars->map[bucket].next; node != NULL; node = node->next)
			free(node->entry.data);
		free_nodes(vars->map[bucket].next);
	}
	free(vars->map);
	free(vars);
}

void variableSet(Variables *vars, char *name, char *value) {
	TableEntry *entry;
	vars->map = tableAdd(vars->map, &vars->buckets, name, &entry);

	char *const new_value = strdup(value);

	// Variable already existed, so we must free some data
	if (entry->data != NULL)
		free(entry->data);

	// Populate table entry
	entry->data = new_value;
}

char *variableGet(Variables *vars, char *name) {
	TableEntry *entry = tableSearch(vars->map, vars->buckets, name);
	return entry == NULL ? NULL : entry->data;
}

void variableUnset(Variables *vars, char *name) {
	TableEntry *entry = tableSearch(vars->map, vars->buckets, name);
	if (entry == NULL)
		return;

	// Free string, and remove from table
	free(entry->data);
	vars->map = tableRemove(vars->map, &vars->buckets, name);
}

int setvar(Variables *vars, char *name, char *value, _Bool env) {
	// User is setting local variable, but this variable is already in the environment
	if (!env && getenv(name) != NULL)
		env = 1;

	// Environment variable
	if (env) {
		// User is exporting a local variable into the environment
		if (value == NULL) {
			char *local = variableGet(vars, name); // If this returns NULL the local var isn't actually set, so we'll use an empty string
			int ret = setenv(name, local == NULL ? "" : local, 1);
			int err = errno;
			if (local != NULL)
				variableUnset(vars, name);
			errno = err;
			return ret;
		}
		// User is setting an environment variable
		if (variableGet(vars, name) != NULL) // Remove local variable if it is set
			variableUnset(vars, name);
		return setenv(name, value, 1);
	}

	// Shell variable
	variableSet(vars, name, value);
	return 0;
}

char *getvar(Variables *vars, char *name) {
	char *var = getenv(name);
	if (var != NULL)
		return var;

	var = variableGet(vars, name);
	return var;
}

int unsetvar(Variables *vars, char *name) {
	if (getenv(name) == NULL) {
		variableUnset(vars, name);
		return 0;
	}
	return unsetenv(name);
}

size_t varNameLength(char *str) {
	// All of those characters are valid (except 0-9 for the first character)
	if (str[0] < '0' || str[0] > '9')
		return strspn(str, "_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	return 0;
}
