#define _POSIX_C_SOURCE 200809L // strdup
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "hashTable.h"

// Evaluate hash table performance.
hashTable *evaluate(hashTable*, unsigned long long*);

// Grow the hash table.
hashTable *growTable(hashTable*, unsigned long long*);

// Function to hash strings
unsigned long long crc64(char* string);

/*
 * Create a new hash table.
 * Specify the size in buckets.
 */
hashTable *createTable(unsigned long long buckets) {
	hashTable *table = calloc(buckets, sizeof(hashTable));
	for (unsigned long long bucket = 0; bucket < buckets; bucket++)
		table[bucket].next = NULL;
	return table;
}

/*
 * Add a string to a table.
 * The size of the table (in buckets) must also be provided.
 * Will free the provided string, so make sure you don't need it (and make
 * sure it's allocated, and not on the stack.)
 * It will also set a pointer 'entry' to the corresponding strhash structure
 * that has been created (or if it already existed, that one).
 */
hashTable *tableAdd(hashTable *table, unsigned long long *buckets, char *str, TableEntry **entry) {
	// Increment counter/add new
	unsigned long long hash = crc64(str);
	int bucket = hash % *buckets;
	Node *strNode = search(&table[bucket], hash, str);
	// If search returns NULL, this is a new string
	if (strNode == NULL) {
		TableEntry new_entry = { .hash = hash, .key = strdup(str), .data = NULL };
		add(&table[bucket], new_entry); // TODO handle 0 (bad return)
		*entry = &table[bucket].next->entry;

		// Check hash table
		if (!CHECK_AVERAGE) { // If this flag is not set, we will only test maximum
		                      // strings per bucket.
			// If the bucket being added to, has exceeded the acceptable maximum number
			// of strings, it will increase the table size.
			// We use the sentinel string pointer as a long representing how many items are
			// in the linked list (bucket).
			if (table[bucket].size >= MAX_SINGLE_COLLISIONS)
				table = growTable(table, buckets);
		}
		else {                // Otherwise, if the flag *is* set, we will also check
		                      // the average number of strings per bucket.
			table = evaluate(table, buckets);
		}
	}
	// Otherwise, it's already there, and we can set the pointer to the existing structure
	else {
		*entry = &strNode->entry;
		//free(str);
	}

	return table;
}

TableEntry *tableSearch(hashTable *table, unsigned long long buckets, char *str) {
	// Hash given string and check proper bucket
	unsigned long long hash = crc64(str);
	int bucket = hash % buckets;
	Node *strNode = search(&table[bucket], hash, str);
	if (strNode == NULL)
		return NULL;
	return &strNode->entry;
}

hashTable *tableRemove(hashTable *table, unsigned long long *buckets, char *str) {
	unsigned long long hash = crc64(str);
	int bucket = hash % *buckets;
	removeNode(&table[bucket], hash, str);
	return table;
}

/*
 * Evaluate performance of hash table.
 * Must provide the size of the table in buckets.
 * If necessary, call growTable.
 */
hashTable *evaluate(hashTable *map, unsigned long long *mapSize) {
	int increase = 0;
	int totalStrings = 0;

	// Check each bucket, and calculate average string count
	for (unsigned long long bucket = 0; bucket < *mapSize; bucket++) {
		int stringsInBucket = 0;
		for (Node *string = map[bucket].next; string != NULL; string = string->next)
			stringsInBucket++;

		// Check threshold
		if (stringsInBucket >= MAX_SINGLE_COLLISIONS) {
			increase = 1;
			break;
		}

		totalStrings += stringsInBucket;
	}

	// Check threshold
	if (totalStrings / *mapSize >= MAX_AVERAGE_COLLISIONS)
		increase = 1;

	// Increase size if necessary
	if (increase)
		return growTable(map, mapSize);
	return map;
}

/*
 * Grow the size of the hash table.
 * Must provide the size of the table in buckets.
 */
hashTable *growTable(hashTable *map, unsigned long long *mapSize) {
	unsigned long long newMapSize = *mapSize * GROWTH_FACTOR;

	/*
	 * Move to new map
	 */
	map = realloc(map, newMapSize * sizeof (Node));
	for (unsigned long long bucket = 0; bucket < *mapSize; bucket++) {
		map[bucket].size = 0;
		map[bucket].temp = map[bucket].next;
		map[bucket].next = NULL;
	}
	for (unsigned long long bucket = *mapSize; bucket < newMapSize; bucket++)
		map[bucket] = (Node){};
	for (unsigned long long bucket = 0; bucket < *mapSize; bucket++) {
		for (Node *stringNode = map[bucket].temp; stringNode != NULL; ) {
			int newBucket = stringNode->entry.hash % newMapSize;
			Node *old = map[newBucket].next;
			map[newBucket].next = stringNode;
			stringNode = stringNode->next;
			map[newBucket].next->next = old;
			++map[newBucket].size;
		}
	}

	// Update size value
	*mapSize = newMapSize;

	// Point to new map
	return map;
}

// CRC64 code provided to me by Ben Mccamish, with permission.

#define CRC64_REV_POLY      0x95AC9329AC4BC9B5ULL
#define CRC64_INITIALIZER   0xFFFFFFFFFFFFFFFFULL
#define CRC64_TABLE_SIZE    256

/* interface to CRC 64 module */

/* crc64 takes a string argument and computes a 64-bit hash based on */
/* cyclic redundancy code computation.                               */

unsigned long long crc64(char* string) {
    static int initFlag = 0;
    static unsigned long long table[CRC64_TABLE_SIZE];
    
    if (!initFlag) { initFlag++;
        for (int i = 0; i < CRC64_TABLE_SIZE; i++) {
            unsigned long long part = i;
            for (int j = 0; j < 8; j++) {
                if (part & 1)
                    part = (part >> 1) ^ CRC64_REV_POLY;
                else part >>= 1;
            }
            table[i] = part;
        }
    }
    
    unsigned long long crc = CRC64_INITIALIZER;
    while (*string)
        crc = table[(crc ^ *string++) & 0xff] ^ (crc >> 8);
    return crc;
}
