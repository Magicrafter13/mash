#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashTable.h"

/*
 * Create a new node that points to NULL.
 * Returns pointer to this node.
 */
Node *init() {
	Node *sentinel = malloc(sizeof (Node));
	sentinel->next = NULL;
	return sentinel;
}

/*
 * Add a node containing a hashed string as its data to the list.
 * Returning 1 if successful, and 0 if not.
 */
int add(Node *ll, struct _entry entry) {
	// Make sure sentinel exists
	if (ll == NULL)
		return 0;

	// Find next empty node
	Node *new_node = malloc(sizeof (Node));

	// If malloc returned NULL, memory is full!
	if (new_node == NULL)
		return 0;

	// Update previous_node to point to our new node, and set the new node to point to current_node
	*new_node = (Node){ .entry = entry, .next = ll->next };
	ll->next = new_node;

	// Use sentinel node's unused pointer as counter for number of items in list :)
	++ll->size;

	return 1;
}

/*
 * Check if a node contains a string.
 * If it does, return the node that contains it, otherwise NULL.
 */
Node *search(Node *ll, unsigned long long hash, char *str) {
	for (Node *n = ll->next; n != NULL; n = n->next) {
		// If the hash doesn't match, we know the string can't, so don't waste cycles
		// on strcmp!
		if (hash != n->entry.hash)
			continue;

		if (!strcmp(n->entry.key, str))
			return n;
	}

	return NULL;
}

int removeNode(Node *ll, unsigned long long hash, char *str) {
	for (Node *p = ll, *n = p->next; n != NULL; p = n, n = n->next) {
		if (hash != n->entry.hash)
			continue;

		if (!strcmp(n->entry.key, str)) {
			p->next = n->next;
			free(n->entry.key);
			free(n);
			return 1;
		}
	}

	return 0;
}

/*
 * Free all nodes created with malloc in provided list.
 */
void free_nodes(Node *ll) {
	if (ll == NULL)
		return;
	Node *next = ll->next;
	free(ll->entry.key);
	free(ll);
	return free_nodes(next);
}
