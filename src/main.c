#include <stdio.h>
#include <stdlib.h>

#include "callstack.h"
#include "data/data.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

void insert(struct callstack_entry *stack)
{
    printf("insert\n");
}

struct callstack_tree *callstack_get(unsigned long id)
{
    struct callstack_tree *t = calloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->insert = insert;
    return t;
}

void callstack_put(struct callstack_tree *tree)
{
    free(tree);
}

struct callstack cs = {
    .get = callstack_get,
    .put = callstack_put,
};

struct callstack *callstack = &cs;

int main(int argc, char *argv[])
{
    struct record *r = records;
    for (int i = 0; i < ARRAY_SIZE(records); i++) {
        r = &records[i];

        struct callstack_tree *tree = callstack->get(r->id);
        tree->insert(r->stack);
    }
    return 0;
}