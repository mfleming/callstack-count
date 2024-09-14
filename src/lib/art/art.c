/*
 * Implementation of Adaptive Radix Trees.
 *
 * Note that node removal is not implemented because perf does not require it.
 *
 * Lastly, since the whole purpose of storing callstacks in the ART is to count
 * the number of times a callstack is seen from samples, we store a count in
 * each node.
 */
#include "art.h"

typedef enum memory_order
{
  memory_order_relaxed,
  memory_order_consume,
  memory_order_acquire,
  memory_order_release,
  memory_order_acq_rel,
  memory_order_seq_cst
} memory_order;

typedef _Atomic(_Bool) atomic_bool;
typedef struct atomic_flag { atomic_bool _Value; } atomic_flag;

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


struct art_priv {
    /* Root of the ART */
    struct radix_tree_node *root;
};

static inline unsigned int node_size(unsigned int flags)
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

static inline bool is_leaf(struct radix_tree_node *node)
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
    int index;

    if (!node)
        return NULL;

    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
        if (key == 0) {
            for (int i = 0; i < node->key_len; i++) {
                if (node->key[i] == key)
                    return (struct radix_tree_node **)&node->arr[i];
            }
        } else {
            unsigned int pattern = (key << 3) | (key << 2) | (key << 1) | key;
            unsigned int input = *(unsigned int *)node->key ^ pattern;
            unsigned tmp = (input & 0x7f7f7f7f) + 0x7f7f7f7f;
            tmp = ~(tmp | input | 0x7f7f7f7f);
            int index = __builtin_clz(tmp) >> 3;
            return (struct radix_tree_node **)&node->arr[index];
        }
        break;
    case NODE_FLAGS_INNER_16:
        for (int i = 0; i < node->key_len; i++) {
            if (node->key[i] == key)
                return (struct radix_tree_node **)&node->arr[i];
        }
        break;
    case NODE_FLAGS_INNER_48:
        index = node->key[key];
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
    printf("key_len=%u\n", node->key_len);
    unsigned int min_len = min(node->key_len, stream_size(stream));
    unsigned int prefix_sz = ARRAY_SIZE(node->prefix);
    art_key_t *key2 = load_key(node);
    int i;
    if (min_len == stream_size(stream) && !memcmp(&key2[depth], &stream->data[depth], min_len)) {
        printf("Match stream_size=%u?\n", stream_size(stream));
        printf("[0] == [0], %c == %c\n", key2[depth], stream->data[depth]);
        printf("[1] == [1], %c == %c\n", key2[depth + 1], stream->data[depth + 1]);
        printf("%p == %p\n", key2, stream->data);
        // Match!
        return;
    }
    for (i = depth; i < min_len && stream_get(stream, i) == key2[i] && (i - depth) < prefix_sz; i++)
        ;

    if (i == stream_size(stream))
        return; /* Match! */

    struct radix_tree_node *new_node = alloc_node(NODE_INITIAL_SIZE);

    printf("Not match: %d\n", i - depth);
    /* This can probably be a memcpy */
    for (i = depth;
         i < min_len && stream_get(stream, i) == key2[i] &&
         (i - depth) < prefix_sz; i++) {
            new_node->prefix[i - depth] = stream_get(stream, i);
    }

    // At this point we know the leaf and node do not match exactly but do have
    // a prefix match. So far, we've only checked AT MOST prefix_len keys of the
    // prefix. If we have more comparisons left to do insert an inner node with
    // the new prefix and continue the matching.
    
    // if ((i - depth) == prefix_sz) {
    //     // We filled the prefix for the new inner node but we have more
    //     // comparisons to do. Insert the new node and repeat the leaf
    //     // handling code.
    // }

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
/*
 * Insert a fully constructed leaf node into the tree rooted at _node.
 */
void insert(struct radix_tree_node **_node, struct stream *stream,
                   struct radix_tree_node *leaf, int depth)
{
    struct radix_tree_node **next, *node = *_node;
    unsigned int match_len;
    unsigned int __depth = 0;

    // gdb -p (pid) ; step
    // kill(getpid(), SIGSTOP);

    if (node == NULL) {
        printf("Replace\n");
        leaf = make_leaf(stream);
        replace(_node, leaf);
        return;
    }

    while (1) {
        printf("Not replace\n");
        if (is_leaf(node)) {
            printf("Leaf\n");
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