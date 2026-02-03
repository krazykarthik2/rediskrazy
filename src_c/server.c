#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "port.h"
#include "dict.h"
#include "resp.h"
#include "rdb.h"
#include "zset.h"
#include "ae.h"

#define BACKLOG 16
#define BUF_SIZE 4096
#define AOF_FILE "appendonly.aof"

static Dict g_data;   // Strings
static Dict g_zsets;  // ZSets (val is ZSet*)
static FILE *aof = NULL;
static aeEventLoop *g_loop = NULL;

// Helper: Send all data
static int send_all(sock_t s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = (int)send(s, buf + sent, len - sent, 0);
        if (r <= 0) return r;
        sent += r;
    }
    return sent;
}

// Wrapper for Dict operations with AOF logging
void set_kv(const char *key, size_t key_len, const char *val, size_t val_len, time_t ttl_seconds) {
    dict_put(&g_data, key, key_len, val, val_len, ttl_seconds);
    if (aof) {
        fprintf(aof, "*3\r\n$3\r\nSET\r\n$%zu\r\n", key_len);
        fwrite(key, 1, key_len, aof);
        fprintf(aof, "\r\n$%zu\r\n", val_len);
        fwrite(val, 1, val_len, aof);
        fprintf(aof, "\r\n");
        fflush(aof);
    }
}

char *get_kv(const char *key, size_t key_len, size_t *out_len) {
    return dict_get(&g_data, key, key_len, out_len);
}

// Return 1 if deleted, 0 if not found
// Helper to delete key from either String or ZSet db
int delete_key(const char *key, size_t key_len) {
    size_t dummy;
    // 1. Try generic strings
    if (dict_get(&g_data, key, key_len, &dummy)) {
        dict_delete(&g_data, key, key_len);
        if (aof) {
            fprintf(aof, "*2\r\n$3\r\nDEL\r\n$%zu\r\n", key_len);
            fwrite(key, 1, key_len, aof);
            fprintf(aof, "\r\n");
            fflush(aof);
        }
        return 1;
    }
    // 2. Try ZSets
    char *v = dict_get(&g_zsets, key, key_len, &dummy);
    if (v) {
        ZSet *z = *(ZSet**)v;
        zset_free(z);
        dict_delete(&g_zsets, key, key_len);
        if (aof) {
            fprintf(aof, "*2\r\n$3\r\nDEL\r\n$%zu\r\n", key_len);
            fwrite(key, 1, key_len, aof);
            fprintf(aof, "\r\n");
            fflush(aof);
        }
        return 1;
    }
    return 0;
}

// Callback for ZSet rewriting
static void rewrite_zset_cb(const char *member, double score, void *arg) {
    FILE *fp = (FILE*)arg;
    fprintf(fp, "$%zu\r\n%s\r\n%.17g\r\n", strlen(member), member, score);
}

