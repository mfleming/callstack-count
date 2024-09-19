#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hashtable.c"

typedef void (*funcptr)(void);

#define STREAM_ENTRY(k) k, k + strlen(k)

static void test0(void)
{
	struct stream s[] = {
		{ STREAM_ENTRY("foobar") },
		{ STREAM_ENTRY("fubar") },
	};

	unsigned long h1 = basic_hash(&s[0]);
	assert(h1 == basic_hash(&s[0]));
	assert(h1 != basic_hash(&s[1]));
}

static void test1(void)
{
	struct hashtable *h = alloc_table();
	int count;
	struct stream s = { STREAM_ENTRY("foobar")};

	hash_insert(h, &s);
	count = hash_lookup(h, &s);
	assert(count == 1);
}

static void test2(void)
{
	struct hashtable *h = alloc_table();
	int count;
	struct stream s = { STREAM_ENTRY("foobar")};

	hash_insert(h, &s);
	hash_insert(h, &s);
	hash_insert(h, &s);
	hash_insert(h, &s);
}

int main(int argc, char **argv)
{
	unsigned int num_tests = 0;
	funcptr tests[] = {
		test0,
		test1,
		NULL,
	};

	for (funcptr *f = tests; *f != NULL; f++, num_tests++) {
		(*f)();
	}

	printf("Ran %u tests\n", num_tests);
	return 0;
}

// vim: ts=8 sw=8 noexpandtab
