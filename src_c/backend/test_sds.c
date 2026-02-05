#include <stdio.h>
#include <string.h>
#include "sds.h"

int main() {
    printf("Testing SDS...\n");

    // Test sdsnew
    sds s = sdsnew("Hello");
    if (strcmp(s, "Hello") != 0) { printf("FAIL: sdsnew content\n"); return 1; }
    if (sdslen(s) != 5) { printf("FAIL: sdsnew len\n"); return 1; }
    
    // Test sdscat
    s = sdscat(s, ", World!");
    if (strcmp(s, "Hello, World!") != 0) { printf("FAIL: sdscat content\n"); return 1; }
    if (sdslen(s) != 13) { printf("FAIL: sdscat len\n"); return 1; }
    
    // Test sdsfree
    sdsfree(s);
    
    // Test sdsempty
    s = sdsempty();
    if (sdslen(s) != 0) { printf("FAIL: sdsempty len\n"); return 1; }
    if (sdsavail(s) != 0) { printf("FAIL: sdsempty avail\n"); return 1; }
    sdsfree(s);
    
    // Test binary safety
    s = sdsnewlen("A\0B", 3);
    if (sdslen(s) != 3) { printf("FAIL: binary len\n"); return 1; }
    if (memcmp(s, "A\0B", 3) != 0) { printf("FAIL: binary content\n"); return 1; }
    sdsfree(s);

    printf("PASS: All SDS tests passed.\n");
    return 0;
}
