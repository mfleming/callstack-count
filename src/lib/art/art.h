#ifndef __ART_H__
#define __ART_H__

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <xmmintrin.h>

typedef unsigned char art_key_t;

struct stream {
    art_key_t *data;
    /* Pointer to the end of the data. See stream_end() */
    art_key_t *end;
    /* The current position into data, in 1-byte increments */
    unsigned int pos;
};

#define NODE_FLAGS_LEAF      (1 << 0)
#define NODE_FLAGS_INNER_4   (1 << 1)
#define NODE_FLAGS_INNER_16  (1 << 2)
#define NODE_FLAGS_INNER_48  (1 << 3)
#define NODE_FLAGS_INNER_256 (1 << 4)

#define NODE_INITIAL_SIZE   NODE_FLAGS_INNER_4

#define EMPTY 0xff

/*
 * A radix tree node.
 */
struct radix_tree_node {
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

    /* See NODE_FLAGS_* */
    unsigned int flags;

    /* How many IP-map pairs matched this path */
    unsigned long count;

    unsigned int prefix_len;
    art_key_t prefix[128];
};

#ifndef die
#define die() exit(1)
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define cfree(ptr, flag) free(ptr)
#define ccalloc(num, size) calloc(num, size)

/*
 * API
 */
void insert(struct radix_tree_node **_node, struct stream *stream,
                   struct radix_tree_node *leaf, int depth);
#endif /* __ART_H__ */
