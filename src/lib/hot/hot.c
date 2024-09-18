/*
 * An implementation of Height Optimized Tries
 *
 * See HOT: A Height Optimized Trie Index for Main-Memory Database Systems
 */
#include "hot.h"

static inline uint8_t node_type(struct node *node)
{
	return node->flags;
}

static uint32_t retrieve_result_candidate(struct node *node, hot_key_t *key) {
	switch (node_type(node)) {
	case SINGLE_MASK_PKEYS_8_BIT:
		break;
	case MULTI_MASK_8_PKEYS_8_BIT:
		break;
	default:
		panic();
	}
	return -1;
}

static void hot_insert(struct node *node, struct stream *stream)
{
}
