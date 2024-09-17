#include <stdio.h>
#include "art.c"

/*
 * TODO: Need to add
 * - SIMD comparsion for NODE_16
 */

typedef void (*funcptr)(void);

static inline unsigned int
max_height(struct radix_tree_node *node, unsigned int max)
{
	unsigned int count = 0;

	if (!node)
		return max;

	max += 1;

	switch (node->flags) {
	case NODE_FLAGS_LEAF:
		break;
	case NODE_FLAGS_INNER_4:
		int m = max;
		for (int i = 0; i < node->key_len; i++) {
			count = max_height((struct radix_tree_node *)node->arr[i], m);
			if (count > max) {
				max = count;
			}
		}
		break;
	}
	return max;
}

#define STREAM_ENTRY(k) k, k + strlen(k)

static void test1(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"foobar",
		"fubar",
		NULL,
	};

	struct stream s[] = {
		{ keys[0], keys[0] + strlen(keys[0]) },
		{ keys[1], keys[1] + strlen(keys[1]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(root->key_len == 2);
	assert(root->key[0] == 'o');
	assert(root->key[1] == 'u');
	assert(root->prefix_len == 1);
	assert(root->prefix[0] == 'f');
	assert(max_height(root, 0) == 2);
}

/*
 * TEST: Inserting a partial match key should split a leaf at
 * the point where the keys differ.
 */
static void test2a(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"ABCDEFG",
		"ABCDE",
		NULL,
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(!strcmp(root->prefix, "ABCDE"));

	struct stream s2 = {STREAM_ENTRY("ABCDEH")};
	insert(&root, &s2, NULL, 0);

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(!strcmp(root->prefix, "ABCDE"));
	assert(root->key[0] == 'F');
	assert(root->key[1] == 'H');
}

static void test2b(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"ABCDE",
		"ABCDEFG",
		NULL,
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(!strcmp(root->prefix, "ABCDE"));

	struct stream s2 = {STREAM_ENTRY("ABCDEH")};
	insert(&root, &s2, NULL, 0);

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(!strcmp(root->prefix, "ABCDE"));
	assert(root->key[0] == 'F');
	assert(root->key[1] == 'H');
}

/*
 * TEST: Repeatedly inserting the same key shouldn't create new nodes
 */
static void test3(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"ABCDEFG",
		NULL,
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[0]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_LEAF);
	assert(root->key_len == strlen(keys[0]));
	assert(max_height(root, 0) == 1);
}

/*
 * TEST: Inserting progressively longer keys should create new leaves
 */
static void test4(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"A",
		"AB",
		"ABC",
		"ABCD",
		"ABCDE",
		"ABCDEF",
		"ABCDEFG",
		"ABCDEFGH",
		"ABCDEFGHI",
		"ABCDEFGHIJ",
		"ABCDEFGHIJK",
		"ABCDEFGHIJKL",
		"ABCDEFGHIJKLM",
		"ABCDEFGHIJKLMN",
		"ABCDEFGHIJKLMNO",
		"ABCDEFGHIJKLMNOP",
		"ABCDEFGHIJKLMNOPQ",
		"ABCDEFGHIJKLMNOPQR",
		"ABCDEFGHIJKLMNOPQRS",
		"ABCDEFGHIJKLMNOPQRST",
		"ABCDEFGHIJKLMNOPQRSTU",
		"ABCDEFGHIJKLMNOPQRSTUV",
		"ABCDEFGHIJKLMNOPQRSTUVW",
		"ABCDEFGHIJKLMNOPQRSTUVWX",
		"ABCDEFGHIJKLMNOPQRSTUVWXY",
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
		NULL,
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ STREAM_ENTRY(keys[2]) },
		{ STREAM_ENTRY(keys[3]) },
		{ STREAM_ENTRY(keys[4]) },
		{ STREAM_ENTRY(keys[5]) },
		{ STREAM_ENTRY(keys[6]) },
		{ STREAM_ENTRY(keys[7]) },
		{ STREAM_ENTRY(keys[8]) },
		{ STREAM_ENTRY(keys[9]) },
		{ STREAM_ENTRY(keys[10]) },
		{ STREAM_ENTRY(keys[11]) },
		{ STREAM_ENTRY(keys[12]) },
		{ STREAM_ENTRY(keys[13]) },
		{ STREAM_ENTRY(keys[14]) },
		{ STREAM_ENTRY(keys[15]) },
		{ STREAM_ENTRY(keys[16]) },
		{ STREAM_ENTRY(keys[17]) },
		{ STREAM_ENTRY(keys[18]) },
		{ STREAM_ENTRY(keys[19]) },
		{ STREAM_ENTRY(keys[20]) },
		{ STREAM_ENTRY(keys[21]) },
		{ STREAM_ENTRY(keys[22]) },
		{ STREAM_ENTRY(keys[23]) },
		{ STREAM_ENTRY(keys[24]) },
		{ STREAM_ENTRY(keys[25]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(root->key_len == strlen(keys[0]));
	int height = max_height(root, 0);
	assert(height == 26);
}

/*
 * TEST: Two keys with same length but different contents should not match
 */
static void test5(void)
{
	struct radix_tree_node *root = NULL;

	art_key_t *keys[] = {
		"ABC",
		"DEF",
		NULL,
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&root, sp, NULL, 0);
	}

	assert(root->flags == NODE_FLAGS_INNER_4);
	assert(root->key_len == 2);
	assert(max_height(root, 0) == 2);
}

/*
 * TEST: Prefixes larger than PREFIX_LEN should create a chain of
 * inner nodes. And everything should continue to work if a new key
 * splits that chain.
 */
static void test6a(void)
{
	struct radix_tree_node *r = NULL;
	unsigned int prefix_sz = sizeof(r->prefix) / sizeof(r->prefix[0]);
	unsigned int remaining = prefix_sz * 3;
	art_key_t *keys[3];

	keys[0] = calloc(1, remaining);
	keys[1] = calloc(1, remaining);
	memset(keys[0], 'A', 2* prefix_sz + 1);
	memset(keys[1], 'A', 2*prefix_sz + 1);

	remaining -= ((2*prefix_sz) + 1);
	memset(keys[0]+2*prefix_sz+1, 'X', remaining);
	memset(keys[1]+2*prefix_sz+1, 'Y', remaining);

	// Can't use the STREAM_ENTRY() macro here because we're not using
	// static strings, rather we're building the keys by hand
	struct stream s[] = {
		{ keys[0], keys[0] + prefix_sz * 3 },
		{ keys[1], keys[1] + prefix_sz * 3 },
		{ NULL, NULL }
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&r, sp, NULL, 0);
	}

	assert(r->flags == NODE_FLAGS_INNER_4);
	int height = max_height(r, 0);
	assert(height == 3);

	// Insert a new key that will split the root node 
	keys[2] = "AAAAAAB";
	struct stream s2 = { STREAM_ENTRY(keys[2]) };
	insert(&r, &s2, NULL, 0);
	height = max_height(r, 0);
	assert(height == 4);
	assert(r->key_len == 2);
	assert(r->prefix_len == strlen("AAAAAA"));
}

