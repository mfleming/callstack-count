/*
 * Implementation of Adaptive Radix Trees.
 *
 * TODO: Each node in the tree can be one of several types.
 * In addition, we use two optimisations: lazy expansion and path compression.
 *
 * For the purpose of storing Linux callstacks, keys are a sequence of
 * instruction ips and map pointers (both 8-bytes), and the sequence can be
 * arbitrarily long.
 *
 * Instead of working with 8 bytes at a time we treat the key as a sequence of
 * bytes. This is inefficient but significantly simplifies the implementation.
 *
 * Note that node removal is not implemented because perf does not require it.
 *
 * TODO: Right now we use a static span of 256 children, but picking the span
 * dynamically based on the number of children would be more efficient and is
 * exactly what the paper was written for.
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

static inline void stream_advance(struct stream *stream, unsigned int n)
{
    stream->pos += n;
}

static inline unsigned int stream_remaining(struct stream *stream)
{
    return stream->end - &stream->data[stream->pos];
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

#define NODE_FLAGS_LEAF  (1 << 0)
#define NODE_FLAGS_INNER (1 << 1)
struct radix_tree_node {
    /* Pointers to children nodes */
    unsigned long children[256];

    /* How many IP-map pairs matched this path */
    unsigned long count;

    /* Only used if we're a leaf node. See lazy expansion in insert() */
    u8 *key;
    unsigned int key_len;

    /* See NODE_FLAGS_* */
    unsigned int flags;
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
    struct radix_tree_node *prev = NULL;

    while (!stream_end(stream)) {
        u8 key = stream_next(stream);

        prev = node;
        node = (struct radix_tree_node *)node->children[key];
        if (!node) {
            /*
             * Lazy expansion: allocate a new leaf node and store the rest
             * of the key in it.
             */
            unsigned int remaining = stream_remaining(stream);

            node = alloc_node();
            prev->children[key] = (unsigned long)node;
            node->flags = LEAF_NODE;
            node->key_len = remaining;
            node->key = malloc(remaining);
            if (!node->key)
                die();

            /* Copy the rest of stream */
            memcpy(node->key, &stream->data[stream->pos], remaining);
            stream_advance(stream, remaining);
            goto out;
        }

        if (node->flags & NODE_FLAGS_LEAF) {
            /*
             * Compare the rest of the key. If it matches we're done.
             */
            unsigned int remaining = stream_remaining(stream);
            unsigned int min_bytes = min(remaining, node->key_len);
            struct radix_tree_node *inner;

            /* Be optimistic. Hope for whole key match */
            if (!memcmp(node->key, &stream->data[stream->pos], min_bytes)) {
                stream_advance(stream, min_bytes);
                goto out;
            }

            /*
             * Mismatch. Insert a new inner node with just the next partial key.
             */
            inner = alloc_node();
            prev->children[key] = (unsigned long)inner;
            inner->flags = INNER_NODE;
            inner->count = node->count;
            inner->children[node->key[0]] = (unsigned long)node;
            memmove(&node->key[1], &node->key[2], node->key_len - 1);
            node->key_len -= 1;

            node = inner;
        } else {
            assert(node->flags & NODE_FLAGS_INNER);
        }
    }

out:
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
