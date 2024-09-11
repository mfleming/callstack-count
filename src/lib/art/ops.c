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

typedef u8 art_key_t;

struct stream {
    art_key_t *data;
    /* Pointer to the end of the data. See stream_end() */
    art_key_t *end;
    /* The current position into data, in 1-byte increments */
    unsigned int pos;
};

static struct stream _stream;

static inline void stream_init(struct stream *stream, art_key_t *data)
{
    stream->pos = 0;
    stream->data = data;
}

static inline bool stream_end(struct stream *stream)
{
    art_key_t *ptr = &stream->data[stream->pos];
    return ptr >= stream->end;
}

static inline __always_inline unsigned long stream_size(struct stream *stream)
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
static inline art_key_t stream_next(struct stream *stream)
{
    return stream->data[stream->pos++];
}

/*
 * Level of indirection for reading from the input stream. Note
 * that all accesses to a node's key go directly through ->key[].
 */
static inline art_key_t stream_get(struct stream *stream, unsigned int offset)
{
    #if 0
    // Read backwards because Little Endian
    int i = stream_size(stream) - offset - 1;
    if (i < 0)
        return stream->data[0]; // Empty

    assert(offset < stream_size(stream));
    return stream->data[i];
    #else
    return stream->data[offset];
    #endif
}

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

#define NODE_FLAGS_LEAF      (1 << 0)
#define NODE_FLAGS_INNER_4   (1 << 1)
#define NODE_FLAGS_INNER_16  (1 << 2)
#define NODE_FLAGS_INNER_48  (1 << 3)
#define NODE_FLAGS_INNER_256 (1 << 4)

#define NODE_INITIAL_SIZE   NODE_FLAGS_INNER_256

/*
 * A radix tree node.
 */
struct radix_tree_node {
    /* See NODE_FLAGS_* */
    unsigned int flags;

    /* This is necessary for expanding nodes when looking at leaves during insert() */
    unsigned int prefix_len;
    art_key_t prefix[32];

    /* How many IP-map pairs matched this path */
    unsigned long count;

    unsigned long key_len;
    art_key_t *key;

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

static inline __always_inline unsigned int node_size(unsigned int flags)
{
    switch (flags) {
    case NODE_FLAGS_LEAF:
        die();
    case NODE_FLAGS_INNER_4:
        return 4;
    case NODE_FLAGS_INNER_16:
        return 16;
    case NODE_FLAGS_INNER_48:
        return 48;
    case NODE_FLAGS_INNER_256:
        return 256;
    default:
        die();
    }
    return 0;
}

static struct radix_tree_node *alloc_node(unsigned int flags)
{
    struct radix_tree_node *node = NULL;
    unsigned int children_size;
    unsigned int obj_size;
    unsigned int key_size;

    if (flags & NODE_FLAGS_LEAF) {
        /*
         * Leaves are special and ->key and ->arr are set to the callstack_entry.
         * See art_tree_insert().
         */
        obj_size = sizeof(struct radix_tree_node);
        node = ccalloc(1, obj_size);
        goto out;
    }

    switch (flags) {
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
        key_size = node_size(flags) * sizeof(art_key_t);
        children_size = node_size(flags) * sizeof(unsigned long);
        obj_size = sizeof(struct radix_tree_node) + key_size + children_size;
        node = ccalloc(1, obj_size);
        node->key = (art_key_t *)((char *)node + sizeof(struct radix_tree_node));
        node->arr = (unsigned long *)((char *)node->key + key_size);
        break;
    case NODE_FLAGS_INNER_48:
        key_size = 256 * sizeof(art_key_t);
        children_size = node_size(flags) * sizeof(unsigned long);
        obj_size = sizeof(struct radix_tree_node) + key_size + children_size;
        node = calloc(1, obj_size);
        node->key = (art_key_t *)((char *)node + sizeof(struct radix_tree_node));
        node->arr = (unsigned long *)((char *)node->key + key_size);
        // Required in grow().
        memset(node->key, 0xff, key_size);
        break;
    case NODE_FLAGS_INNER_256:
        children_size = node_size(flags) * sizeof(unsigned long);
        obj_size = sizeof(struct radix_tree_node) + children_size;
        node = calloc(1, obj_size);
        node->arr = (unsigned long *)((char *)node + sizeof(struct radix_tree_node));
        break;
    default:
        die();
    }

out:
    node->flags = flags;
    return node;
}

static void free_node(struct radix_tree_node *node, bool leaf)
{
    switch (node->flags) {
    case NODE_FLAGS_LEAF:
        cfree(node->key, leaf);
        break;
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
    case NODE_FLAGS_INNER_48:
    case NODE_FLAGS_INNER_256:
        break;
    default:
        die();
    }

    cfree(node, leaf);
}

static inline __always_inline bool is_leaf(struct radix_tree_node *node)
{
    return node->flags & NODE_FLAGS_LEAF;
}

/*
 * Have all the keys of node been assigned?
 *
 * This function is used to decide when to reallocate node
 * with a larger size (number of children).
 */
static inline bool is_full(struct radix_tree_node *node)
{
    /*
     * Leaf nodes are always full and require callers to insert a new
     * node if expansion is needed.
     */
    if (is_leaf(node))
        return true;

    return node->key_len == node_size(node->flags);
}

/*
 * Given a node, lookup the node for key. If missing is missing return
 * the slot to insert a new node at.
 *
 * The lookup method varies based on the type of node.
 */
struct radix_tree_node **find_child(struct radix_tree_node *node, art_key_t key)
{
    unsigned int entries;

