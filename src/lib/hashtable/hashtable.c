#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hashtable.h"
#include "callstack.h"

#include "jenkins.c"

/*
 * Implementation of a rolling hash function.
 * See https://en.wikipedia.org/wiki/Rolling_hash
 */
static inline unsigned long basic_hash(struct stream *stream)
{
	unsigned long h = 0;
	unsigned long p = 31;
	unsigned long m = 1000000009;
	unsigned long exp = 1;

	for (hash_key_t *k = stream->begin; k < stream->end; k++) {
		h += (h + *k * exp) % m;
		exp = (exp * p) % m;
	}

	// Fit into 16 bits?
	return h & ((1<<16)-1);
}

static inline unsigned long jenkins_hash(struct stream *stream)
{
	unsigned long length = stream->end - stream->begin;
	return jhash((ub1 *)stream->begin, length, 0);
}

static void *alloc(size_t size)
{
	void *ptr = ccalloc(1, size);
	if (!ptr) {
		fprintf(stderr, "Failed allocation");
		exit(EXIT_FAILURE);
	}
	return ptr;
}

struct hashtable *alloc_table(void)
{
	struct hashtable *h = alloc(sizeof(*h));
	h->map = alloc(sizeof(struct bucket *) * 1<<16);
	return h;
}

void hash_insert(struct hashtable *table, struct stream *stream)
{
	unsigned long h = basic_hash(stream); 
	struct bucket *b = table->map[h];

	if (!b) {
		b = alloc(sizeof(*b));
		b->key = stream;
		table->map[h] = b;
	} else {
		size_t len = stream->end - stream->begin;
		assert(!memcmp(b->key->begin, stream->begin, len));
	}
	b->count++;
}

/*
 * Search for the given key in the hash table and return the associated
 * value. In our case, that's a count.
 */
int hash_lookup(struct hashtable *table, struct stream *stream)
{
	struct bucket *b = table->map[basic_hash(stream)];
	return b->count;
}
