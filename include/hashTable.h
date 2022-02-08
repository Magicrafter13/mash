#include "node.h"

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