// Rewrite AOF
int rewrite_aof(const char *filename) {
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "temp-%s", filename);
    FILE *fp = fopen(tmpfile, "w");
    if (!fp) return 0;

    // 1. Strings
    if (g_data.tab) {
        for (size_t i = 0; i <= g_data.mask; ++i) {
            DictEntry *e = g_data.tab[i];
            while (e) {
                // Check expiry
                if (e->expire == 0 || e->expire > time(NULL)) {
                    fprintf(fp, "*3\r\n$3\r\nSET\r\n$%zu\r\n", e->key_len);
                    fwrite(e->key, 1, e->key_len, fp);
                    fprintf(fp, "\r\n$%zu\r\n", e->val_len);
                    fwrite(e->val, 1, e->val_len, fp);
                    fprintf(fp, "\r\n");
                    
                    // Persist TTL if exists
                    if (e->expire > 0) {
                         fprintf(fp, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n", e->key_len);
                         fwrite(e->key, 1, e->key_len, fp);
                         fprintf(fp, "\r\n$%lld\r\n", (long long)(e->expire - time(NULL)));
                    }
                }
                e = e->next;
            }
        }
    }

    // 2. ZSets
    if (g_zsets.tab) {
        for (size_t i = 0; i <= g_zsets.mask; ++i) {
            DictEntry *e = g_zsets.tab[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                    ZSet *z = *(ZSet**)e->val;
                    size_t card = zset_card(z);
                    if (card > 0) {
                        // ZADD key score member ...
                        // For simplicity, multiple ZADDs or one big ZADD?
                        // One big ZADD is better but formatting is harder. 
                        // Let's do One ZADD per member for strict MVP simplicity or 
                        // better: ZADD key score member score member
                        
                        fprintf(fp, "*%zu\r\n$4\r\nZADD\r\n$%zu\r\n", 2 + card * 2, e->key_len);
                        fwrite(e->key, 1, e->key_len, fp);
                        fprintf(fp, "\r\n");
                        
                        // We use zset_range to iterate all
                        // But wait, zset_range callback doesn't support easy "one command" flow without context.
                        // Actually, simpler: just iterate bucket? No, ZSet is ZSet*.
                        // Use zset_range with valid callback. 
                        // Callback needs to just emit "score\nmember\n".
                        // Wait, RESP format requires leading $len.
                        // My callback `rewrite_zset_cb` does that.
                        zset_range(z, 0, -1, rewrite_zset_cb, fp);
                        
                        if (e->expire > 0) {
                             fprintf(fp, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n", e->key_len);
                             fwrite(e->key, 1, e->key_len, fp);
                             fprintf(fp, "\r\n$%lld\r\n", (long long)(e->expire - time(NULL)));
                        }
                    }
                }
                e = e->next;
            }
        }
    }

    if (fflush(fp) == EOF) { fclose(fp); return 0; }
    fclose(fp);
    
    // Replace
    if (aof) fclose(aof);
    
#ifdef _WIN32
    unlink(filename); // Windows rename might fail if exists
#endif
    if (rename(tmpfile, filename) == -1) {
        perror("rename");
        aof = fopen(filename, "a+");
        return 0;
    }
    
    aof = fopen(filename, "a+");
    return 1;
}

int exists_key(const char *key, size_t key_len) {
    size_t dummy;
    if (dict_get(&g_data, key, key_len, &dummy)) return 1;
    if (dict_get(&g_zsets, key, key_len, &dummy)) return 1;
    return 0;
}

// ZRANGE callback
static void zrange_sender(const char *member, double score, void *arg) {
    (void)score;
    sock_t *client = (sock_t*)arg;
    size_t len = strlen(member);
    size_t total_len = 64 + len + 5;
    char *buf = (char*)malloc(total_len);
    if(buf) {
        int n = snprintf(buf, total_len, "$%zu\r\n", len);
        if (n > 0) {
            memcpy(buf + n, member, len);
            memcpy(buf + n + len, "\r\n", 2);
            send_all(*client, buf, n + (int)len + 2);
        }
        free(buf);
    }
}

