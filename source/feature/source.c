#define _POSIX_C_SOURCE 200809L // strdup
#include "mash.h"
#include <stdlib.h>
#include <string.h>

void freeSingleSource(Source *source) {
	if (source->input != NULL) {
		fclose(source->input);
		source->input = NULL;
	}
	if (source->output != NULL) {
		fclose(source->output);
		source->output = NULL;
	}

	if (source->argv != NULL) {
		for (size_t i = 0; i < source->argc; ++i)
			free(source->argv[i]);
		free(source->argv);
		source->argc = 0;
		source->argv = NULL;
	}
}

Source *sourceInit() {
	Source *new_src = malloc(sizeof (Source));
	*new_src = (Source){
		.input = NULL,
		.output = NULL,
		.argc = 0,
		.argv = NULL,
		.prev = NULL,
		.next = NULL
	};
	return new_src;
}

void sourceSet(Source *source, FILE *restrict input, size_t argc, char **argv) {
	source->input = input;
	if (source->argc > 0) {
		for (size_t i = 0; i < source->argc; ++i)
			free(source->argv[i]);
		free(source->argv);
	}
	source->argc = argc;
	source->argv = calloc(argc, sizeof (char*));
	for (size_t i = 0; i < argc; ++i)
		source->argv[i] = strdup(argv[i]);
}

Source *sourceAdd(Source *source, FILE *restrict input, size_t argc, char **argv) {
	if (source->next != NULL)
		return sourceAdd(source->next, input, argc, argv);

	source->next = sourceInit();
	sourceSet(source->next, input, argc, argv);
	source->next->prev = source;
	return source->next;
}

Source *sourceClose(Source *source) {
	freeSingleSource(source);
	if (source->prev != NULL) {
		Source *prev = source->prev;
		prev->next = source->next;
		free(source);
		return prev;
	}
	return source;
}

void sourceFree(Source *source) {
	if (source->prev != NULL) {
		source->prev->next = NULL;
		sourceFree(source->prev);
	}
	if (source->next != NULL) {
		source->next->prev = NULL;
		sourceFree(source->next);
	}

	if (source->input == stdin)
		source->input = NULL;
	freeSingleSource(source);
	free(source);
}

int sourceShift(Source *source, int amount) {
	if (amount < 0) {
		fprintf(stderr, "%s: shift: amount cannot be negative\n", source->argv[0]);
		return 1;
	}
	if (amount >= source->argc) {
		fprintf(stderr, "%s: shift: amount must be <= $#\n", source->argv[0]);
		return 1;
	}
	if (amount == 0)
		return 0;
	// Free arguments being "shifted out"
	for (size_t i = 0; i < amount; ++i)
		free(source->argv[i + 1]);
	// Shift remaining arguments
	for (size_t i = 0; i < source->argc - amount - 1; ++i)
		source->argv[i + 1] = source->argv[amount + i + 1];
	source->argc -= amount;
	return 0;
}
