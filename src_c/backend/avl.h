#ifndef AVL_H
#define AVL_H

#include <stddef.h>
#include "sds.h"

// AVL Tree Node
typedef struct AVLNode {
    sds key;             // Key (e.g., member name) - owned by node
    double score;        // Score for sorting
    int height;
    struct AVLNode *left;
    struct AVLNode *right;
} AVLNode;

typedef void (*avl_callback)(AVLNode *node, void *arg);

// AVL Tree Structure
typedef struct AVLTree {
    AVLNode *root;
    size_t count;
} AVLTree;

// API
AVLTree *avl_create();
void avl_free(AVLTree *tree);

// Insert or update. 
int avl_insert(AVLTree *tree, sds key, double score);

// Remove specific node identified by key AND score. 
int avl_delete(AVLTree *tree, sds key, double score);

// Find node by rank (0-indexed). Returns NULL if out of range.
AVLNode *avl_get_by_rank(AVLTree *tree, size_t rank);

void avl_traverse(AVLTree *tree, avl_callback cb, void *arg);


#endif