void handle_command(sock_t client, char **argv, int *arglens, int argc) {
    if (argc <= 0) return;

    for (char *p = argv[0]; *p; ++p) *p = toupper((unsigned char)*p);
    
    if (strcmp(argv[0], "PING") == 0) {
        const char *resp = "+PONG\r\n";
        send_all(client, resp, (int)strlen(resp));
    } else if (strcmp(argv[0], "SET") == 0) {
        if (argc < 3) { 
            send_all(client, "-ERR wrong number of arguments for 'SET'\r\n", 45); 
        } else {
            // Check if key exists as ZSet, if so, delete it
            size_t klen = arglens[1]; 
            delete_key(argv[1], klen); // Ensures we overwrite any type
            
            time_t ttl = 0;
            size_t val_len = arglens[2];
            if (argc >= 5) {
                for (char *p = argv[3]; *p; ++p) *p = toupper((unsigned char)*p);
                if (strcmp(argv[3], "EX") == 0) ttl = atoi(argv[4]);
            }
            set_kv(argv[1], klen, argv[2], val_len, ttl);
            send_all(client, "+OK\r\n", 5);
        }
    } else if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2) { 
            send_all(client, "-ERR wrong number of arguments for 'GET'\r\n", 45); 
        } else {
            size_t key_len = arglens[1];
            size_t dummy;
            // Check incorrect type (ZSet)
            if (dict_get(&g_zsets, argv[1], key_len, &dummy)) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                size_t val_len = 0;
                char *v = get_kv(argv[1], key_len, &val_len);
                if (!v) {
                    send_all(client, "$-1\r\n", 5);
                } else {
                    // Buffer response to avoid fragmentation (header + CRLF + value + CRLF)
                    size_t total_len = 64 + val_len + 5; 
                    char *resp_buf = (char*)malloc(total_len);
                    if (resp_buf) {
                        int hn = snprintf(resp_buf, total_len, "$%zu\r\n", val_len);
                        if (hn > 0) {
                            memcpy(resp_buf + hn, v, val_len);
                            memcpy(resp_buf + hn + val_len, "\r\n", 2);
                            send_all(client, resp_buf, hn + (int)val_len + 2);
                        }
                        free(resp_buf);
                    } else {
                        // Fallback
                        char hdr[64];
                        int n = snprintf(hdr, sizeof(hdr), "$%zu\r\n", val_len);
                        send_all(client, hdr, n);
                        // Binary safe send
                        send_all(client, v, (int)val_len);
                        send_all(client, "\r\n", 2);
                    }
                }
            }
        }
    } else if (strcmp(argv[0], "DEL") == 0) {
        if (argc < 2) {
             send_all(client, "-ERR wrong number of arguments for 'DEL'\r\n", 45);
        } else {
            int deleted = 0;
            for (int i = 1; i < argc; ++i) {
                deleted += delete_key(argv[i], arglens[i]);
            }
            char resp[32];
            int n = snprintf(resp, sizeof(resp), ":%d\r\n", deleted);
            send_all(client, resp, n);
        }
    } else if (strcmp(argv[0], "EXISTS") == 0) {
        if (argc < 2) {
             send_all(client, "-ERR wrong number of arguments for 'EXISTS'\r\n", 48);
        } else {
            int count = 0;
            for (int i = 1; i < argc; ++i) {
                count += exists_key(argv[i], arglens[i]);
            }
            char resp[32];
            int n = snprintf(resp, sizeof(resp), ":%d\r\n", count);
            send_all(client, resp, n);
        }
    } else if (strcmp(argv[0], "INCR") == 0 || strcmp(argv[0], "DECR") == 0) {
        size_t klen = arglens[1];
        size_t dummy;
        if (dict_get(&g_zsets, argv[1], klen, &dummy)) {
            send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
        } else {
            if (argc < 2) {
                 send_all(client, "-ERR wrong number of arguments\r\n", 30);
            } else {
                size_t vlen = 0;
                char *curr = get_kv(argv[1], klen, &vlen);
                long long val = 0;
                if (curr) val = strtoll(curr, NULL, 10);
                if (strcmp(argv[0], "INCR") == 0) val++; else val--;
                char buf[64];
                int n = snprintf(buf, sizeof(buf), "%lld", val);
                time_t expire = dict_get_expiry(&g_data, argv[1], klen);
                time_t now = time(NULL);
                time_t ttl_rem = (expire > 0 && expire > now) ? expire - now : 0;
                set_kv(argv[1], klen, buf, n, ttl_rem);
                char resp[64];
                n = snprintf(resp, sizeof(resp), ":%lld\r\n", val);
                send_all(client, resp, n);
            }
        }
    } else if (strcmp(argv[0], "TTL") == 0) {
        size_t klen = arglens[1];
        size_t dummy;
        if (dict_get(&g_data, argv[1], klen, &dummy)) {
             time_t expire = dict_get_expiry(&g_data, argv[1], klen);
             if (expire == 0) send_all(client, ":-1\r\n", 5);
             else {
                 time_t now = time(NULL);
                 long long t = (long long)(expire - now);
                 if (t < 0) t = 0;
                 char buf[64]; snprintf(buf, sizeof(buf), ":%lld\r\n", t);
                 send_all(client, buf, (int)strlen(buf));
             }
        } else if (dict_get(&g_zsets, argv[1], klen, &dummy)) {
             time_t expire = dict_get_expiry(&g_zsets, argv[1], klen);
             if (expire == 0) send_all(client, ":-1\r\n", 5);
             else {
                 time_t now = time(NULL);
                 long long t = (long long)(expire - now);
                 if (t < 0) t = 0;
                 char buf[64]; snprintf(buf, sizeof(buf), ":%lld\r\n", t);
                 send_all(client, buf, (int)strlen(buf));
             }
        } else {
             send_all(client, ":-2\r\n", 5);
        }

    } else if (strcmp(argv[0], "EXPIRE") == 0) {
        if (argc < 3) {
            send_all(client, "-ERR wrong number of arguments for 'EXPIRE'\r\n", 48);
        } else {
            size_t klen = arglens[1];
            time_t ttl = atoi(argv[2]);
            int res = dict_set_expiry(&g_data, argv[1], klen, ttl);
            if (!res) {
                res = dict_set_expiry(&g_zsets, argv[1], klen, ttl);
            }
            if (res) send_all(client, ":1\r\n", 4);
            else send_all(client, ":0\r\n", 4);
            
            if (res && aof) {
                 fprintf(aof, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n", 
                     klen, argv[1], strlen(argv[2]), argv[2]);
                 fflush(aof);
            }
        }

    } else if (strcmp(argv[0], "FLUSHDB") == 0) {
        dict_clear(&g_data);
        dict_clear(&g_zsets);
        
        if (aof) {
            fprintf(aof, "*1\r\n$7\r\nFLUSHDB\r\n\r\n");
            fflush(aof);
        }
        send_all(client, "+OK\r\n", 5);

    } else if (strcmp(argv[0], "ZADD") == 0) {
        if (argc < 4) {
             send_all(client, "-ERR wrong number of arguments for 'ZADD'\r\n", 46);
        } else {
            size_t klen = arglens[1];
            size_t dummy;
            if (dict_get(&g_data, argv[1], klen, &dummy)) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                double score = atof(argv[2]);
                char *member = argv[3];
                // Note: ZSet member binary safety logic handled here? 
                // We assume member is string, but ZSet implementation might use simple strdup/strcmp.
                // For full binary safety, zset needs length too. 
                // MVP: keep using string for member, but trust arglen if needed.
                
                ZSet *z = NULL;
                char *v = dict_get(&g_zsets, argv[1], klen, &dummy);
                if (v) {
                    z = *(ZSet**)v;
                } else {
                    z = zset_create();
                    dict_put(&g_zsets, argv[1], klen, (char*)&z, sizeof(ZSet*), 0);
                }
                
                int added = zset_add(z, member, score);
                
                if (aof) {
                    fprintf(aof, "*4\r\n$4\r\nZADD\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n",
                        klen, argv[1], strlen(argv[2]), argv[2], strlen(member), member);
                    fflush(aof);
                }
                
                char resp[32];
                snprintf(resp, sizeof(resp), ":%d\r\n", added);
                send_all(client, resp, (int)strlen(resp));
            }
        }
    } else if (strcmp(argv[0], "ZSCORE") == 0) {
        if (argc < 3) send_all(client, "-ERR wrong number of arguments for 'ZSCORE'\r\n", 48);
        else {
             size_t klen = arglens[1];
             size_t dummy;
             if (dict_get(&g_data, argv[1], klen, &dummy)) {
                 send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
             } else {
                 char *v = dict_get(&g_zsets, argv[1], klen, &dummy);
                 if (!v) {
                     send_all(client, "$-1\r\n", 5);
                 } else {
                     ZSet *z = *(ZSet**)v;
                     double score;
                     if (zset_score(z, argv[2], &score)) {
                         char buf[128];
                         int n = snprintf(buf, sizeof(buf), "%.17g", score);
                         char resp[160];
                         int rn = snprintf(resp, sizeof(resp), "$%d\r\n%s\r\n", n, buf);
                         send_all(client, resp, rn);
                     } else {
                         send_all(client, "$-1\r\n", 5);
                     }
                 }
             }
        }
    } else if (strcmp(argv[0], "ZRANGE") == 0) {
        if (argc < 4) send_all(client, "-ERR wrong number of arguments for 'ZRANGE'\r\n", 48);
        else {
             size_t klen = arglens[1];
             size_t dummy;
             if (dict_get(&g_data, argv[1], klen, &dummy)) {
                 send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
             } else {
                 char *v = dict_get(&g_zsets, argv[1], klen, &dummy);
                 if (!v) {
                     send_all(client, "*0\r\n", 4);
                 } else {
                     ZSet *z = *(ZSet**)v;
                     int start = atoi(argv[2]);
                     int end = atoi(argv[3]);
                     
                     int len = (int)zset_card(z);
                     if (start < 0) start += len;
                     if (end < 0) end += len;
                     if (start < 0) start = 0;
                     if (end >= len) end = len - 1;
                     
                     int count = 0;
                     if (start <= end) count = end - start + 1;
                     
                     char buf[32];
                     int n = snprintf(buf, sizeof(buf), "*%d\r\n", count);
                     send_all(client, buf, n);
                     
                     if (count > 0) {
                        zset_range(z, atoi(argv[2]), atoi(argv[3]), zrange_sender, &client);
                     }
                 }
             }
        }
    } else if (strcmp(argv[0], "SAVE") == 0) {
        if (rdb_save("dump.rdb", &g_data, &g_zsets) == 0) {
            send_all(client, "+OK\r\n", 5);
        } else {
            send_all(client, "-ERR save failed\r\n", 18);
        }
    } else if (strcmp(argv[0], "BGREWRITEAOF") == 0) {
        if (rewrite_aof(AOF_FILE)) {
             send_all(client, "+OK\r\n", 5);
        } else {
             send_all(client, "-ERR rewrite failed\r\n", 21);
        }
    } else {
        send_all(client, "-ERR unknown command\r\n", 24);
    }
}

