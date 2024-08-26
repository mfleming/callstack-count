#ifndef __CALLSTACK_H__
#define __CALLSTACK_H__

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
     * Free all resources associated with the callstack ops.
     */
    void (*put)(struct callstack_tree *tree);
};

extern struct callstack_ops *cs_ops;

extern struct callstack_ops linux_ops;

extern void die(void);

#endif /* __CALLSTACK_H__ */
