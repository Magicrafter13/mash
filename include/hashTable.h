#ifndef MR_HASHTABLE_H
#define MR_HASHTABLE_H

/*
 * Hash Table (Bucket) Entry
 * Contains a key,value pair, as well as its full hash.
 */
typedef struct _entry TableEntry;
struct _entry {
	unsigned long long hash;
	char *key;
	void *data;
};

/*
 * Linked list node.
 * The sentinel node has a temporary pointer, and stores the number of nodes in the chain.
 * All other nodes store a hash table entry.
 * Both types have a pointer to the next node in the chain.
 */
typedef struct _node Node;
struct _node {
	union {
		// Hash data
		TableEntry entry;
		// Temporary storage for sentinel node
		struct {
			long size;
			Node *temp;
		};
	};
	Node *next;
};

// Initialize a new sentinel node.
Node *init();

// Add a new value into the list of nodes.
int add(Node*, TableEntry);

// Checks for a value in the list.
Node *search(Node*, unsigned long long, char*);

int removeNode(Node*, unsigned long long, char*);

// Free all nodes in a list from memory.
void free_nodes(Node*);

#define CHECK_AVERAGE 0 // Whether or not to check the average collisions

// Collision values
#define MAX_SINGLE_COLLISIONS 11 // Up to n strings per bucket
#define MAX_AVERAGE_COLLISIONS 8 // Up to n strings per bucket average

#define GROWTH_FACTOR 8 // Bucket count increase factor

typedef Node hashTable;

// Create a new hash table.
hashTable *createTable(unsigned long long buckets);

// Add a string to the hash table.
hashTable *tableAdd(hashTable*, unsigned long long*, char*, TableEntry**);

// Search hash table for a string
TableEntry *tableSearch(hashTable*, unsigned long long, char*);

hashTable *tableRemove(hashTable*, unsigned long long*, char*);

#endif
