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

/*
 * Input stream of bytes.
 *
 * Each entry is a pair of (map, ip) where map is the address of the
 * map and ip is the instruction pointer.
 * 
 * Streams return bytes at a time.
 * 
 * Perf supports all kinds of extra bits of info to figure out if two samples
 * are the same or not, such as the branch count, cycles count, etc. We do not
 * support that. Instead, we just use the ip and map.
 */
struct stream {
    u8 *data;
    /* Pointer to the end of the data. See stream_end() */
    u8 *end;
    /* The current position into data, in 1-byte increments */
    unsigned int pos;
};

static struct stream _stream;

static inline bool stream_end(struct stream *stream)
{
    u8 *ptr = &stream->data[stream->pos];
    return ptr >= stream->end;
}

static inline u8 stream_next(struct stream *stream)
{
    return stream->data[stream->pos++];
}

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

/*
 * A radix tree node that stores 256 children.
 *
 * TODO: The paper also includes 4, 16, and 48 node types but working
 * with the keys becomes more complicated. With the 256 node type we
 * can simply use the key as an index into the children array.
 */
struct radix_tree_node {
    unsigned long children[256];
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

// static void mjf(void) {
    // printf("Here\n");
// }

static void insert(struct radix_tree_node *root, struct stream *stream)
{
    struct radix_tree_node *node;
    u8 key;

    if (stream_end(stream)) {
        return;
    }

    key = stream_next(stream);

    // Can this fail?
    if (root->children[key]) {
        struct radix_tree_node *node = (struct radix_tree_node *)root->children[key];
        insert(node, stream);
        return;
    } else {
        node = alloc_node();
        root->children[key] = (unsigned long)node;
        insert(node, stream);
    }
}

static void art_tree_insert(struct callstack_tree *tree,
                            struct callstack_entry *stack)
{
    struct art_priv *priv = tree->priv;
    struct stream *stream = &_stream;

    /*
     * We don't need to build a cursor (unlike the linux backend) because
     * we don't need to do any manipuation of the callchain nodes. We simply
     * feed the bytes into the ART.
     */
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        struct callstack_entry *entry = &stack[i];
        if (!entry->ip) {
            stream->end = (u8 *)entry;
            break;
        }
    }
    stream->pos = 0;
    stream->data = (u8 *)stack;
    insert(&priv->root, stream);
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
