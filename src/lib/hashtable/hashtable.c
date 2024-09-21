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
	return jhash((ub1 *)stream->begin, length, 0) & ((1<<16)-1);
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

extern unsigned long num_unique_entries;

static inline void update_unique(unsigned long entries)
{
	if (entries > num_unique_entries)
		num_unique_entries = entries;
}

void __hash_insert(struct hashtable *table, struct stream *stream)
{
	unsigned long h = jenkins_hash(stream);
	struct bucket *b = table->map[h];

	if (!b) {
		b = alloc(sizeof(*b));
		b->key = stream;
		table->map[h] = b;
		table->unique++;
		// assert (!(num_unique_entries > (1<<16)));
		update_unique(table->unique);
	} else {
		table->hits++;
		size_t len = stream->end - stream->begin;
		assert(!memcmp(b->key->begin, stream->begin, len));
	}
	b->count++;
}

void hash_insert(struct hashtable *table, struct stream *stream)
{
	size_t len;
	int i;

	if (table->num_internal <= NUM_INTERNAL) {
		// Lookup in the internal hashtable we just fetched the cacheline for.
		len = stream->end - stream->begin;
		for (i = 0; i < table->num_internal; i++) {
			struct bucket *b = &table->_bucket[i];
			size_t b_len = b->key->end - b->key->begin;

			if (!b->key) {
				// No match so far and we found an empty slot. Insert
				b->key = stream;
				table->num_internal++;
				table->unique++;
				update_unique(table->unique);
				return;
			}

			if (len != b_len)
				continue;

			if (!memcmp(b->key->begin, stream->begin, len)) {
				// Match
				b->count++;
				table->hits++;
				return;
			}
		}

		if (table->num_internal < NUM_INTERNAL) {
			struct bucket *b = &table->_bucket[i];
			b->key = stream;
			table->num_internal++;
			b->count++;
			table->unique++;
			update_unique(table->unique);
			return;
		}

		// If we get here then we failed to match stream to the internal
		// buckets. Expand to the indirect buckets.
		table->num_internal = NUM_INTERNAL + 1;
		assert(i == NUM_INTERNAL);

		for (int i = 0; i < NUM_INTERNAL; i++) {
			struct bucket *b = &table->_bucket[i];
			__hash_insert(table, b->key);
		}

		/* FALLTHROUGH */
	}
	/* Slow path*/
	__hash_insert(table, stream);
}

/*
 * Search for the given key in the hash table and return the associated
 * value. In our case, that's a count.
 */
int hash_lookup(struct hashtable *table, struct stream *stream)
{
	struct bucket *b;
	size_t len = stream->end - stream->begin;

	if (table->num_internal > NUM_INTERNAL) {
		// Slow path
		b = table->map[jenkins_hash(stream)];
		return b->count;
	}

	for (int i = 0; i < table->num_internal; i++) {
		size_t b_len;

		b = &table->_bucket[i];
		if (!b->key)
			return -1;

		b_len = b->key->end - b->key->begin;
		if (len != b_len)
			continue;

		if (!memcmp(b->key->begin, stream->begin, len))
			return b->count;
	}

	return -1;
}
