#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "avl.h"

// internal struct definition for testing height/structure
// This duplicates part of avl.c/avl.h (if struct was hidden). 
// Since struct is in avl.h, we can access generic fields.

void test_insert_search() {
    printf("Test Insert & Search...\n");
    AVLTree *t = avl_create();
    
    avl_insert(t, "A", 10);
    avl_insert(t, "B", 20);
    avl_insert(t, "C", 5);
    
    assert(t->count == 3);
    
    // Check rank
    AVLNode *n0 = avl_get_by_rank(t, 0); // "C", 5
    assert(n0 != NULL);
    assert(strcmp(n0->key, "C") == 0);
    assert(n0->score == 5);
    
    AVLNode *n1 = avl_get_by_rank(t, 1); // "A", 10
    assert(n1 != NULL);
    assert(strcmp(n1->key, "A") == 0);
    
    AVLNode *n2 = avl_get_by_rank(t, 2); // "B", 20
    assert(n2 != NULL);
    assert(strcmp(n2->key, "B") == 0);
    
    avl_free(t);
    printf("  Passed.\n");
}

void test_insert_duplicate() {
    printf("Test Duplicate Insert...\n");
    AVLTree *t = avl_create();
    avl_insert(t, "A", 10);
    int res = avl_insert(t, "A", 10); // Should fail/return 0
    assert(res == 0);
    assert(t->count == 1);
    
    // Different score, same key -> Allowed logic-wise in AVL, but ZSET logic prevents it.
    // My AVL is dumb, it treats ("A", 10) and ("A", 11) as different nodes.
    res = avl_insert(t, "A", 11); 
    assert(res == 1);
    assert(t->count == 2);
    
    avl_free(t);
    printf("  Passed.\n");
}

void test_delete() {
    printf("Test Delete...\n");
    AVLTree *t = avl_create();
    avl_insert(t, "A", 10);
    avl_insert(t, "B", 20);
    avl_insert(t, "C", 5);
    
    int DelRes = avl_delete(t, "A", 10);
    assert(DelRes == 1);
    assert(t->count == 2);
    
    AVLNode *n0 = avl_get_by_rank(t, 0); // "C", 5
    assert(strcmp(n0->key, "C") == 0);
    
    AVLNode *n1 = avl_get_by_rank(t, 1); // "B", 20
    assert(strcmp(n1->key, "B") == 0);
    
    // Delete non-existent
    assert(avl_delete(t, "A", 10) == 0);
    
    avl_free(t);
    printf("  Passed.\n");
}

void check_balance(AVLNode *node) {
    if (!node) return;
    int lh = node->left ? node->left->height : 0;
    int rh = node->right ? node->right->height : 0;
    int diff = lh - rh;
    if (diff < -1 || diff > 1) {
        printf("Balance violation at %s: %d (L%d R%d)\n", node->key, diff, lh, rh);
        assert(0);
    }
    check_balance(node->left);
    check_balance(node->right);
}

void test_balance_large() {
    printf("Test Balance (Large)...\n");
    AVLTree *t = avl_create();
    // Insert sorted to force rebalancing
    for (int i = 0; i < 100; i++) {
        char buf[16];
        sprintf(buf, "k%d", i);
        avl_insert(t, buf, (double)i);
    }
    assert(t->count == 100);
    check_balance(t->root);
    
    // Delete some
    for (int i = 0; i < 50; i++) {
        char buf[16];
        sprintf(buf, "k%d", i);
        avl_delete(t, buf, (double)i);
    }
    assert(t->count == 50);
    check_balance(t->root);
    
    avl_free(t);
    printf("  Passed.\n");
}

int main() {
    test_insert_search();
    test_insert_duplicate();
    test_delete();
    test_balance_large();
    printf("ALL AVL TESTS PASSED.\n");
    return 0;
}