static void test6b(void)
{
	struct radix_tree_node *r = NULL;
	unsigned int prefix_sz = sizeof(r->prefix) / sizeof(r->prefix[0]);
	art_key_t *keys[2];

	keys[0] = calloc(1, prefix_sz + 1);
	keys[1] = calloc(1, prefix_sz + 1);
	memset(keys[0], 'A', prefix_sz);
	memset(keys[1], 'A', prefix_sz);
	keys[0][prefix_sz] = 'B';
	keys[1][prefix_sz] = 'C';

	// Can't use the STREAM_ENTRY() macro here because we're not using
	// static strings, rather we're building the keys by hand
	struct stream s[] = {
		{ keys[0], keys[0] + prefix_sz + 1 },
		{ keys[1], keys[1] + prefix_sz + 1 },
		{ NULL, NULL }
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&r, sp, NULL, 0);
	}

	assert(r->flags == NODE_FLAGS_INNER_4);
	int height = max_height(r, 0);
	assert(height == 2);
}

/*
 * TEST: Filling up nodes with new keys should cause them to grow.
 * This test is intimately tied to the structure of inner nodes and
 * it would be an improvement to abstract this somehow. Maybe iterating
 * over all NODE_FLAGS_INNER* values and passing them to node_size().
 */
static void test7(void)
{
	struct radix_tree_node *r = NULL;

	art_key_t *keys[] = {
		"A", "B", "C", "D", "E"
	};

	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ STREAM_ENTRY(keys[2]) },
		{ STREAM_ENTRY(keys[3]) },
		{ STREAM_ENTRY(keys[4]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s; sp->data; sp++) {
		insert(&r, sp, NULL, 0);
	}

	assert(r->flags == NODE_FLAGS_INNER_16);
	assert(r->key_len == ARRAY_SIZE(s) - 1);

	art_key_t *keys2[] = {
		"F","G","H","I","J","K","L","M","N","O","P","Q"
	};
	struct stream s2[] = {
		{ STREAM_ENTRY(keys2[0]) },
		{ STREAM_ENTRY(keys2[1]) },
		{ STREAM_ENTRY(keys2[2]) },
		{ STREAM_ENTRY(keys2[3]) },
		{ STREAM_ENTRY(keys2[4]) },
		{ STREAM_ENTRY(keys2[5]) },
		{ STREAM_ENTRY(keys2[6]) },
		{ STREAM_ENTRY(keys2[7]) },
		{ STREAM_ENTRY(keys2[8]) },
		{ STREAM_ENTRY(keys2[9]) },
		{ STREAM_ENTRY(keys2[10]) },
		{ STREAM_ENTRY(keys2[11]) },
		{ NULL, NULL },
	};

	for (struct stream *sp = s2; sp->data; sp++) {
		insert(&r, sp, NULL, 0);
	}

	assert(r->flags == NODE_FLAGS_INNER_48);
	assert(r->key_len == ARRAY_SIZE(s) + ARRAY_SIZE(s2) - 2);
}

