#ifndef AVL_H
#define AVL_H

#include <stddef.h>

// AVL Tree Node
typedef struct AVLNode {
    char *key;           // Key (e.g., member name) - owned by node
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
// Note: Caller must ensure old node is removed if updating score w/o key change? 
// No, standard AVL insert doesn't know about "updates" if sorted by score. 
// It effectively inserts a new node.
// To move a node (change score), we must Delete then Insert.
// Returns 1 on success.
int avl_insert(AVLTree *tree, const char *key, double score);

// Remove specific node identified by key AND score. 
// Essential for O(log N) deletion when backed by a Dict.
int avl_delete(AVLTree *tree, const char *key, double score);

// Find node by rank (0-indexed). Returns NULL if out of range.
AVLNode *avl_get_by_rank(AVLTree *tree, size_t rank);

void avl_traverse(AVLTree *tree, avl_callback cb, void *arg);


#endif
