/*
 * Implementation of Adaptive Radix Trees.
 *
 * Each node in the tree can be one of several types.
 * 
 * In addition, we use two optimisations: lazy expansion and path
 * compression. 
 */
#include <stdlib.h>
#include "callchain.h"
#include "callstack.h"
#include "data/data.h"

static struct callstack_tree *art_tree_get(unsigned long id)
{
    return NULL;
}

static void art_tree_put(struct callstack_tree *tree)
{
    free(tree);
}

static void
art_tree_stats(struct callstack_tree *cs_tree, struct stats *stats)
{
}

#define LEAF_NODE  (1<< 0)
#define INNER_NODE (1<<1)

struct radix_tree_node {
    unsigned int node_type;
    unsigned int num_keys;

    /*
     * If node_type is LEAF this array holds values
     * If node_type is INNER this holds pointers to child nodes
     */
    unsigned long children[8];
    unsigned long keys[8];
};

struct art_priv {
    /* Root of the ART */
    struct radix_tree_node root;
};

static inline struct radix_tree_node *alloc_node()
{
    struct radix_tree_node *node;

    node = calloc(1, sizeof(*node));
    if (!node)
        die();

    return node;
}

static void insert(struct radix_tree_node *root, struct callchain_cursor *cursor)
{
    struct callchain_cursor_node *cnode;
    struct radix_tree_node *node;

	cnode = callchain_cursor_current(cursor);
    if (!cnode)
        return;

    // Does this key match?
    if (!root->num_keys) {
        // No children, insert a new node
        node = alloc_node();
        root->keys[0] = cnode->ip;
        root->children[0] = (unsigned long)node;
        root->num_keys++;

        callchain_cursor_advance(cursor);
        insert(node, cursor);
        return;
    }

    // Iterate over all keys, looking for a match
    for (int i = 0; i < root->num_keys; i++) {
        // TODO use match_chain_dso_addresses()
        if (cnode->ip != root->keys[i])
            continue;
        
        // Match
        node = (struct radix_tree_node *)root->children[i];
        callchain_cursor_advance(cursor);
        insert(node, cursor);
        return;
    }

    // We searched all keys and didn't find a match. Insert new one.
    assert(root->num_keys != sizeof(root->keys));

    root->keys[root->num_keys] = cnode->ip;
    root->children[root->num_keys] = (unsigned long)alloc_node();
    root->num_keys++;
}

static void art_tree_insert(struct callstack_tree *tree,
                            struct callstack_entry *stack)
{
    struct callchain_cursor *cursor = get_tls_callchain_cursor();
	struct callchain_cursor_node *cnode;
    struct art_priv *priv = tree->priv;

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

    if (!cursor->nr) {
        return;
    }

    callchain_cursor_commit(cursor);

    // Necessary?
	cnode = callchain_cursor_current(cursor);
	if (!cnode)
		return;

    insert(&priv->root, cursor);
}

static struct callstack_tree *art_tree_new()
{
    struct callstack_tree *cs_tree;
    struct art_priv *priv;

    cs_tree = calloc(1, sizeof(*cs_tree));
    if (!cs_tree)
        die();

    priv = calloc(1, sizeof(*priv));
    if (!priv)
        die();

    cs_tree->insert = art_tree_insert;
    cs_tree->priv = priv;

    return cs_tree;
}

struct callstack_ops art_ops = {
    .get = art_tree_get,
    .put = art_tree_put,
    .stats = art_tree_stats,
    .new = art_tree_new,
};