    if (!node)
        return NULL;

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
        entries = node_size(node->flags);
        for (int i = 0; i < entries; i++) {
            if (node->key[i] == key)
                return (struct radix_tree_node **)&node->arr[i];
        }
        break;
    case NODE_FLAGS_INNER_48:
        int index = node->key[key];
        if (index == 0xff)
            return NULL; // Unset

        return (struct radix_tree_node **)&node->arr[index];
    case NODE_FLAGS_INNER_256:
        return (struct radix_tree_node **)&node->arr[key];
    default:
        die();
    }

   return NULL;
}

static void replace(struct radix_tree_node **node, struct radix_tree_node *leaf)
{
    // TODO need to handle freeing memory?
    // struct radix_tree_node *n = *node;
    // if (n) {
    //     cfree(n->key);
    //     cfree(n);
    // }

    *node = leaf;
}

static art_key_t *load_key(struct radix_tree_node *node)
{
    return node->key;
}

static void add_child(struct radix_tree_node *node, art_key_t key,
                      struct radix_tree_node *child)
{
    assert(!is_full(node));

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
        node->key[node->key_len] = key;
        node->arr[node->key_len] = (unsigned long)child;
        node->key_len += 1;
        break;
    case NODE_FLAGS_INNER_48:
        node->key[key] = node->key_len;
        node->arr[node->key_len] = (unsigned long)child;
        node->key_len += 1;
        break;
    case NODE_FLAGS_INNER_256:
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

    unsigned int min_len = min(node->prefix_len, (unsigned int)stream_size(stream) - depth);
    int i;

    // Optimise for the common case where the prefix is the same
    if (!memcmp(&node->prefix, &stream->data[depth], min_len))
        return min_len;

    // OK, they're different. Count the number of matching bytes.
    for (i = 0; i < min_len; i++) {
        if (node->prefix[i] != stream_get(stream, depth + i))
            break;
    }
    return i;
}

/*
 * Return the new, larger node.
 */
static struct radix_tree_node *
grow(struct radix_tree_node **_node, struct radix_tree_node *node)
{
    struct radix_tree_node *new_node = NULL;
    unsigned int type = 0;
    unsigned int entries;

    assert(is_full(node));

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
        // Bump to NODE_FLAGS_INNER_16
        type = NODE_FLAGS_INNER_16;
        break;
    case NODE_FLAGS_INNER_16:
        // Bump to NODE_FLAGS_INNER_48
        type = NODE_FLAGS_INNER_48;
        break;
    case NODE_FLAGS_INNER_48:
        // Bump to NODE_FLAGS_INNER_256
        type = NODE_FLAGS_INNER_256;
        break;
    default:
        assert(false);
    }

    new_node = alloc_node(type);
    // Lookup the number of entries for the *old* node type
    entries = node_size(node->flags);
    switch(node->flags) {
    case NODE_FLAGS_INNER_4:
        memcpy(new_node->key, node->key, entries * sizeof(art_key_t));
        memcpy(new_node->arr, node->arr, entries * sizeof(unsigned long));
        break;
    case NODE_FLAGS_INNER_16:
        for (int i = 0; i < entries; i++) {
            int key = node->key[i];
            new_node->key[key] = i;
            new_node->arr[i] = node->arr[i];
        }
        break;
    case NODE_FLAGS_INNER_48:
        for (int i = 0; i < 256; i++) {
            int index = node->key[i];

            if (index == 0xff)
                continue; // Unset

            new_node->arr[i] = node->arr[index];
        }
        break;
    default:
        die();
    }

    new_node->key_len = entries;
    *_node = new_node;
    free_node(node, false);
    return new_node;
}

