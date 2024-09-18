#include "callstack.h"
#include "data/data.h"
#include "hot.c"

struct hot_priv {
    struct node root;
};

static void insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct hot_priv *priv = tree->priv;
    struct stream s;

    // Build a callchain cursor
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        struct callstack_entry *entry = &stack[i];
        if (!entry->ip) {
            s.end = (hot_key_t *)entry;
            break;
        }

    }

    s.start = (hot_key_t *)stack;

    hot_insert(&priv->root, &s);
}

static struct callstack_tree *hot_new()
{
    struct callstack_tree *t = ccalloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->priv = ccalloc(1, sizeof(struct hot_priv));
    if (!t->priv) {
        die();
    }


    // initialise root somehow
    //struct hot_priv *priv = t->priv;

    t->insert = insert;

    return t;
}

static void hot_put(struct callstack_tree *tree)
{
    // TODO remove from tree_list.
    cfree(tree, false);
}

static void hot_stats(struct callstack_tree *cs_tree, struct stats *stats)
{
}

struct callstack_ops hot_ops = {
    .put = hot_put,
    .stats = hot_stats,
    .new = hot_new,
};
