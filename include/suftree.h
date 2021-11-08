#ifndef SUFTREE_H
#define SUFTREE_H

#include <stddef.h>

struct _suffix_tree {
	char *sf_str;
	size_t sf_len;
	size_t sf_id;
	int sf_valid;
	// TODO should this be an array for ease of coding, or leave it like this for an easier time reading??
	struct _suffix_tree *sf_lt;
	struct _suffix_tree *sf_eq;
	struct _suffix_tree *sf_gt;
};

typedef struct _suffix_tree SufTree;

#define suftreeInit(str, id) (SufTree){ str, strlen(str), id, 1, NULL, NULL, NULL }

int suftreeAdd(SufTree*, char*, size_t);

int suftreeHas(SufTree*, char*, size_t*);

void suftreeFree(SufTree*);

#endif
