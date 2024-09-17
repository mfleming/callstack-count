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
        memset(node->key, EMPTY, key_size);
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

static struct radix_tree_node **
find_child_16(struct radix_tree_node *node, art_key_t key)
{
#if 1
    for (int i = 0; i < node->key_len; i++) {
        if (node->key[i] == key)
            return (struct radix_tree_node **)&node->arr[i];
    }
#else
    __m128i node_key = _mm_loadu_si128((const __m128i *)node->key);
    __m128i __cmp = _mm_set1_epi8(key);
    // __m128i __input = _mm_set_epi8(
    __m128i __eq = _mm_cmpeq_epi8(node_key, __cmp);
    unsigned int mask = (1 << node->key_len) - 1;
    unsigned int bitfield = _mm_movemask_epi8(__eq) & mask;

    // mask is 32-bits but we're only interested in the lower 16-bits
    if (bitfield)
        return (struct radix_tree_node **)&node->arr[__builtin_ctz(bitfield)];
#endif

    return NULL;
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
            for (int i = 0; i < node->key_len; i++) {
                if (node->key[i] == key)
                    return (struct radix_tree_node **)&node->arr[i];
            }
        break;
    case NODE_FLAGS_INNER_16:
        struct radix_tree_node **r = find_child_16(node, key);
        if (r)
            return r;
        break;
    case NODE_FLAGS_INNER_48:
        index = node->key[key];
        if (index == EMPTY)
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
#if 1
    // Check to make sure key isn't already in this node
    switch (node->flags) {
    case NODE_FLAGS_INNER_4:
    case NODE_FLAGS_INNER_16:
        for (int i = 0; i < node->key_len; i++) {
            assert(node->key[i] != key);
        }
        break;
    case NODE_FLAGS_INNER_48:
        assert(node->key[key] == EMPTY);
        break;
    case NODE_FLAGS_INNER_256:
        assert(node->arr[key] == 0x0);
        break;
    }
 #endif

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

            if (index == EMPTY)
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
    //assert(stream_size(stream) > 0);
    if (stream_size <= 0)
      return;

    unsigned int stream_len = stream_size(stream) - depth;
    unsigned int key_len = node->key_len - depth;
    unsigned int prefix_sz = ARRAY_SIZE(node->prefix);

    art_key_t *key2 = load_key(node);
    unsigned int prefix_len;
    int match;

    for (match = depth;
        match < node->key_len && match < stream_size(stream) && key2[match] == stream->data[match];
        match++)
        ;

    if (match == stream_size(stream) && match == node->key_len) {
        if (!memcmp(&node->key[depth], &stream->data[depth], match)) {
            node->count++;
            return; // 100% match. Nothing to do.
        }
    }

    struct radix_tree_node *new_node = alloc_node(NODE_INITIAL_SIZE);

    prefix_len = match - depth;

    // Chain together multiple inner nodes for prefixes that don't
    // fit in a single node->prefix[] array.
    unsigned int prefix_remaining = prefix_len;
    int j = 0;
    while (prefix_remaining > prefix_sz) {
        memcpy(new_node->prefix, &stream->data[depth + j*prefix_sz], prefix_sz);
        new_node->prefix_len = prefix_sz;
        prefix_remaining -= prefix_sz;

        // If we will do another iteration, form a chain
        if (prefix_remaining > prefix_sz) {
            struct radix_tree_node *chain = alloc_node(NODE_INITIAL_SIZE);
            new_node->key[0] = stream->data[depth + j * prefix_sz + 1];
            new_node->key_len = 1;
            new_node->arr[0] = (unsigned long)chain;
            replace(_node, new_node);
            _node = &new_node;
            new_node = chain;
            prefix_remaining -= 1;
        }
        j++;
    }

    if (prefix_remaining) {
        memcpy(new_node->prefix, &stream->data[depth + prefix_len - prefix_remaining], prefix_remaining);
        new_node->prefix_len = prefix_remaining;
    }

    // Does the leaf have remaining bytes in the key?
    if (key_len - prefix_len > 0)
        add_child(new_node, node->key[depth + prefix_len], node);

    // Does the stream have remaining bytes in the key?
    if (stream_len - prefix_len > 0) {
        struct radix_tree_node *other_node = make_leaf(stream);
        add_child(new_node, other_node->key[depth + prefix_len], other_node);
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
            node->count++;
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

static bool
leaf_matches(struct radix_tree_node *node, struct stream *stream, int depth)
{
    if (node->key_len != stream_size(stream))
        return false;

    return memcmp(node->key, stream->data, node->key_len) == 0;
}

/*
 * Search in the tree rooted at node for key and return the node.
 */
struct radix_tree_node *
search(struct radix_tree_node *node, struct stream *stream, int depth)
{
    struct radix_tree_node **next;

    if (!node)
        return NULL;

    if (is_leaf(node)) {
        if (leaf_matches(node, stream, depth))
            return node;
        return NULL;
    }

    if (check_prefix(node, stream, depth) != node->prefix_len)
        return NULL;
        
    depth = depth + node->prefix_len;

    if (depth == stream_size(stream)) {
        // Matched on inner node.
        return node;
    }

    next = find_child(node, stream_get(stream, depth));
    if (!next)
        return NULL;

    return search(*next, stream, depth + 1);
}
