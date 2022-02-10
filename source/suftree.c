#include "suftree.h"
#include <stdlib.h>
#include <string.h>

int suftreeAdd(SufTree *tree, char *str, size_t id) {
	size_t i = 0;
	for (; i < tree->sf_len; i++) {
		const int cmp = str[i] - tree->sf_str[i];
		if (!cmp)
			continue;
		SufTree * next = cmp > 0 ? tree->sf_gt : tree->sf_lt;
		if (next != NULL)
			return suftreeAdd(next,  &str[i], id);

		SufTree temp = *tree;
		tree->sf_eq = malloc(sizeof(SufTree));
		*tree->sf_eq = temp;
		tree->sf_eq->sf_str = &tree->sf_str[i];
		tree->sf_eq->sf_len -= i;
		tree->sf_len = i;
		tree->sf_valid = 0;

		next = malloc(sizeof (SufTree));
		*next = suftreeInit(&str[i], id);
		if (cmp > 0)
			tree->sf_gt = next;
		else
			tree->sf_lt = next;
		return 0;
	}
	// Strings, as of this far, are equal
	if (str[i] == '\0')
		return -1;
	// This instance of SufTree has length 0
	if (i == 0) {
		const int cmp = str[i] - tree->sf_str[i];
		if (cmp > 0) {
			if (tree->sf_gt != NULL)
				return suftreeAdd(tree->sf_gt, str, id);

			tree->sf_gt = malloc(sizeof (SufTree));
			*tree->sf_gt = suftreeInit(str, id);
			return 0;
		}
		if (cmp < 0) {
			if (tree->sf_lt != NULL)
				return suftreeAdd(tree->sf_lt, str, id);

			tree->sf_lt = malloc(sizeof (SufTree));
			*tree->sf_lt = suftreeInit(str, id);
			return 0;
		}
	}
	// Length is greater than zero, or first character of each are the same
	if (tree->sf_eq != NULL)
		return suftreeAdd(tree->sf_eq, &str[i], id);

	tree->sf_eq = malloc(sizeof (SufTree));
	*tree->sf_eq = suftreeInit(&str[i], id);
	return 0;
}

int suftreeHas(SufTree * tree, char *str, size_t *id) {
	if (tree == NULL)
		return 0;

	if (tree->sf_len) {
		int cmp = strncmp(str, tree->sf_str, tree->sf_len);
		if (cmp)
			return 0;
		if (str[tree->sf_len] == '\0') {
			*id = tree->sf_id;
			return tree->sf_valid;
		}
		if (tree->sf_eq == NULL)
			return 0;
	}

	int cmp = str[tree->sf_len] - tree->sf_eq->sf_str[0];
	return suftreeHas(cmp ? cmp > 0 ? tree->sf_gt : tree->sf_lt : tree->sf_eq, &str[tree->sf_len], id);
}

void suftreeFree(SufTree *tree) {
	if (tree != NULL) {
		suftreeFree(tree->sf_gt);
		suftreeFree(tree->sf_eq);
		suftreeFree(tree->sf_lt);
		free(tree);
	}
}
