#include "avl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int max_int(int a, int b) { return a > b ? a : b; }

static int get_height(AVLNode *node) {
    return node ? node->height : 0;
}

static void update_height(AVLNode *node) {
    if (node) {
        node->height = 1 + max_int(get_height(node->left), get_height(node->right));
    }
}

static int get_balance(AVLNode *node) {
    return node ? get_height(node->left) - get_height(node->right) : 0;
}

static AVLNode *rotate_right(AVLNode *y) {
    AVLNode *x = y->left;
    AVLNode *T2 = x->right;

    x->right = y;
    y->left = T2;

    update_height(y);
    update_height(x);

    return x;
}

static AVLNode *rotate_left(AVLNode *x) {
    AVLNode *y = x->right;
    AVLNode *T2 = y->left;

    y->left = x;
    x->right = T2;

    update_height(x);
    update_height(y);

    return y;
}

// Compare two nodes: return <0 if A < B, >0 if A > B, 0 if equal (only if exact same key and score)
// Sort order: Score ASC, then Key Lexicographically ASC
static int compare_nodes(const char *keyA, double scoreA, const char *keyB, double scoreB) {
    if (scoreA < scoreB) return -1;
    if (scoreA > scoreB) return 1;
    return strcmp(keyA, keyB);
}

static AVLNode *avl_insert_node(AVLNode *node, const char *key, double score, int *inserted) {
    if (!node) {
        AVLNode *n = (AVLNode*)malloc(sizeof(AVLNode));
        n->key = _strdup(key);
        n->score = score;
        n->height = 1;
        n->left = NULL;
        n->right = NULL;
        *inserted = 1;
        return n;
    }

    int cmp = compare_nodes(key, score, node->key, node->score);
    if (cmp < 0) {
        node->left = avl_insert_node(node->left, key, score, inserted);
    } else if (cmp > 0) {
        node->right = avl_insert_node(node->right, key, score, inserted);
    } else {
        // Duplicate (same key AND score). 
        // In full ZSet, user might call ZADD with same score/member. 
        // Logic usually handles this before calling avl_insert, or we just do nothing.
        *inserted = 0;
        return node;
    }

    update_height(node);
    int balance = get_balance(node);

    // Left Left
    if (balance > 1 && compare_nodes(key, score, node->left->key, node->left->score) < 0)
        return rotate_right(node);

    // Right Right
    if (balance < -1 && compare_nodes(key, score, node->right->key, node->right->score) > 0)
        return rotate_left(node);

    // Left Right
    if (balance > 1 && compare_nodes(key, score, node->left->key, node->left->score) > 0) {
        node->left = rotate_left(node->left);
        return rotate_right(node);
    }

    // Right Left
    if (balance < -1 && compare_nodes(key, score, node->right->key, node->right->score) < 0) {
        node->right = rotate_right(node->right);
        return rotate_left(node);
    }

    return node;
}

static AVLNode *min_value_node(AVLNode *node) {
    AVLNode *current = node;
    while (current->left != NULL) current = current->left;
    return current;
}

static AVLNode *avl_delete_node(AVLNode *root, const char *key, double score, int *deleted) {
    if (!root) return NULL;

    int cmp = compare_nodes(key, score, root->key, root->score);
    if (cmp < 0) {
        root->left = avl_delete_node(root->left, key, score, deleted);
    } else if (cmp > 0) {
        root->right = avl_delete_node(root->right, key, score, deleted);
    } else {
        // Found
        *deleted = 1;
        if (!root->left || !root->right) {
            AVLNode *temp = root->left ? root->left : root->right;
            if (!temp) {
                temp = root;
                root = NULL;
            } else {
                *root = *temp; // Copy contents
                // wait, careful with pointers if we do this struct copy. 
                // Better to just pointer swap.
                // Standard BST delete logic:
            }
            // If strictly pointer manipulation:
            if (temp == root) { // No children case
                free(temp->key);
                free(temp);
                root = NULL; // already set above, but clarifying
            } else {
                // One child case, we copied content to root.
                // We must free the temp node which is now structurally detached?
                // Actually the *root = *temp copy is tricky with allocated members like key.
                // It copies the pointer. So now root->key points to what temp->key pointed.
                // We should NOT free temp->key because root owns it now.
                // We DO free temp structure.
                free(temp); 
            }
        } else {
            // Two children
            AVLNode *temp = min_value_node(root->right);
            // Copy successor data to this node
            free(root->key); // Free old key
            root->key = _strdup(temp->key);
            root->score = temp->score;
            // Delete successor
            root->right = avl_delete_node(root->right, temp->key, temp->score, deleted);
        }
    }

    if (!root) return NULL;

    update_height(root);
    int balance = get_balance(root);

    // Balance checks
    if (balance > 1 && get_balance(root->left) >= 0)
        return rotate_right(root);

    if (balance > 1 && get_balance(root->left) < 0) {
        root->left = rotate_left(root->left);
        return rotate_right(root);
    }

    if (balance < -1 && get_balance(root->right) <= 0)
        return rotate_left(root);

    if (balance < -1 && get_balance(root->right) > 0) {
        root->right = rotate_right(root->right);
        return rotate_left(root);
    }

    return root;
}

AVLTree *avl_create() {
    AVLTree *t = (AVLTree*)malloc(sizeof(AVLTree));
    t->root = NULL;
    t->count = 0;
    return t;
}

static void free_nodes(AVLNode *node) {
    if (!node) return;
    free_nodes(node->left);
    free_nodes(node->right);
    free(node->key);
    free(node);
}

void avl_free(AVLTree *tree) {
    if (!tree) return;
    free_nodes(tree->root);
    free(tree);
}

int avl_insert(AVLTree *tree, const char *key, double score) {
    int inserted = 0;
    tree->root = avl_insert_node(tree->root, key, score, &inserted);
    if (inserted) tree->count++;
    return inserted;
}

int avl_delete(AVLTree *tree, const char *key, double score) {
    int deleted = 0;
    tree->root = avl_delete_node(tree->root, key, score, &deleted);
    if (deleted) tree->count--;
    return deleted;
}

static void traverse_in_order(AVLNode *node, avl_callback cb, void *arg) {
    if (!node) return;
    traverse_in_order(node->left, cb, arg);
    cb(node, arg);
    traverse_in_order(node->right, cb, arg);
}

void avl_traverse(AVLTree *tree, avl_callback cb, void *arg) {
    traverse_in_order(tree->root, cb, arg);
}

// Helper for get by rank
static AVLNode *get_rank_rec(AVLNode *node, size_t rank, size_t *curr) {
    if (!node) return NULL;
    
    // In-order: Left, Self, Right
    AVLNode *res = get_rank_rec(node->left, rank, curr);
    if (res) return res;

    if (*curr == rank) return node;
    (*curr)++;

    return get_rank_rec(node->right, rank, curr);
}

AVLNode *avl_get_by_rank(AVLTree *tree, size_t rank) {
    if (rank >= tree->count) return NULL;
    size_t curr = 0;
    return get_rank_rec(tree->root, rank, &curr);
}
