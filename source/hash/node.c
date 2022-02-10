#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"

/*
 * Create a new node that points to NULL.
 * Returns pointer to this node.
 */
struct node *init() {
	struct node *sentinel = malloc(sizeof(struct node));
	sentinel->next = NULL;
	return sentinel;
}

/*
 * Add a node containing a hashed string as its data to the list.
 * Returning 1 if successful, and 0 if not.
 */
int add(struct node *ll, struct strhash *strhash) {
	// Make sure sentinel exists
	if (ll == NULL)
		return 0;

	// Find next empty node
	struct node *new_node = malloc(sizeof(struct node));

	// If malloc returned NULL, memory is full!
	if (new_node == NULL)
		return 0;

	// Update previous_node to point to our new node, and set the new node to point to current_node
	*new_node = (struct node){ strhash, NULL, ll->next == NULL ? NULL : ll->next };
	ll->next = new_node;

	// Use sentinel node's unused pointer as counter for number of items in list :)
	ll->data = (struct strhash*)((long)ll->data + 1);

	return 1;
}

/*
 * Check if a node contains a string.
 * If it does, return the node that contains it, otherwise NULL.
 */
struct node *search(struct node *ll, unsigned long long hash, char *str) {
	for (struct node *n = ll->next; n != NULL; n = n->next) {
		// If the hash doesn't match, we know the string can't, so don't waste cycles
		// on strcmp!
		if (hash != n->data->hash)
			continue;

		if (!strcmp(n->data->str, str))
			return n;
	}

	return NULL;
}

int removeNode(struct node *ll, unsigned long long hash, char *str) {
	for (struct node *p = ll, *n = p->next; n != NULL; p = n, n = n->next) {
		if (hash != n->data->hash)
			continue;

		if (!strcmp(n->data->str, str)) {
			p->next = n->next;
			free(n->data->str);
			free(n->data);
			free(n);
			return 1;
		}
	}

	return 0;
}

/*
 * Free all nodes created with malloc in provided list.
 */
void free_nodes(struct node *ll) {
	if (ll == NULL)
		return;
	struct node* next = ll->next;
	free(ll->data->str);
	free(ll->data);
	free(ll);
	return free_nodes(next);
}
