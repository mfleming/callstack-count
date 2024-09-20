#include "callstack.h"
#include "data/data.h"
#include "hashtable.c"

struct hash_priv {
    struct hashtable *table;
};

static void insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct hash_priv *priv = tree->priv;
    struct stream s;

    // Build a callchain cursor
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        struct callstack_entry *entry = &stack[i];
        if (!entry->ip) {
            s.end = (hash_key_t *)entry;
            break;
        }

    }

    s.begin = (hash_key_t *)stack;

    hash_insert(priv->table, &s);
}

static struct callstack_tree *hash_new()
{
    struct callstack_tree *t = ccalloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->priv = ccalloc(1, sizeof(struct hash_priv));
    if (!t->priv) {
        die();
    }


    // initialise root somehow
    struct hash_priv *priv = t->priv;

    priv->table = alloc_table();
    t->insert = insert;

    return t;
}

static void hash_put(struct callstack_tree *tree)
{
    // TODO remove from tree_list.
    cfree(tree, false);
}

unsigned long num_unique_entries = 0;
static void hash_stats(struct callstack_tree *cs_tree, struct stats *stats)
{
    struct hash_priv *priv = cs_tree->priv;
    printf("Unique entries in hashtable: %lu\n", priv->table->unique);
    printf("Table hits: %lu\n", priv->table->hits);
    // printf("Max unique entries: %lu\n", num_unique_entries);
}

struct callstack_ops hash_ops = {
    .put = hash_put,
    .stats = hash_stats,
    .new = hash_new,
};
