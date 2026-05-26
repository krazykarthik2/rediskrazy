#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#define closesocket_safe(s) closesocket(s)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket_safe(s) close(s)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sql_parser.h"
#include "../backend/sds.h"

#define listen_port 6380
#define redis_port 6379
#define BACKLOG 10
#define BUF_SIZE 65536

static void execute_redis_cmd(SQLResult *res) {
    if (!res->redis_cmd) return;

    sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        if (res->error) free(res->error);
        res->error = strdup("Could not create socket");
        return;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(redis_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        if (res->error) free(res->error);
        res->error = strdup("Could not connect to Redis server on 127.0.0.1:6379");
        closesocket_safe(sock);
        return;
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
    static char buf[BUF_SIZE];
    int n = 0;
    while (1) {
        int r = recv(sock, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) break;
        n += r;
        buf[n] = 0;
        
        if (n >= 4 && buf[n-2] == '\r' && buf[n-1] == '\n') {
            if (buf[0] == '*' || buf[0] == '$') {
                fd_set readfds;
                struct timeval tv = {0, 10000}; // 10ms
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);
                if (select((int)(sock + 1), &readfds, NULL, NULL, &tv) <= 0) break;
            } else {
                break; 
            }
        } else if (n > 0) {
            // continue reading
        } else {
            break;
        }
    }

    if (n > 0) {
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
                if (res->rows) {
                    for (int i = 0; i < res->num_cols; i++) {
                        if (res->rows[0][i]) free(res->rows[0][i]);
                    }
                    free(res->rows[0]);
                    free(res->rows);
                }
                res->num_rows = 1;
                res->rows = calloc(1, sizeof(char**));
                res->rows[0] = calloc(res->num_cols, sizeof(char*));
                for (int i = 0; i < num_elements; i += 2) {
                    int flen = atoi(current + 1);
                    current = strstr(current, "\r\n");
                    if (!current) break;
                    current += 2;
                    char field_name[128];
                    int copy_len = (flen < 127) ? flen : 127;
                    memcpy(field_name, current, copy_len); field_name[copy_len] = 0;
                    current += flen + 2;

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
            int len = strlen(res->error);
            if (len >= 2 && res->error[len-2] == '\r') res->error[len-2] = '\0';
        }
    }

cleanup:
    closesocket_safe(sock);
}

sds format_result_to_json(SQLResult res) {
    sds json = sdsempty();
    json = sdscat(json, "[");
    for (int r = 0; r < res.num_rows; r++) {
        if (r > 0) json = sdscat(json, ", ");
        json = sdscat(json, "{");
        for (int c = 0; c < res.num_cols; c++) {
            if (c > 0) json = sdscat(json, ", ");
            char temp_col[256];
            snprintf(temp_col, sizeof(temp_col), "\"%s\": ", res.headers[c]);
            json = sdscat(json, temp_col);
            if (res.rows && res.rows[r] && res.rows[r][c]) {
                json = sdscat(json, "\"");
                char *v = res.rows[r][c];
                while (*v) {
                    if (*v == '"') json = sdscat(json, "\\\"");
                    else if (*v == '\\') json = sdscat(json, "\\\\");
                    else if (*v == '\n') json = sdscat(json, "\\n");
                    else if (*v == '\r') json = sdscat(json, "\\r");
                    else if (*v == '\t') json = sdscat(json, "\\t");
                    else {
                        char temp[2] = {*v, 0};
                        json = sdscat(json, temp);
                    }
                    v++;
                }
                json = sdscat(json, "\"");
            } else {
                json = sdscat(json, "null");
            }
        }
        json = sdscat(json, "}");
    }
    json = sdscat(json, "]");
    return json;
}

int main() {
    schema_init();

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    sock_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listen_port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, BACKLOG) == SOCKET_ERROR) {
        perror("listen");
        return 1;
    }

    printf("Query Processor Server listening on port %d...\n", listen_port);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        sock_t client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd == INVALID_SOCKET) continue;

        // Read query line
        char buf[4096];
        int pos = 0;
        while (pos < sizeof(buf) - 1) {
            char c;
            int r = recv(client_fd, &c, 1, 0);
            if (r <= 0) break;
            if (c == '\n') break;
            if (c != '\r') {
                buf[pos++] = c;
            }
        }
        buf[pos] = '\0';

        if (pos > 0) {
            SQLResult res = process_sql(buf);
            if (res.error) {
                sds err_resp = sdsempty();
                char err_buf[512];
                snprintf(err_buf, sizeof(err_buf), "-ERR %s\r\n", res.error);
                err_resp = sdscat(err_resp, err_buf);
                send(client_fd, err_resp, (int)sdslen(err_resp), 0);
                sdsfree(err_resp);
            } else {
                if (res.redis_cmd) {
                    execute_redis_cmd(&res);
                }
                if (res.error) {
                    sds err_resp = sdsempty();
                    char err_buf[512];
                    snprintf(err_buf, sizeof(err_buf), "-ERR %s\r\n", res.error);
                    err_resp = sdscat(err_resp, err_buf);
                    send(client_fd, err_resp, (int)sdslen(err_resp), 0);
                    sdsfree(err_resp);
                } else {
                    sds json = format_result_to_json(res);
                    sds resp = sdsempty();
                    char len_buf[64];
                    snprintf(len_buf, sizeof(len_buf), "$%zu\r\n", sdslen(json));
                    resp = sdscat(resp, len_buf);
                    resp = sdscatlen(resp, json, sdslen(json));
                    resp = sdscat(resp, "\r\n");
                    send(client_fd, resp, (int)sdslen(resp), 0);
                    sdsfree(json);
                    sdsfree(resp);
                }
            }
            free_sql_result(res);
        }

        closesocket_safe(client_fd);
    }

    closesocket_safe(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
