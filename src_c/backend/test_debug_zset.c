#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "zset.h"
#include "dict.h"

// Externs or includes
// zset.h includes dict.h and avl.h

int main() {
    printf("DEBUG: Starting ZSet isolation test...\n");
    
    // 1. Create ZSet
    ZSet *z = zset_create();
    printf("DEBUG: ZSet created. z=%p, z->dict=%p\n", z, z->dict);
    fflush(stdout);
    
    // 2. Add "m1" 10
    double score = 10.0;
    int res = zset_add(z, "m1", score);
    printf("DEBUG: Add 'm1' score 10. Result: %d (Expected: 1)\n", res);
    if (res != 1) {
        printf("FAIL: First add returned %d\n", res);
        // Force check dict
        size_t d;
        char *v = dict_get(z->dict, "m1", 2, &d);
        if (v) printf("FAIL: dict_get found 'm1'!\n");
        else printf("FAIL: dict_get did NOT find 'm1', but zset_add returned 0?\n");
    }

    // 3. Add "m2" 20
    score = 20.0;
    res = zset_add(z, "m2", score);
    printf("DEBUG: Add 'm2' score 20. Result: %d (Expected: 1)\n", res);
    
    // 4. Add "m1" 15 (Update)
    score = 15.0;
    res = zset_add(z, "m1", score);
    printf("DEBUG: Add 'm1' score 15. Result: %d (Expected: 0)\n", res);
    
    // 5. Check Score
    double s;
    if (zset_score(z, "m1", &s)) {
        printf("DEBUG: Score of m1 is %f (Expected 15.0)\n", s);
    } else {
        printf("FAIL: m1 not found\n");
    }

    // 6. Check Range
    printf("DEBUG: Range...\n");
    // We can't verify range callback easily without mocks, but we can call it.
    // ...
    
    zset_free(z);
    return 0;
}
