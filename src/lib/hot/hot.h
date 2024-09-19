#ifndef __HOT_H__
#define __HOT_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef uint8_t hot_key_t;

struct stream {
	hot_key_t *start;
	hot_key_t *end;	// One past the end
};

#define SINGLE_MASK_PKEYS_8_BIT	   (1<<0)
#define SINGLE_MASK_PKEYS_16_BIT   (1<<1)
#define SINGLE_MASK_PKEYS_32_BIT   (1<<2)
#define MULTI_MASK_8_PKEYS_8_BIT   (1<<3)
#define MULTI_MASK_8_PKEYS_16_BIT  (1<<4)
#define MULTI_MASK_8_PKEYS_32_BIT  (1<<5)
#define MULTI_MASK_16_PKEYS_16_BIT (1<<6)
#define MULTI_MASK_16_PKEYS_32_BIT (1<<7)
#define MULTI_MASK_32_PKEYS_32_BIT (1<<8)

struct node {
	unsigned short flags;
};

/*
 * Single mask node
 */
struct smask_node {
};

/*
 * Multi-mask node with 8 8-bit masks
 */
struct mmask8_node {
};

/*
 * Multi-mask node with 16 8-bit masks
 */
struct mmask16_node {
};

static inline void panic()
{
	fprintf(stderr, "Panic\n");
	exit(EXIT_FAILURE);
}

#endif /* __HOT_H__ */
