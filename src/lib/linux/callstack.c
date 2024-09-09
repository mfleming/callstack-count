#include <stdlib.h>
#include <stdbool.h>
#include <linux/list.h>
#include "callstack.h"
#include "callchain.h"
#include "data/data.h"

struct linux_priv {
    struct callchain_root root;
};

static void insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct callchain_cursor *cursor = get_tls_callchain_cursor();
    struct linux_priv *priv = tree->priv;

	cursor->nr = 0;
	cursor->last = &cursor->first;

    // Build a callchain cursor
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        struct callstack_entry *entry = &stack[i];
        if (!entry->ip) {
            break;
        }

        struct map_symbol *ms = get_map(entry->map);
        callchain_cursor_append(cursor, entry->ip, ms, false, NULL, 0, 0, 0, NULL);
    }

    if (!cursor->nr)
        return;

    callchain_append(&priv->root, cursor, 0);
}

static struct callstack_tree *linux_tree_new()
{
   // printf("alloc\n");
    struct callstack_tree *t = ccalloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->priv = ccalloc(1, sizeof(struct linux_priv));
    if (!t->priv) {
        die();
    }

    struct linux_priv *priv = t->priv;
    callchain_init(&priv->root);

    t->insert = insert;

    return t;
}

static void callstack_put(struct callstack_tree *tree)
{
    // TODO remove from tree_list.
    cfree(tree, false);
}

static void callstack_stats(struct callstack_tree *cs_tree, struct stats *stats)
{
    struct linux_priv *priv = cs_tree->priv;
    struct rb_node *n = rb_first(&priv->root.node.rb_root_in);

    unsigned long this_full_matches = 0;
    unsigned long this_cumul_counts = 0;
    while (n) {
        struct callchain_node *cnode = rb_entry(n, struct callchain_node, rb_node_in);     
        // cumul_counts += callchain_cumul_counts(cnode);
        this_cumul_counts += callchain_cumul_counts(cnode);
        this_full_matches += cnode->count;
        n = rb_next(n);
    }
    if (!this_cumul_counts) {
        // Some tree had zero matches?
        return;
    }

    // avg_full_matches += this_full_matches/this_cumul_counts;
    // printf("Max children: %lu\n", max_num_children);
    // printf("avg_full_matches: %lu\n", avg_full_matches);
    // printf("Cumulative counts: %lu\n", cumul_counts);
    // stats->avg_full_matches = (double)avg_full_matches / stats->num_trees * 100;
}

struct callstack_ops linux_ops = {
    .put = callstack_put,
    .stats = callstack_stats,
    .new = linux_tree_new,
};