static unsigned long count(struct radix_tree_node *root, struct stream *stream)
{
	struct radix_tree_node *entry;

	entry = search(root, stream, 0);
	if (!entry)
		return 0;

	return entry->count;
}

/*
 * TEST: Inserting a set of unique keys N times should set their
 * individual counts to N.
 */
static void test8(void)
{
	struct radix_tree_node *r = NULL;
	art_key_t *keys[] = {
		"ABC", "DEF", "DGH", "DEFZKS", "Z", "SKJ",
	};
	struct stream s[] = {
		{ STREAM_ENTRY(keys[0]) },
		{ STREAM_ENTRY(keys[1]) },
		{ STREAM_ENTRY(keys[2]) },
		{ STREAM_ENTRY(keys[3]) },
		{ STREAM_ENTRY(keys[4]) },
		{ STREAM_ENTRY(keys[5]) },
		{ NULL, NULL },
	};
	int N = 5;

	for (int i = 0; i <= N; i++) {
		for (struct stream *sp = s; sp->data; sp++) {
			insert(&r, sp, NULL, 0);
		}
	}

	for (struct stream *sp = s; sp->data; sp++) {
		unsigned long c = count(r, sp);
		assert(c == N);
	}
}

static void test9(void)
{
	struct radix_tree_node *n, *r = NULL;
	struct stream s = { STREAM_ENTRY("foo") };
	int c;

	insert(&r, &s, NULL, 0);
	n = search(r, &s, 0);
	assert(n != NULL);
	assert(!strcmp(n->key, "foo"));
}

static void test10(void)
{
	struct radix_tree_node *n, *r = NULL;
	struct stream s[] = { 
		{ STREAM_ENTRY("DEF") },
		{ STREAM_ENTRY("DGH") },
		{ STREAM_ENTRY("DEFGZ") },
	};
	int c;

	insert(&r, &s[0], NULL, 0);
	n = search(r, &s[0], 0);
	assert(n != NULL);
	assert(!strcmp(n->key, "DEF"));

	insert(&r, &s[1], NULL, 0);
	n = search(r, &s[1], 0);
	assert(n != NULL);
	assert(!strcmp(n->key, "DGH"));

	insert(&r, &s[2], NULL, 0);
	n = search(r, &s[2], 0);
	assert(n != NULL);
	assert(!strcmp(n->key, "DEFGZ"));
}

int main(int argc, char **argv)
{
	unsigned int num_tests = 0;
	funcptr tests[] = {
		test1,
		test2a,
		test2b,
		test3,
		test4,
		test5,
		test6a,
		test6b,
		test7,
		test8,
		test9,
		test10,
		NULL,
	};

	for (funcptr *f = tests; *f != NULL; f++, num_tests++) {
		(*f)();
	}

	printf("Ran %u tests\n", num_tests);
	return 0;
}

// vim: ts=8 sw=8 noexpandtab
