#include <linux/rbtree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map_symbol.h"

#include "callstack.h"
#include "data/data.h"

struct record records[] = {
#include "gen2.d"
};

struct record simple_records[] = {
    {0x1111,
        {
                {0xffff2222, 0x333},
                {0x0, 0x0},
        }},
    {0x1111,
        {
                {0xffff2222, 0x333},
                {0xffff3333, 0x444},
                {0x0, 0x0},
        }}
};

void _insert(struct callstack_tree *tree, struct callstack_entry *stack)
{
    struct callstack_entry *entry;
    for (int i = 0; i < MAX_STACK_ENTRIES; i++) {
        entry = &stack[i];
        if (!entry->ip) {
            break;
        }
        printf("ip: 0x%016lx, map: 0x%016lx\n", entry->ip, entry->map);
    }
}

struct callstack_tree *callstack_get(unsigned long id)
{
    struct callstack_tree *t = calloc(1, sizeof(struct callstack_tree));
    if (!t) {
        die();
    }

    t->insert = _insert;

    return t;
}

void callstack_put(struct callstack_tree *tree)
{
    free(tree);
}

struct callstack_ops cs = {
    .get = callstack_get,
    .put = callstack_put,
};

// struct callstack_ops *cs_ops = &cs;
// struct callstack_ops *cs_ops = &linux_ops;
struct callstack_ops *cs_ops = &art_ops;

struct map_tree {
    struct rb_node node;
    unsigned long id;
    struct map_symbol *ms;
};

struct map_trees {
    struct rb_root_cached node;
};
static struct map_trees map_trees;

static struct map_symbol *last_ms = NULL;

struct map_symbol *get_map(unsigned long id) {
    struct rb_node **p = &map_trees.node.rb_root.rb_node;
    struct rb_node *parent = NULL;
    struct map_tree *mt;
    bool leftmost = true;
    struct map_symbol *ms = NULL;

    if (last_ms && last_ms->map == (void *)id) {
        return last_ms;
    }

    while (*p != NULL) {
        parent = *p;
        mt = rb_entry(parent, struct map_tree, node);
        int cmp = mt->id - id;
        if (cmp < 0)
            p = &(*p)->rb_left;
        else if (cmp > 0) {
            p = &(*p)->rb_right;
            leftmost = false;
        } else {
            ms = mt->ms;
            goto out;
        }
    }

    ms = calloc(1, sizeof(struct map_symbol));
    if (!ms) {
        die();
    }
    // Append the IP to the callchain
    ms->map = (void *)id;

    mt = calloc(1, sizeof(struct map_tree));
    if (!mt) {
        die();
    }

    mt->id = id;
    mt->ms = ms;
    rb_link_node(&mt->node, parent, p);
    rb_insert_color_cached(&mt->node, &map_trees.node, leftmost);

out:
    last_ms = ms;
    return ms;
}

struct tree {
    unsigned long id;
    struct callstack_tree *cs_tree;
    struct rb_node node;
};

struct trees {
    struct rb_root_cached entries;
};

struct trees trees;

/* Initialise various caches */
static inline void init_caches() {
    trees.entries = RB_ROOT_CACHED;
    map_trees.node = RB_ROOT_CACHED;
}

static struct callstack_tree *get_tree(unsigned long id) {
    struct tree *cursor;
    struct callstack_tree *t = NULL;
    struct rb_node **p = &trees.entries.rb_root.rb_node;
    struct rb_node *parent = NULL;
    struct tree *tree;
    bool leftmost = true;

    while (*p != NULL) {
        parent = *p;
        tree = rb_entry(parent, struct tree, node);

        if (tree->id < id)
            p = &(*p)->rb_left;
        else if (tree->id > id) {
            p = &(*p)->rb_right;
            leftmost = false;
        } else
            return tree->cs_tree;
    }

    t = cs_ops->new();

    cursor = calloc(1, sizeof(*cursor));
    if (!cursor)
        die();

    cursor->cs_tree = t;
    cursor->id = id;
    rb_link_node(&cursor->node, parent, p);
    rb_insert_color_cached(&cursor->node, &trees.entries, leftmost);
    return cursor->cs_tree;
}

unsigned long __max_depth = 0;
int main(int argc, char *argv[])
{
    struct stats stats = {0};
    struct record *r = records;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <linux|art>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (!strcmp(argv[1], "linux")) {
        cs_ops = &linux_ops;
    } else if (!strcmp(argv[1], "art")) {
        cs_ops = &art_ops;
    } else if (!strcmp(argv[1], "hash")) {
        cs_ops = &hash_ops;
    } else {
        fprintf(stderr, "Invalid argument: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    init_caches();

    // Main loop
    for (int j = 0; j < 20; j++) {
        for (int i = 0; i < ARRAY_SIZE(records); i++) {
            r = &records[i];

            struct callstack_tree *tree = get_tree(r->id);
            tree->insert(tree, r->stack);
            stats.num_records += 1;
        }
    }

    // Walk the rbtree and count the number of entries
    struct rb_root *root = &trees.entries.rb_root;
    struct rb_node *tree_node = rb_first(root);
    struct tree *tree;

    while (tree_node) {
        tree = rb_entry(tree_node, struct tree, node);
        cs_ops->stats(tree->cs_tree, &stats);
        stats.num_trees++;
        tree_node = rb_next(tree_node);
    }

    struct rb_root *map_root = &map_trees.node.rb_root;
    struct rb_node *map_node = rb_first(map_root);
    // struct map_tree *map_tree;

    unsigned long num_maps = 0;
    while (map_node) {
        // map_tree = rb_entry(map_node, struct map_tree, node);
        // printf("map: 0x%016lx\n", map_tree->id);
        num_maps++;
        map_node = rb_next(map_node);
    }

    printf("Processed %lu records\n", stats.num_records);
    printf("Created %lu trees\n", stats.num_trees);
    printf("Average 100%% matches: %0.2f%%\n", stats.avg_full_matches);
    printf("Number of maps: %lu\n", num_maps);
    printf("Number of allocations: %lu\n", num_allocs);
    printf("Number of free:        %lu\n", num_frees);
    printf("Number of LEAF frees:  %lu\n", leaf_frees);
    printf("Max tree depth: %lu\n", __max_depth);

    return 0;
}
