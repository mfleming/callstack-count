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

static inline unsigned long stream_size(struct stream *stream)
{
    return (unsigned long)(stream->end - stream->data);
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

#define NODE_FLAGS_LEAF      (1 << 0)
#define NODE_FLAGS_INNER_4   (1 << 1)
#define NODE_FLAGS_INNER_16  (1 << 2)
#define NODE_FLAGS_INNER_48  (1 << 3)
#define NODE_FLAGS_INNER_256 (1 << 4)

/*
 * A radix tree node.
 */
struct radix_tree_node {
    /* How many IP-map pairs matched this path */
    unsigned long count;

    /* Only used if we're a leaf node. See lazy expansion in insert() */
    u8 *key;
    unsigned int key_len;

    /* See NODE_FLAGS_* */
    unsigned int flags;

    /* This is necessary for expanding nodes when looking at leaves during insert() */
    u8 prefix[512];
    unsigned int prefix_len;

    /*
     * Array of keys and pointers.
     *
     * If the flags field is NODE_FLAGS_LEAF, then this is a leaf node
     * and the children array is unused.
     *
     * If the flags field is NODE_FLAGS_INNER_4, then this is an inner
     * node with 4 keys and 4 pointers to children. The keys are stored
     * in the first 4 bytes of the data array and the pointers are stored
     * in the next 4 bytes.
     *
     * If the flags field is NODE_FLAGS_INNER_16, then this is an inner
     * node with 16 keys and 16 pointers to children. You can find the key
     * efficiently with a binary search.
     *
     * If the flags field is NODE_FLAGS_INNER_48, then this is an inner
     * node with 48 keys. The keys index a second array of pointers to
     * children.
     *
     * If the flags field is NODE_FLAGS_INNER_256, then this is an inner
     * node with 256 keys. The keys index directly into the children
     * array.
     */
    unsigned long *arr;
};

struct art_priv {
    /* Root of the ART */
    struct radix_tree_node *root;
};

static inline struct radix_tree_node *alloc_node(unsigned int flags)
{
    struct radix_tree_node *node;
    unsigned int size = sizeof(*node);

    node = calloc(1, size);
    if (!node)
        die();

    switch (flags) {
    case NODE_FLAGS_LEAF:
        break;
    case NODE_FLAGS_INNER_4:
        /* 4 keys and 4 pointers */
        node->key = calloc(1, 4 * sizeof(u8));
        if (!node->key)
            die();

        node->arr = calloc(1, 4 * sizeof(unsigned long));
        if (!node->arr)
            die();

        break;
    case NODE_FLAGS_INNER_16:
        /* 16 keys and 16 pointers */
        node->key = calloc(1, 16 * sizeof(u8));
        if (!node->key)
            die();

        node->arr = calloc(1, 16 * sizeof(unsigned long));
        if (!node->arr)
            die();

         break;
    case NODE_FLAGS_INNER_48:
        /* 4 keys and 4 pointers */
        node->key = calloc(1, 48 * sizeof(u8));
        if (!node->key)
            die();

        node->arr = calloc(1, 48 * sizeof(unsigned long));
        if (!node->arr)
            die();

         break;
    case NODE_FLAGS_INNER_256:
        /* 4 keys and 4 pointers */
        // node->key = calloc(1, 4 * sizeof(unsigned long));
        // if (!node->key)
            // die();

        node->arr = calloc(1, 256 * sizeof(unsigned long));
        if (!node->arr)
            die();

         break;
    default:
        die();
    }

    node->flags = flags;

    return node;
}

static void free_node(struct radix_tree_node *node)
{
    switch (node->flags) {
    case NODE_FLAGS_LEAF:
        free(node->key);
        break;
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
    case NODE_FLAGS_INNER_48:
        free(node->key);
    case NODE_FLAGS_INNER_256:
        free(node->arr);
        break;
    default:
        die();
    }

    free(node);
}

/*
 * Have all the keys of node been assigned?
 *
 * This function is used to decide when to reallocate node
 * with a larger size (number of children).
 */
static inline bool is_full(struct radix_tree_node *node)
{
    bool full = false;

    switch (node->flags) {
    case NODE_FLAGS_LEAF:
        /*
         * Leaf nodes are always full and require callers to insert a new
         * node if expansion is needed.
         */
        return true;
    case NODE_FLAGS_INNER_4:
        return node->key_len == 4;
    case NODE_FLAGS_INNER_16:
        return node->key_len == 16;
    case NODE_FLAGS_INNER_48:
        return node->key_len == 48;
    case NODE_FLAGS_INNER_256:
        return node->key_len == 256;
    default:
        die();
    }
    return full;
}

static inline bool is_leaf(struct radix_tree_node *node)
{
    return node->flags & NODE_FLAGS_LEAF;
}

/*
 * Given a node, lookup the node for key. If missing is missing return
 * the slot to insert a new node at.
 *
 * The lookup method varies based on the type of node.
 */
struct radix_tree_node **find_child(struct radix_tree_node *node, u8 key)
{
    if (!node)
        goto out;

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
        for (int i = 0; i < 4; i++) {
            if (node->key[i] == key)
                return (struct radix_tree_node **)&node->arr[i];
        }
        return NULL;

    case NODE_FLAGS_INNER_16:
        for (int i = 0; i < 16; i++) {
            if (node->key[i] == key)
                return (struct radix_tree_node **)&node->arr[i];
        }
        return NULL;

    case NODE_FLAGS_INNER_256:
        return (struct radix_tree_node **)&node->arr[key];

    case NODE_FLAGS_INNER_48:
        for (int i = 0; i < 48; i++) {
            if (node->key[i] == key)
                return (struct radix_tree_node **)&node->arr[i];
        }
        return NULL;

    default:
        die();
    }

out:
    return NULL;
}

