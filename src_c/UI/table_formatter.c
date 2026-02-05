#include "table_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_table(SQLResult result) {
    if (result.error) {
        printf("Error: %s\n", result.error);
        return;
    }

    if (result.num_cols == 0) {
        printf("Empty result set.\n");
        return;
    }

    // Calculate column widths
    int *widths = malloc(sizeof(int) * result.num_cols);
    for (int i = 0; i < result.num_cols; i++) {
        widths[i] = (int)strlen(result.headers[i]);
        for (int j = 0; j < result.num_rows; j++) {
            int len = (int)strlen(result.rows[j][i]);
            if (len > widths[i]) widths[i] = len;
        }
        widths[i] += 2; // Padding
    }

    // Print header
    printf("+");
    for (int i = 0; i < result.num_cols; i++) {
        for (int j = 0; j < widths[i]; j++) printf("-");
        printf("+");
    }
    printf("\n|");
    for (int i = 0; i < result.num_cols; i++) {
        printf(" %-*s |", widths[i] - 1, result.headers[i]);
    }
    printf("\n+");
    for (int i = 0; i < result.num_cols; i++) {
        for (int j = 0; j < widths[i]; j++) printf("-");
        printf("+");
    }
    printf("\n");

    // Print rows
    for (int i = 0; i < result.num_rows; i++) {
        printf("|");
        for (int j = 0; j < result.num_cols; j++) {
            printf(" %-*s |", widths[j] - 1, result.rows[i][j]);
        }
        printf("\n");
    }

    // Print footer
    printf("+");
    for (int i = 0; i < result.num_cols; i++) {
        for (int j = 0; j < widths[i]; j++) printf("-");
        printf("+");
    }
    printf("\n%d row(s) in set\n", result.num_rows);

    free(widths);
}