int make_nonblocking(sock_t s) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

// Event Loop Handlers

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    (void)privdata; (void)mask; (void)el;
    char buf[BUF_SIZE];
    int nread = recv((sock_t)fd, buf, BUF_SIZE, 0);
    if (nread <= 0) {
        if (nread == 0) {
             // Client closed connection
        } else {
             // Error
        }
        aeDeleteFileEvent(el, fd, AE_READABLE);
        closesocket((sock_t)fd);
        return;
    }
    
    char **argv2 = NULL; 
    int *arglens = NULL;
    int argc2 = 0;
    int consumed = resp_parse_array(buf, nread, &argv2, &arglens, &argc2); // Updated call
    
    if (consumed > 0) {
        handle_command((sock_t)fd, argv2, arglens, argc2); // Updated call
        resp_free_argv(argv2, argc2);
        free(arglens);
    } else if (consumed == 0) {
         // Incomplete
    } else {
        send_all((sock_t)fd, "-ERR parse error\r\n", 17);
    }
}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    (void)privdata; (void)mask;
    struct sockaddr_in caddr; socklen_t len = sizeof(caddr);
    sock_t cs = accept((sock_t)fd, (struct sockaddr*)&caddr, &len);
    if (cs == INVALID_SOCKET) return;
    
    make_nonblocking(cs);
    aeCreateFileEvent(el, (int)cs, AE_READABLE, readQueryFromClient, NULL);
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    (void)eventLoop; (void)id; (void)clientData;
    dict_active_expire(&g_data, 20);
    dict_active_expire(&g_zsets, 20);
    return 1000; // Run every 1000ms
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    
    dict_init(&g_data, 16); 
    dict_init(&g_zsets, 16);
    
    // Load RDB
    printf("Loading RDB...\n");
    if (rdb_load("dump.rdb", &g_data, &g_zsets) == 0) {
        printf("RDB loaded successfully.\n");
    } else {
        printf("No RDB file found or load failed.\n");
    }
    
    aof = fopen(AOF_FILE, "a+");
    if (!aof) {
        fprintf(stderr, "Warning: could not open AOF file '%s' for append\n", AOF_FILE);
    }

    sock_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { perror("bind"); return 1; }
    if (listen(listen_sock, BACKLOG) == SOCKET_ERROR) { perror("listen"); return 1; }

    make_nonblocking(listen_sock);
    
    // Init Event Loop
    g_loop = aeCreateEventLoop(1024);
    
    // Create Time Event
    aeCreateTimeEvent(g_loop, 1, serverCron, NULL, NULL);
    
    // Create File Event for Accept
    if (aeCreateFileEvent(g_loop, (int)listen_sock, AE_READABLE, acceptTcpHandler, NULL) == AE_ERR) {
        printf("Unrecoverable error creating server.ipfd file event.\n");
        return 1;
    }

    printf("rediskrazy server listening on port %d\n", SERVER_PORT);
    
    // Run Loop
    aeMain(g_loop);

    if (aof) fclose(aof);
    closesocket(listen_sock);
    WSACleanup();
    dict_destroy(&g_data);
    dict_destroy(&g_zsets); 
    aeDeleteEventLoop(g_loop);
    return 0;
}