static void replace(struct radix_tree_node **node, struct radix_tree_node *leaf)
{
    // TODO need to handle freeing memory?
    // struct radix_tree_node *n = *node;
    // if (n) {
    //     free(n->key);
    //     free(n);
    // }

    *node = leaf;
}

static u8 *load_key(struct radix_tree_node *node)
{
    return node->key;
}

static void add_child(struct radix_tree_node *node, u8 key,
                      struct radix_tree_node *child)
{
    switch(node->flags) {
    case NODE_FLAGS_INNER_4:
        assert(node->key_len != 4); // Full
        node->key_len += 1;
        node->key[node->key_len - 1] = key;
        node->arr[node->key_len - 1] = (unsigned long)child;
        break;
    case NODE_FLAGS_INNER_16:
        assert(node->key_len != 16); // Full
        node->key_len += 1;
        node->key[node->key_len - 1] = key;
        node->arr[node->key_len - 1] = (unsigned long)child;
        break;
    case NODE_FLAGS_INNER_48:
        assert(node->key_len != 48); // Full
        node->key_len += 1;
        node->key[node->key_len - 1] = key;
        node->arr[node->key_len - 1] = (unsigned long)child;
        break;
    case NODE_FLAGS_INNER_256:
        assert(node->key_len != 256); // Full
        node->key_len += 1;
        node->arr[key] = (unsigned long)child;
        break;
     default:
        die();
    }
}

/*
 * Compare the path of node with key and return the number of equal bytes.
 */
static unsigned int check_prefix(struct radix_tree_node *node,
                                 struct stream *stream, int depth)
{

    // Depth should represent how much of stream we've consumed so far
    // printf("%lu %u %u\n", stream_size(stream), stream_remaining(stream), depth);
    // assert((stream_size(stream) - stream_remaining(stream)) == depth);

    // assert(node->prefix_len > 0);

    u8 *key = stream->data;
    unsigned int min_len = min(node->prefix_len, (unsigned int)stream_size(stream) - depth);
    int i;

    for (i = 0; i < min_len; i++) {
        if (node->prefix[i] != key[depth + i])
            break;
    }
    return i;
}

// Grow in-place.
static void grow(struct radix_tree_node *node)
{
    u8 *new_keys;
    unsigned long *new_children;

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
        // Bump to NODE_FLAGS_INNER_16
        new_keys = calloc(1, 16 * sizeof(u8));
        if (!new_keys)
            die();
        new_children = calloc(1, 16 * sizeof(unsigned long));
        if (!new_children)
            die();

        memcpy(new_keys, node->key, 4 * sizeof(u8));
        memcpy(new_children, node->arr, 4 * sizeof(unsigned long));
        free(node->key);
        free(node->arr);
        node->key = new_keys;
        node->arr = new_children;
        node->flags = NODE_FLAGS_INNER_16;
        break;
    case NODE_FLAGS_INNER_16:
        // Bump to NODE_FLAGS_INNER_48
        new_keys = calloc(1, 48 * sizeof(u8));
        if (!new_keys)
            die();
        new_children = calloc(1, 48 * sizeof(unsigned long));
        if (!new_children)
            die();

        memcpy(new_keys, node->key, 16 * sizeof(u8));
        memcpy(new_children, node->arr, 16 * sizeof(unsigned long));
        free(node->key);
        free(node->arr);
        node->key = new_keys;
        node->arr = new_children;
        node->flags = NODE_FLAGS_INNER_48;
        break;
    case NODE_FLAGS_INNER_48:
        // Bump to NODE_FLAGS_INNER_256
        new_keys = calloc(1, 256 * sizeof(u8));
        if (!new_keys)
            die();
        new_children = calloc(1, 256 * sizeof(unsigned long));
        if (!new_children)
            die();

        memcpy(new_keys, node->key, 48 * sizeof(u8));
        memcpy(new_children, node->arr, 48 * sizeof(unsigned long));
        free(node->key);
        free(node->arr);
        node->key = new_keys;
        node->arr = new_children;
        node->flags = NODE_FLAGS_INNER_256;
        break;
      default:
        die();
    }
}

