#ifndef __HASHTABLE_H__
#define __HASHTABLE_H__

#include <stdint.h>

typedef uint8_t hash_key_t;

struct stream {
	hash_key_t *begin;
	hash_key_t *end; // One past the end
};

struct bucket {
	unsigned int count;
	struct stream *key;
};

struct hashtable {
	struct bucket **map;
};

#endif /* __HASHTABLE_H__ */
