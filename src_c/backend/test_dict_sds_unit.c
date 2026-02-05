#include "dict.h"
#include "sds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h> // For Sleep

void test_basic_ops() {
    printf("Testing Basic Ops...\n");
    Dict d;
    dict_init(&d, 4);

    sds k1 = sdsnew("key1");
    sds v1 = sdsnew("val1");
    dict_put(&d, k1, v1, 0);
    sdsfree(k1); sdsfree(v1); // dict_put duplicates

    sds lookup_k1 = sdsnew("key1");
    sds val = dict_get(&d, lookup_k1);
    assert(val != NULL);
    assert(strcmp(val, "val1") == 0);
    printf("  Retrieval OK\n");

    // Overwrite
    sds v2 = sdsnew("value2");
    dict_put(&d, lookup_k1, v2, 0);
    sdsfree(v2);

    val = dict_get(&d, lookup_k1);
    assert(strcmp(val, "value2") == 0);
    printf("  Overwrite OK\n");
    
    sdsfree(lookup_k1);
    dict_destroy(&d);
    printf("Basic Ops Passed.\n");
}

void test_expiration() {
    printf("Testing Expiration...\n");
    Dict d;
    dict_init(&d, 4);
    
    sds k = sdsnew("ex_key");
    sds v = sdsnew("ex_val");
    dict_put(&d, k, v, 1); // 1 sec TTL
    sdsfree(k); sdsfree(v);
    
    sds lookup = sdsnew("ex_key");
    assert(dict_get(&d, lookup) != NULL);
    
    printf("  Waiting for expiration (1.5s)...\n");
    Sleep(1500); 
    
    assert(dict_get(&d, lookup) == NULL); // Should be gone
    printf("  Expired Key OK\n");
    
    sdsfree(lookup);
    dict_destroy(&d);
    printf("Expiration Passed.\n");
}

void test_rehashing() {
    printf("Testing Rehashing...\n");
    Dict d;
    dict_init(&d, 4);
    
    // Add 100 items to trigger expansion
    char buf[32];
    for (int i = 0; i < 100; ++i) {
        snprintf(buf, sizeof(buf), "key_%d", i);
        sds k = sdsnew(buf);
        snprintf(buf, sizeof(buf), "val_%d", i);
        sds v = sdsnew(buf);
        dict_put(&d, k, v, 0);
        sdsfree(k); sdsfree(v);
    }
    
    printf("  Added 100 items. Checking retrieval...\n");
    
    for (int i = 0; i < 100; ++i) {
        snprintf(buf, sizeof(buf), "key_%d", i);
        sds k = sdsnew(buf);
        sds val = dict_get(&d, k);
        if (!val) {
             printf("Failed to find key_%d\n", i);
             assert(0);
        }
        snprintf(buf, sizeof(buf), "val_%d", i);
        assert(strcmp(val, buf) == 0);
        sdsfree(k);
    }
    
    printf("Rehashing Passed.\n");
    dict_destroy(&d);
}

int main() {
    test_basic_ops();
    test_expiration();
    test_rehashing();
    printf("All Dict SDS Tests Passed!\n");
    return 0;
}
