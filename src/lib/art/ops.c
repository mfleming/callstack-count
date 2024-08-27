/*
 * Implementation of Adaptive Radix Trees.
 *
 * TODO: Each node in the tree can be one of several types.
 * In addition, we use two optimisations: lazy expansion and path compression.
 *
 * For the purpose of storing Linux callstacks, keys are a sequence of
 * instruction ips and map pointers (both 8-bytes), and the sequence can be
 * arbitrary long.
 *
 * Instead of working with 8 bytes at a time we treat the key as a sequence of
 * bytes. This is inefficient but significantly simplifies the implementation.
 *
 * Note that node removal is not implemented because perf does not require it.
 *
 * TODO: Right now we use a static span of 256 children, but picking the span
 * dynamically based on the number of children would be more efficient and is
 * exactly what the paper was written for.
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
 * support that. Instead, we just use the ip and map and feed a stream of bytes
 * into the radix tree.
 */
struct stream {
    u8 *data;
    /* Pointer to the end of the data. See stream_end() */
    u8 *end;
    /* The current position into data, in 1-byte increments */
    unsigned int pos;
};

static struct stream _stream;

static inline void stream_init(struct stream *stream, u8 *data)
{
    stream->pos = 0;
    stream->data = data;
}

static inline bool stream_end(struct stream *stream)
{
    u8 *ptr = &stream->data[stream->pos];
    return ptr >= stream->end;
}

/*
 * Callers must call stream_end() before this function to check whether
 * there is any data left to consume.
 */
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
 * With the 256 node type we can simply use the partial key as an index into
 * the children array.
 *
 * TODO: The paper also includes 4, 16, and 48 node types but working with
 * the keys becomes more complicated.
 */
struct radix_tree_node {
    /* Pointers to children nodes */
    unsigned long children[256];

    /* How many IP-map pairs matched this path */
    unsigned long count;
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
    struct radix_tree_node *node = root;

    while (!stream_end(stream)) {
        u8 key = stream_next(stream);

        if (!node->children[key]) {
            node->children[key] = (unsigned long)alloc_node();
        }

        node = (struct radix_tree_node *)node->children[key];
    }

    /* Now we're at the final node and we need to bump the count. */
    node->count += 1;
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

    stream_init(stream, (u8 *)stack);
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
