/*
 * Implementation of Adaptive Radix Trees.
 *
 * TODO: In addition, we use two optimisations: lazy expansion and path
 * compression.
 *
 * Linux callstacks can be modelled as a sequence of instruction ips and map
 * pointers (both 8-bytes), and the sequence can be arbitrarily long.
 *
 * Instead of working with 8 bytes at a time we treat the key as a sequence of
 * bytes. This is inefficient but significantly simplifies the implementation.
 *
 * Note that node removal is not implemented because perf does not require it.
 *
 * Lastly, since the whole purpose of storing callstacks in the ART is to count
 * the number of times a callstack is seen from samples, we store a count in
 * each node.
 */

#include <linux/kernel.h>
#include <stdlib.h>
#include <string.h>
#include "callchain.h"
#include "callstack.h"
#include "data/data.h"

#include "art.c"

/*
 * Input stream of bytes.
 *
 * Each entry is a pair of (map, ip) where map is the address of the
 * map and ip is the instruction pointer. Streams return bytes at a time.
 * 
 * Given the following ip/map pair (0x12345678, 0xdeadbeef), we can turn it into
 * a stream of bytes like so:
 * 
 *     { .ip = 0x12345678, .map = 0xdeadbeef }
 *
 *                      |
 *                      v
 *
 * [0x12, 0x34, 0x56, 0x78, 0xde, 0xad, 0xbe, 0xef]
 *
 * And we feed this stream of bytes into the radix tree.
 *
 * Perf supports all kinds of extra bits of info to figure out if two samples
 * are the same or not, such as the branch count, cycles count, etc. We do not
 * support that. Instead, we just use the ip and map and feed a stream of bytes
 * into the radix tree.
 *
 * We can reduce the number of nodes in the tree by being careful with the order
 * we feed bytes into the tree. For example, there are fewer map objects than
 * ip addresses, so we should feed the map object first. Similarly, we can
 * feed the ip addresses big endian to take advance of the fact that there is
 * less variability in the higher bits, e.g. 0xffff0000 and 0xffff5555 differ
 * only in the lower 16 bits.
 */

static struct callstack_tree *art_tree_get(unsigned long id)
{
    return NULL;
}

static void art_tree_put(struct callstack_tree *tree)
{
    cfree(tree, false);
}

static void
art_tree_stats(struct callstack_tree *cs_tree, struct stats *stats)
{
}

struct art_priv {
    /* Root of the ART */
    struct radix_tree_node *root;
};

extern unsigned long __max_depth;

static void art_tree_insert(struct callstack_tree *tree,
                            struct callstack_entry *stack)
{
    struct art_priv *priv = tree->priv;
    struct stream _stream;
    struct stream *stream = &_stream;
    struct radix_tree_node *leaf;

    /*
     * We don't need to build a cursor (unlike the linux backend) because
     * we don't need to do any manipuation of the callchain nodes. We simply
     * feed the bytes into the ART.
     */
    for (int n = 0; n < MAX_STACK_ENTRIES; n++) {
        struct callstack_entry *entry = &stack[n];
        if (!entry->ip) {
            stream->end = (art_key_t *)entry;
            break;
        }
    }

    stream_init(stream, (art_key_t *)stack);

    // Unroll the stream?!?!!?
    leaf = NULL;
    // leaf->key = ccalloc(1, key_len);
    // if (!leaf->key)
        // die();

    //memcpy(leaf->key, stream->data, key_len);

    insert(&priv->root, stream, leaf, 0);
}

static inline void init_root(struct radix_tree_node **root)
{
    // *root = alloc_node(NODE_FLAGS_INNER_4);
    *root = NULL;
}

static struct callstack_tree *art_tree_new()
{
    struct callstack_tree *cs_tree;
    struct art_priv *priv;

    cs_tree = ccalloc(1, sizeof(*cs_tree));
    if (!cs_tree)
        die();

    priv = ccalloc(1, sizeof(*priv));
    if (!priv)
        die();

    init_root(&priv->root);
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
