#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../query_processor/sql_parser.h"
#include "table_formatter.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

int main() {
    char line[1024];
    printf("RedisSQL CLI\n");
    printf("Type 'exit' to quit.\n");

    while (1) {
        printf("sql> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;
        
        if (strcasecmp(line, "exit") == 0) break;
        if (strlen(line) == 0) continue;

        SQLResult res = process_sql(line);
        print_table(res);
        free_sql_result(res);
    }

    return 0;
}
