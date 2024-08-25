#define MAX_STACK_ENTRIES 256

struct record {
	unsigned long id;
	struct callstack_entry stack[MAX_STACK_ENTRIES];
};

struct record records[] = {
#include "gen.c"
};