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
    static char buf[65536];
    int n = 0;
    while (1) {
        int r = recv(sock, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) break;
        n += r;
        buf[n] = 0;
        
        // Basic check: if it's an array and we have the full first line, 
        // we might still need more if it's long.
        // For a hacky but effective solution, we'll wait a bit and try to read more
        // if the buffer seems incomplete (e.g. doesn't end in \r\n).
        if (n >= 4 && buf[n-2] == '\r' && buf[n-1] == '\n') {
            // Check if we expect more elements based on the first line
            if (buf[0] == '*' || buf[0] == '$') {
                // If it's short, let's assume we might need more.
                // In a real Redis client, we'd parse the count and read until EOF or full count.
                // Here we'll just wait a tiny bit to see if more comes in.
                fd_set readfds;
                struct timeval tv = {0, 10000}; // 10ms
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);
                if (select(sock + 1, &readfds, NULL, NULL, &tv) <= 0) break;
            } else {
                break; 
            }
        } else if (n > 0) {
            // Continue reading
        } else {
            break;
        }
    }

    if (n > 0) {
        
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
            // RESP Array
            int num_elements = atoi(buf + 1);
            char *current = strstr(buf, "\r\n");
            if (!current) goto cleanup;
            current += 2;
            
            if (res->redis_cmd && strncmp(res->redis_cmd, "HSCANALL", 8) == 0) {
                // Nested Array: [ [f,v,f,v], [f,v,f,v], ... ]
                res->rows = calloc(num_elements, sizeof(char**));
                int actual_rows = 0;
                for (int r = 0; r < num_elements; r++) {
                    if (!current || current[0] != '*') break;
                    
                    int fields_val_count = atoi(current + 1);
                    current = strstr(current, "\r\n");
                    if (!current) break;
                    current += 2;
                    
                    res->rows[r] = calloc(res->num_cols, sizeof(char*));
                    for (int f = 0; f < fields_val_count/2; f++) {
                        if (!current || current[0] != '$') break;
                        int flen = atoi(current + 1);
                        current = strstr(current, "\r\n");
                        if (!current) break;
                        current += 2;
                        char field_name[128];
                        int copy_len = (flen < 127) ? flen : 127;
                        memcpy(field_name, current, copy_len); field_name[copy_len] = 0;
                        current += flen + 2;
                        
                        if (!current || current[0] != '$') break;
                        int vlen = atoi(current + 1);
                        current = strstr(current, "\r\n");
                        if (!current) break;
                        current += 2;
                        char *val = malloc(vlen + 1);
                        memcpy(val, current, vlen); val[vlen] = 0;
                        current += vlen + 2;
                        
                        // Map field to column
                        int col_idx = -1;
                        for(int c=0; c<res->num_cols; c++) {
                            if(strcasecmp(res->headers[c], field_name) == 0) { col_idx = c; break; }
                        }
                        if(col_idx != -1) res->rows[r][col_idx] = val;
                        else free(val);
                    }
                    actual_rows++;
                }
                res->num_rows = actual_rows;
            }
 else if (res->num_cols == 1) {
                // Multi-row result (like KEYS)
                if (num_elements > 0) {
                    res->rows = calloc(num_elements, sizeof(char**));
                    int actual_rows = 0;
                    for (int i = 0; i < num_elements; i++) {
                        if (!current) break;
                        if (current[0] == '$') {
                            int len = atoi(current + 1);
                            current = strstr(current, "\r\n");
                            if (!current) break;
                            current += 2;
                            char *val = malloc(len + 1);
                            memcpy(val, current, len); val[len] = '\0';
                            current += len + 2;
                            res->rows[i] = malloc(sizeof(char*) * 1);
                            res->rows[i][0] = val;
                            actual_rows++;
                        } else if (current[0] == ':') {
                            current = strstr(current, "\r\n");
                            if (!current) break;
                            current += 2;
                            actual_rows++;
                        }
                    }
                    res->num_rows = actual_rows;
                }
            } else {
                // Single row multi-column result (HGETALL)
                res->num_rows = 1;
                res->rows = calloc(1, sizeof(char**));
                res->rows[0] = calloc(res->num_cols, sizeof(char*));
                for (int i = 0; i < num_elements; i += 2) {
                    // Field
                    int flen = atoi(current + 1);
                    current = strstr(current, "\r\n");
                    if (!current) break;
                    current += 2;
                    char field_name[128];
                    int copy_len = (flen < 127) ? flen : 127;
                    memcpy(field_name, current, copy_len); field_name[copy_len] = 0;
                    current += flen + 2;

                    // Value
                    int vlen = atoi(current + 1);
                    current = strstr(current, "\r\n");
                    if (!current) break;
                    current += 2;
                    char *val = malloc(vlen + 1);
                    memcpy(val, current, vlen); val[vlen] = 0;
                    current += vlen + 2;
                    
                    int col_idx = -1;
                    for(int c=0; c<res->num_cols; c++) {
                        if(strcasecmp(res->headers[c], field_name) == 0) { col_idx = c; break; }
                    }
                    if(col_idx != -1) res->rows[0][col_idx] = val;
                    else free(val);
                }
            }
        }
 else if (buf[0] == '-') {
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
