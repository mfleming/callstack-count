#include <stdlib.h>
#include <stdbool.h>
#include <linux/list.h>
#include "callstack.h"
#include "callchain.h"
#include "data/data.h"

struct map_tree {
    struct rb_node node;
    unsigned long id;
    struct map_symbol *ms;
};

struct map_trees {
    struct rb_root_cached node;
};
static struct map_trees map_trees;

static struct map_symbol *last_ms = NULL;

static struct map_symbol *get_map(unsigned long id) {
    struct rb_node **p = &map_trees.node.rb_root.rb_node;
    struct rb_node *parent = NULL;
    struct map_tree *mt;
    bool leftmost = true;
    struct map_symbol *ms = NULL;

    if (last_ms && last_ms->map == (void *)id) {
        return last_ms;
    }

    while (*p != NULL) {
        parent = *p;
        mt = rb_entry(parent, struct map_tree, node);
        int cmp = mt->id - id;
        if (cmp < 0)
            p = &(*p)->rb_left;
        else if (cmp > 0) {
            p = &(*p)->rb_right;
            leftmost = false;
        } else {
            ms = mt->ms;
            goto out;
        }
    }

    ms = calloc(1, sizeof(struct map_symbol));
    if (!ms) {
        die();
    }
    // Append the IP to the callchain
    ms->map = (void *)id;

    mt = calloc(1, sizeof(struct map_tree));
    if (!mt) {
        die();
    }

    mt->id = id;
    mt->ms = ms;
    rb_link_node(&mt->node, parent, p);
    rb_insert_color_cached(&mt->node, &map_trees.node, leftmost);

out:
    last_ms = ms;
    return ms;
}

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

// Replace this with a hash table
struct tree {
    struct list_head list;
    unsigned long id;
    struct callstack_tree *cs_tree;
    struct rb_node node;
};

static bool done_init = false;
struct trees {
    struct rb_root_cached entries;
};

struct trees trees;

/* Initialise various caches */
static inline void init_caches() {
        trees.entries = RB_ROOT_CACHED;
        map_trees.node = RB_ROOT_CACHED;
}

static struct callstack_tree *callstack_get(unsigned long id)
{
    struct tree *cursor;
    struct callstack_tree *t = NULL;

    if (!done_init) {
        done_init = true;
        init_caches();
    }

    struct rb_node **p = &trees.entries.rb_root.rb_node;
    struct rb_node *parent = NULL;
    struct tree *tree;
    bool leftmost = true;

    while (*p != NULL) {
        parent = *p;
        tree = rb_entry(parent, struct tree, node);

        if (tree->id < id)
            p = &(*p)->rb_left;
        else if (tree->id > id) {
            p = &(*p)->rb_right;
            leftmost = false;
        } else
            return tree->cs_tree;
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

    t->priv = calloc(1, sizeof(struct linux_priv));
    if (!t->priv) {
        die();
    }

    struct linux_priv *priv = t->priv;
    callchain_init(&priv->root);

    t->insert = insert;

    cursor = calloc(1, sizeof(*cursor));
    if (!cursor)
        die();

    cursor->cs_tree = t;
    cursor->id = id;
    rb_link_node(&cursor->node, parent, p);
    rb_insert_color_cached(&cursor->node, &trees.entries, leftmost);

    return t;
}

static void callstack_put(struct callstack_tree *tree)
{
    // TODO remove from tree_list.
    free(tree);
}

static void callstack_stats(struct stats *stats)
{
    // Walk the rbtree and count the number of entries
    struct rb_root *root = &trees.entries.rb_root;
    struct rb_node *tree_node = rb_first(root);
    struct tree *tree;

    unsigned long avg_full_matches = 0;

    unsigned long max_num_children = 0;
    unsigned long cumul_counts = 0;
    while (tree_node) {
        tree = rb_entry(tree_node, struct tree, node);
        tree_node = rb_next(tree_node);
        stats->num_trees++;

        struct linux_priv *priv = tree->cs_tree->priv;
        struct rb_node *n = rb_first(&priv->root.node.rb_root_in);

        unsigned long this_full_matches = 0;
        unsigned long this_cumul_counts = 0;
        while (n) {
            struct callchain_node *cnode = rb_entry(n, struct callchain_node, rb_node_in);     
            cumul_counts += callchain_cumul_counts(cnode);
            this_cumul_counts += callchain_cumul_counts(cnode);
            this_full_matches += cnode->count;
            n = rb_next(n);
        }
        if (!this_cumul_counts) {
            // Some tree had zero matches?
            continue;
        }
        avg_full_matches += this_full_matches/this_cumul_counts;
    }

    printf("Max children: %lu\n", max_num_children);
    printf("avg_full_matches: %lu\n", avg_full_matches);
    printf("Cumulative counts: %lu\n", cumul_counts);
    stats->avg_full_matches = (double)avg_full_matches / stats->num_trees * 100;
}

struct callstack_ops linux_ops = {
    .get = callstack_get,
    .put = callstack_put,
    .stats = callstack_stats,
};
