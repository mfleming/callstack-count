#ifndef __CALLSTACK_H__
#define __CALLSTACK_H__

struct callstack_entry {
	unsigned long ip;
	unsigned long map;
};

struct callstack_tree {
    /* Insert a new stack into the tree */
    void (*insert)(struct callstack_entry *stack);
};

/**
 * Callstack interface
 * 
 * The interface is resposnsible for creating new trees and returning
 * existing ones.
 */
struct callstack {
    /* Return a new or existing tree for id */
    struct callstack_tree *(*get)(unsigned long id);
    void (*put)(struct callstack_tree *tree);
};

extern struct callstack *callstack;

extern void die(void);

#endif /* __CALLSTACK_H__ */