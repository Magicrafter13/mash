#ifndef HASHNODE_H
#define HASHNODE_H

struct strhash {
	unsigned long long hash;
	char *str;
	void *data;
};

/*
 * Linked list node.
 * Stores a pointer to a hashed string, and the next node in the list.
 */
struct node {
	struct strhash *data;
	struct node *temp;
	struct node *next;
};

// Initialize a new sentinel node.
struct node *init();

// Add a new value into the list of nodes.
int add(struct node*, struct strhash*);

// Checks for a value in the list.
struct node *search(struct node*, unsigned long long, char*);

int removeNode(struct node*, unsigned long long, char*);

// Free all nodes in a list from memory.
void free_nodes(struct node*);

#endif
