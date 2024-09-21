#ifndef __HASHTABLE_H__
#define __HASHTABLE_H__

#include <stdint.h>

typedef uint8_t hash_key_t;

struct stream {
	hash_key_t *begin;
	hash_key_t *end; // One past the end
};

struct bucket {
	struct stream *key;
	unsigned long count;
};

#define NUM_INTERNAL 3

struct hashtable {
	struct bucket _bucket[NUM_INTERNAL];
	struct bucket **map;
	unsigned char num_internal;
	unsigned long unique;
	unsigned long hits;
};

#endif /* __HASHTABLE_H__ */
