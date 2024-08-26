#include <stdlib.h>
#include <stdbool.h>
#include <linux/list.h>
#include "callstack.h"
#include "callchain.h"
#include "data/data.h"

static void insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct callchain_cursor *cursor = get_tls_callchain_cursor();

    callchain_cursor_reset(cursor);

    // Build a callchain cursor
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        struct callstack_entry *entry = &stack[i];
        if (!entry->ip) {
            break;
        }
        struct map_symbol *ms = calloc(1, sizeof(struct map_symbol));
        if (!ms) {
            die();
        }
        // Append the IP to the callchain
        ms->map = (void *)entry->map;
        callchain_cursor_append(cursor, entry->ip, ms, false, NULL, 0, 0, 0, NULL);
    }

    struct callchain_root root;
    callchain_init(&root);
    callchain_append(&root, cursor, 0);
}

// Replace this with a hash table
struct tree {
    struct list_head list;
    unsigned long id;
    struct callstack_tree *cs_tree;
};

static struct list_head tree_list = LIST_HEAD_INIT(tree_list);

static struct callstack_tree *callstack_get(unsigned long id)
{
    struct tree *cursor;
    struct callstack_tree *t = NULL;

    list_for_each_entry(cursor, &tree_list, list) {
        if (cursor->id == id) {
            t = cursor->cs_tree;
            break;
        }
    }

    if (t) {
        // printf("Found for %lu\n", id);
        return t;
    }

    // printf("alloc\n");
    t = calloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->insert = insert;

    cursor = calloc(1, sizeof(*cursor));
    if (!cursor)
        die();

    cursor->cs_tree = t;
    cursor->id = id;
    list_add(&cursor->list, &tree_list);

    return t;
}

static void callstack_put(struct callstack_tree *tree)
{
    // TODO remove from tree_list.
    free(tree);
}

struct callstack_ops linux_ops = {
    .get = callstack_get,
    .put = callstack_put,
};
