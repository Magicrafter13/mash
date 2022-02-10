#ifndef MR_HASHTABLE_H
#define MR_HASHTABLE_H

/**************
 * Structures *
 **************/

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

#define CHECK_AVERAGE 0 // Whether or not to check the average collisions

// Collision values
#define MAX_SINGLE_COLLISIONS 11 // Up to n strings per bucket
#define MAX_AVERAGE_COLLISIONS 8 // Up to n strings per bucket average

#define GROWTH_FACTOR 8 // Bucket count increase factor

typedef struct node hashTable;

// Create a new hash table.
hashTable *createTable(unsigned long long buckets);

// Add a string to the hash table.
hashTable *tableAdd(hashTable*, unsigned long long*, char*, struct strhash**);

// Search hash table for a string
struct strhash *tableSearch(hashTable*, unsigned long long, char*);

hashTable *tableRemove(hashTable*, unsigned long long*, char*);

#endif
