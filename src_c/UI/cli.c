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

#include "../backend/sds.h"

static void execute_redis_cmd(SQLResult *res) {
    if (!res->redis_cmd) return;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(6379);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        if (res->error) free(res->error);
        res->error = strdup("Could not connect to Redis server on 127.0.0.1:6379");
        goto cleanup;
    }

    // Convert redis_cmd to RESP
    int argc;
    sds *argv = sdssplitargs(res->redis_cmd, &argc);
    sds resp = sdsempty();
    char tmp[32];
    sprintf(tmp, "*%d\r\n", argc);
    resp = sdscat(resp, tmp);
    for (int i = 0; i < argc; i++) {
        sprintf(tmp, "$%zu\r\n", sdslen(argv[i]));
        resp = sdscat(resp, tmp);
        resp = sdscatlen(resp, argv[i], sdslen(argv[i]));
        resp = sdscat(resp, "\r\n");
    }
    send(sock, resp, (int)sdslen(resp), 0);
    sdsfree(resp);
    sdsfreesplitres(argv, argc);

    // Read response
    char buf[4096];
    int n = recv(sock, buf, sizeof(buf)-1, 0);
    if (n > 0) {
        buf[n] = 0;
        
        // If it was just a PING (placeholder), don't overwrite our local results
        if (strcmp(res->redis_cmd, "PING") == 0) goto cleanup;

        // Simple RESP parsing for the table
        if (buf[0] == '+') {
            if (res->num_rows > 0 && res->num_cols > 0) {
                free(res->rows[0][0]);
                res->rows[0][0] = strdup(buf + 1);
                res->rows[0][0][strcspn(res->rows[0][0], "\r\n")] = 0;
            }
        } else if (buf[0] == '$') {
            // Bulk String
            char *p = strstr(buf, "\r\n");
            if (p) {
                p += 2;
                char *end = strstr(p, "\r\n");
                if (end) *end = 0;
                if (res->num_rows > 0 && res->num_cols > 1) {
                    free(res->rows[0][1]);
                    res->rows[0][1] = strdup(p);
                }
            }
        } else if (buf[0] == '*') {
            // RESP Array (used for HGETALL result)
            int num_elements = atoi(buf + 1);
            char *current = strstr(buf, "\r\n");
            if (!current) goto cleanup;
            current += 2;
            
            for (int i = 0; i < num_elements; i++) {
                if (current[0] == '$') {
                    int len = atoi(current + 1);
                    current = strstr(current, "\r\n");
                    if (!current) break;
                    current += 2;
                    
                    char *val = malloc(len + 1);
                    memcpy(val, current, len);
                    val[len] = '\0';
                    current += len + 2;
                    
                    if (i % 2 == 1) { // Value
                         int col_idx = i / 2;
                         if (col_idx < res->num_cols) {
                             free(res->rows[0][col_idx]);
                             res->rows[0][col_idx] = val;
                         } else {
                             free(val);
                         }
                    } else {
                         // Field name - for matching in future, for now ignore
                         free(val);
                    }
                } else if (current[0] == ':') { // Integer
                    current = strstr(current, "\r\n") + 2;
                }
            }
        } else if (buf[0] == '-') {
            if (res->error) free(res->error);
            res->error = strdup(buf + 1);
        }
    }

cleanup:
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

#include "../query_processor/schema_manager.h"

int main() {
    schema_init();
    char line[1024];
    printf("RedisSQL CLI\n");
    printf("Type 'exit' to quit.\n");

    while (1) {
        printf("sql> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        
        line[strcspn(line, "\r\n")] = 0;
        if (strcasecmp(line, "exit") == 0) break;
        if (strlen(line) == 0) continue;

        SQLResult res = process_sql(line);
        
        // Handle CLEAR_SCREEN
        if (res.num_rows > 0 && res.rows[0][0] && strcmp(res.rows[0][0], "CLEAR_SCREEN") == 0) {
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif
            free_sql_result(res);
            continue;
        }

        if (!res.error && res.redis_cmd) {
            execute_redis_cmd(&res);
        }
        print_table(res);
        free_sql_result(res);
    }

    return 0;
}
