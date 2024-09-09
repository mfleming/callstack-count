#ifndef __CALLSTACK_H__
#define __CALLSTACK_H__

#include <stdlib.h>

struct callstack_entry {
	unsigned long ip;
	unsigned long map;
};

struct callstack_tree {
    /*
     * Insert a new stack into the tree
     *
     * An implementation is expected to iterate over the stack and
     * insert each entry into the tree.
     */
    void (*insert)(struct callstack_tree *tree, struct callstack_entry *stack);

    /*
     * A backend-specific private data pointer to store any object needed
     * for the backend to operate.
     */
    void *priv;
};

struct stats {
    /* How many records were successfully processed? */
    unsigned long num_records;

    /* How many unique trees were created? */
    unsigned long num_trees;

    /* Average number of 100% matches in a tree */
    double avg_full_matches;
};

/**
 * Callstack operations
 * 
 * The interface is responsible for creating new trees and returning
 * existing ones.
 */
struct callstack_ops {
    /*
     * Return a new or existing tree for id.
     *
     * Callstacks are identified by an 'id' which is provided by the
     * result data. The first time a callstack is seen, a new tree is
     * created and returned. Subsequent calls with the same id will
     * return the same tree.
     */
    struct callstack_tree *(*get)(unsigned long id);

    /*
     * Allocate a new callstack_tree and fill out any backend-specific
     * data required in the ->priv field.
     */
    struct callstack_tree *(*new)(void);

    /*
     * Free all resources associated with the callstack ops.
     */
    void (*put)(struct callstack_tree *tree);

    /*
     * Return statistics about the trees.
     */
    void (*stats)(struct callstack_tree *tree, struct stats *stats);
};

extern struct callstack_ops *cs_ops;
extern struct callstack_ops linux_ops;
extern struct callstack_ops art_ops;

extern void __die(const char *func_name, int lineno);
#define die() __die(__func__, __LINE__)

extern void *ccalloc(size_t nmemb, size_t size);
extern unsigned long num_allocs;

// TODO - de-dup cursor building code
extern struct map_symbol *get_map(unsigned long map);

#endif /* __CALLSTACK_H__ */
