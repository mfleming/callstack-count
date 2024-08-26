#include <stdio.h>
#include <stdlib.h>

#include "callstack.h"
#include "data/data.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct record records[] = {
#include "gen.d"
};

void insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct callstack_entry *entry;
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        entry = &stack[i];
        if (!entry->ip) {
            break;
        }
        printf("ip: 0x%016lx, map: 0x%016lx\n", entry->ip, entry->map);
    }
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

struct callstack_ops cs = {
    .get = callstack_get,
    .put = callstack_put,
};

// struct callstack_ops *cs_ops = &cs;
struct callstack_ops *cs_ops = &linux_ops;

int main(int argc, char *argv[])
{
    struct record *r = records;
    for (int i = 0; i < ARRAY_SIZE(records); i++) {
        r = &records[i];

        struct callstack_tree *tree = cs_ops->get(r->id);
        tree->insert(tree, r->stack);
    }
    return 0;
}