/*
 * Insert a fully constructed leaf node into the tree rooted at _node.
 */
static void insert(struct radix_tree_node **_node, struct stream *stream,
                   struct radix_tree_node *leaf, int depth)
{
    struct radix_tree_node **next, *node = *_node;
    unsigned int match_len;
    u8 *key = stream->data;

    if (node == NULL) {
        replace(_node, leaf);
        return;
    }

    if (is_leaf(node)) {
        struct radix_tree_node *new_node = alloc_node(NODE_FLAGS_INNER_4);
        unsigned int min_len = min(node->key_len, (unsigned int)stream_size(stream));
        int i;

        u8 *key2 = load_key(node); 
        for (i = depth;
             i < min_len && key[i] == key2[i] &&
             (i - depth) < sizeof(new_node->prefix); i++) {
            new_node->prefix[i - depth] = key[i];
        }

        // If we've exhausted the stream then we've 100% matched
        // the leaf and don't need to do anything.
        if (i == stream_size(stream)) {
            free_node(new_node);
            return;
        }

        new_node->prefix_len = i - depth;
        depth = depth + new_node->prefix_len;
        add_child(new_node, key[depth], leaf);

        // Alternatively, if we've 100% matched the leaf node but
        // the stream still has more data, we can merge the two.
        if (i != node->key_len) {
            add_child(new_node, key2[depth], node);
        }
        replace(_node, new_node);
        return;
    }

    match_len = check_prefix(node, stream, depth);
    if (match_len != node->prefix_len) {
        // Prefix mismatch
        struct radix_tree_node *new_node = alloc_node(NODE_FLAGS_INNER_4);

        add_child(new_node, key[depth + match_len], leaf);
        add_child(new_node, node->prefix[match_len], node);
        new_node->prefix_len = match_len;
        assert(new_node->prefix_len <= sizeof(new_node->prefix));
        memcpy(new_node->prefix, node->prefix, match_len);
        node->prefix_len -= match_len + 1;
        assert(node->prefix_len >= 0);
        memmove(node->prefix, node->prefix + match_len + 1, node->prefix_len);
        replace(_node, new_node);
        return;
    }

    // assert ((depth + node->prefix_len) < stream_size(stream));
    depth += node->prefix_len;
    if (depth >= stream_size(stream)) {
        // All stream input consumed. We're done.
        return;
    }

    next = find_child(node, key[depth]);
    if (next && *next) {
        stream_advance(stream, 1);
        insert(next, stream, leaf, depth + 1);
    } else {
        // Add to inner node
        if (is_full(node))
            grow(node);
        add_child(node, key[depth], leaf);
    }
    /* Now we're at the final node and we need to bump the count. */
    // node->count += 1;
}

static void art_tree_insert(struct callstack_tree *tree,
                            struct callstack_entry *stack)
{
    struct art_priv *priv = tree->priv;
    struct stream *stream = &_stream;
    struct radix_tree_node *leaf;
    unsigned int n, key_len;

    /*
     * We don't need to build a cursor (unlike the linux backend) because
     * we don't need to do any manipuation of the callchain nodes. We simply
     * feed the bytes into the ART.
     */
    for (n = 0; n < MAX_STACK_ENTRIES; n++) {
        struct callstack_entry *entry = &stack[n];
        if (!entry->ip) {
            stream->end = (u8 *)entry;
            break;
        }
    }

    stream_init(stream, (u8 *)stack);

    // Unroll the stream?!?!!?
    leaf = alloc_node(NODE_FLAGS_LEAF);
    key_len = n * sizeof(struct callstack_entry);
    // leaf->key = calloc(1, key_len);
    // if (!leaf->key)
        // die();

    //memcpy(leaf->key, stream->data, key_len);
    leaf->key = stream->data;
    leaf->key_len = key_len;

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

    cs_tree = calloc(1, sizeof(*cs_tree));
    if (!cs_tree)
        die();

    priv = calloc(1, sizeof(*priv));
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