static inline struct radix_tree_node *make_leaf(struct stream *stream)
{
    struct radix_tree_node *leaf = alloc_node(NODE_FLAGS_LEAF);
    leaf->key_len = stream_size(stream);
    leaf->key = stream->data;
    return leaf;
}

static void do_leaf(struct radix_tree_node **_node, struct stream *stream,
                   struct radix_tree_node *leaf, int depth)
{
    struct radix_tree_node *node = *_node;

    assert(is_leaf(node));

    /*
     * Optimistically check for a match to avoid allocating a new inner node
     * uneccessarily. We want to avoid splitting the leaf node and inserting
     * new_node in front of it if we don't have to.
     *
     * Which we can do if the stream is a prefix of the leaf node.
     */
    unsigned int min_len = min(node->key_len, stream_size(stream));
    unsigned int prefix_sz = ARRAY_SIZE(node->prefix);
    art_key_t *key2 = load_key(node);
    int i;
    if (min_len == stream_size(stream) && !memcmp(&key2[depth], &stream->data[depth], min_len)) {
        // Match!
        return;
    }
    for (i = depth; i < min_len && stream_get(stream, i) == key2[i] && (i - depth) < prefix_sz; i++)
        ;

    if (i == stream_size(stream))
        return; /* Match! */

    struct radix_tree_node *new_node = alloc_node(NODE_INITIAL_SIZE);

    /* This can probably be a memcpy */
    for (i = depth;
         i < min_len && stream_get(stream, i) == key2[i] &&
         (i - depth) < prefix_sz; i++) {
            new_node->prefix[i - depth] = stream_get(stream, i);
    }

    // If we've exhausted the stream then we've 100% matched
    // the leaf and don't need to do anything.
    if (i == stream_size(stream)) {
        free_node(new_node, true);
        return;
    }

    new_node->prefix_len = i - depth;
    depth = depth + new_node->prefix_len;
    leaf = make_leaf(stream);
    add_child(new_node, stream_get(stream, depth), leaf);

    // Alternatively, if we've 100% matched the leaf node but
    // the stream still has more data, we can merge the two.
    if (i != node->key_len) {
        add_child(new_node, key2[depth], node);
    }
    replace(_node, new_node);
}
extern unsigned long __max_depth;

/*
 * Insert a fully constructed leaf node into the tree rooted at _node.
 */
static void insert(struct radix_tree_node **_node, struct stream *stream,
                   struct radix_tree_node *leaf, int depth)
{
    struct radix_tree_node **next, *node = *_node;
    unsigned int match_len;
    unsigned int __depth = 0;

    if (node == NULL) {
        leaf = make_leaf(stream);
        replace(_node, leaf);
        return;
    }

    while (1) {
        if (is_leaf(node)) {
            do_leaf(_node, stream, leaf, depth);
            return;
        }

        match_len = check_prefix(node, stream, depth);
        if (match_len != node->prefix_len) {
            // Prefix mismatch
            struct radix_tree_node *new_node = alloc_node(NODE_INITIAL_SIZE);

            leaf = make_leaf(stream);
            add_child(new_node, stream_get(stream, depth + match_len), leaf);
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

        next = find_child(node, stream_get(stream, depth));
        if (next && *next) {
            stream_advance(stream, 1);
            _node = next;
            node = *next;
            depth += 1;
            __depth++;
            if (__depth > __max_depth)
                __max_depth = __depth;
            // insert(next, stream, leaf, depth + 1);
        } else {
            // Add to inner node
            if (is_full(node)) {
                node = grow(_node, node);
            }
            leaf = make_leaf(stream);
            add_child(node, stream_get(stream, depth), leaf);
            return;
        }
    /* Now we're at the final node and we need to bump the count. */
    // node->count += 1;
    }
}

static void art_tree_insert(struct callstack_tree *tree,
                            struct callstack_entry *stack)
{
    struct art_priv *priv = tree->priv;
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
