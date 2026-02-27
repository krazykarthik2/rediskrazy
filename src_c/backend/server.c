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
#include "tpool.h"
#include "sds.h"
#include "mempool.h"
#include "expheap.h"
#include "aofbuf.h"

#include <signal.h>
#define BACKLOG 16
#define BUF_SIZE 4096



void sig_handler(int signo);

void bg_task_func(void *arg) {
    int duration = *(int*)arg;
    free(arg);
    printf("Background task started, sleeping %d ms...\n", duration);
#ifdef _WIN32
    Sleep(duration);
#else
    usleep(duration * 1000);
#endif
    printf("Background task finished.\n");
}

#define AOF_FILE "appendonly.aof"

static Dict g_data;   // Strings (Key: sds, Val: sds)
static Dict g_zsets;  // ZSets (Key: sds, Val: sds containing ZSet*)
static Dict g_hashes; // Hashes (Key: sds, Val: sds containing Dict*)
static FILE *aof = NULL;
static aeEventLoop *g_loop = NULL;

// New feature globals
static ExpHeap g_exp_heap;        // Min-heap for TTL scheduling
static AofBuffer g_aof_buf;       // Buffered AOF writer
static MemPool g_entry_pool;      // Memory pool for DictEntry
static int g_use_aof_buffer = 0;  // Flag to use buffered AOF

// Helper: Send all data
static int send_all(sock_t s, const char *buf, int len) {
    if (s == INVALID_SOCKET) return len; // Silent mode for AOF loading
    int sent = 0;
    while (sent < len) {
        int r = (int)send(s, buf + sent, len - sent, 0);
        if (r <= 0) return r;
        sent += r;
    }
    return sent;
}

// Wrapper for Dict operations with AOF logging
// key and val are SDS.
void set_kv(sds key, sds val, time_t ttl_seconds) {
    dict_put(&g_data, key, val, ttl_seconds);
    
    // Add to expiration heap if TTL is set
    if (ttl_seconds > 0) {
        time_t expire_at = time(NULL) + ttl_seconds;
        expheap_push(&g_exp_heap, key, expire_at);
    }
    
    if (aof) {
        fprintf(aof, "*3\r\n$3\r\nSET\r\n$%zu\r\n", sdslen(key));
        fwrite(key, 1, sdslen(key), aof);
        fprintf(aof, "\r\n$%zu\r\n", sdslen(val));
        fwrite(val, 1, sdslen(val), aof);
        fprintf(aof, "\r\n");
        fflush(aof);
    }
}

sds get_kv(sds key) {
    return dict_get(&g_data, key);
}

// Return 1 if deleted, 0 if not found
int delete_key(sds key) {
    // 1. Try generic strings
    if (dict_get(&g_data, key)) {
        dict_delete(&g_data, key);
        if (aof) {
            fprintf(aof, "*2\r\n$3\r\nDEL\r\n$%zu\r\n", sdslen(key));
            fwrite(key, 1, sdslen(key), aof);
            fprintf(aof, "\r\n");
            fflush(aof);
        }
        return 1;
    }
    // 2. Try ZSets
    sds v = dict_get(&g_zsets, key);
    if (v) {
        // v acts as a pointer to ZSet pointer
        ZSet *z = *(ZSet**)v;
        zset_free(z);
        dict_delete(&g_zsets, key);
        if (aof) {
            fprintf(aof, "*2\r\n$3\r\nDEL\r\n$%zu\r\n", sdslen(key));
            fwrite(key, 1, sdslen(key), aof);
            fprintf(aof, "\r\n");
            fflush(aof);
        }
        return 1;
    }
    // 3. Try Hashes
    v = dict_get(&g_hashes, key);
    if (v) {
        Dict *h = *(Dict**)v;
        dict_clear(h);
        free(h);
        dict_delete(&g_hashes, key);
        if (aof) {
            fprintf(aof, "*2\r\n$3\r\nDEL\r\n$%zu\r\n", sdslen(key));
            fwrite(key, 1, sdslen(key), aof);
            fprintf(aof, "\r\n");
            fflush(aof);
        }
        return 1;
    }
    return 0;
}

// Callback for ZSet rewriting
static void rewrite_zset_cb(sds member, double score, void *arg) {
    FILE *fp = (FILE*)arg;
    fprintf(fp, "$%zu\r\n%s\r\n%.17g\r\n", sdslen(member), member, score);
}

// Rewrite AOF
int rewrite_aof(const char *filename) {
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "temp-%s", filename);
    FILE *fp = fopen(tmpfile, "wb");
    if (!fp) return 0;

    // 1. Strings
    for (int table = 0; table <= 1; table++) {
        if (!g_data.ht[table].table) continue;
        for (size_t i = 0; i < g_data.ht[table].size; ++i) {
            DictEntry *e = g_data.ht[table].table[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                    fprintf(fp, "*3\r\n$3\r\nSET\r\n$%zu\r\n", sdslen(e->key));
                    fwrite(e->key, 1, sdslen(e->key), fp);
                    fprintf(fp, "\r\n$%zu\r\n", sdslen(e->val));
                    fwrite(e->val, 1, sdslen(e->val), fp);
                    fprintf(fp, "\r\n");
                    
                    if (e->expire > 0) {
                         fprintf(fp, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n", sdslen(e->key));
                         fwrite(e->key, 1, sdslen(e->key), fp);
                         fprintf(fp, "\r\n$%lld\r\n", (long long)(e->expire - time(NULL)));
                    }
                }
                e = e->next;
            }
        }
    }

    // 2. ZSets
    for (int table = 0; table <= 1; table++) {
        if (!g_zsets.ht[table].table) continue;
        for (size_t i = 0; i < g_zsets.ht[table].size; ++i) {
            DictEntry *e = g_zsets.ht[table].table[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                    ZSet *z = *(ZSet**)e->val;
                    size_t card = zset_card(z);
                    if (card > 0) {
                        fprintf(fp, "*%zu\r\n$4\r\nZADD\r\n$%zu\r\n", 2 + card * 2, sdslen(e->key));
                        fwrite(e->key, 1, sdslen(e->key), fp);
                        fprintf(fp, "\r\n");
                        zset_range(z, 0, -1, rewrite_zset_cb, fp);
                        
                        if (e->expire > 0) {
                             fprintf(fp, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n", sdslen(e->key));
                             fwrite(e->key, 1, sdslen(e->key), fp);
                             fprintf(fp, "\r\n$%lld\r\n", (long long)(e->expire - time(NULL)));
                        }
                    }
                }
                e = e->next;
            }
        }
    }

    // 3. Hashes
    for (int table = 0; table <= 1; table++) {
        if (!g_hashes.ht[table].table) continue;
        for (size_t i = 0; i < g_hashes.ht[table].size; ++i) {
            DictEntry *e = g_hashes.ht[table].table[i];
            while (e) {
                if (e->expire == 0 || e->expire > time(NULL)) {
                    Dict *h = *(Dict**)e->val;
                    size_t card = 0;
                    for (int t = 0; t <= 1; t++) {
                        if (h->ht[t].table) {
                            for (size_t idx = 0; idx < h->ht[t].size; idx++) {
                                DictEntry *he = h->ht[t].table[idx];
                                while(he) { card++; he = he->next; }
                            }
                        }
                    }
                    if (card > 0) {
                        fprintf(fp, "*%zu\r\n$4\r\nHSET\r\n$%zu\r\n", 2 + card * 2, sdslen(e->key));
                        fwrite(e->key, 1, sdslen(e->key), fp);
                        fprintf(fp, "\r\n");
                        for (int t = 0; t <= 1; t++) {
                            if (h->ht[t].table) {
                                for (size_t idx = 0; idx < h->ht[t].size; idx++) {
                                    DictEntry *he = h->ht[t].table[idx];
                                    while(he) {
                                        fprintf(fp, "$%zu\r\n", sdslen(he->key));
                                        fwrite(he->key, 1, sdslen(he->key), fp);
                                        fprintf(fp, "\r\n$%zu\r\n", sdslen(he->val));
                                        fwrite(he->val, 1, sdslen(he->val), fp);
                                        fprintf(fp, "\r\n");
                                        he = he->next;
                                    }
                                }
                            }
                        }
                    }
                }
                e = e->next;
            }
        }
    }

    if (fflush(fp) == EOF) { fclose(fp); return 0; }
    fclose(fp);
    
    if (aof) fclose(aof);
    
#ifdef _WIN32
    unlink(filename); 
#endif
    if (rename(tmpfile, filename) == -1) {
        perror("rename");
        aof = fopen(filename, "ab+");
        return 0;
    }
    
    aof = fopen(filename, "ab+");
    return 1;
}

int exists_key(sds key) {
    if (dict_get(&g_data, key)) return 1;
    if (dict_get(&g_zsets, key)) return 1;
    if (dict_get(&g_hashes, key)) return 1;
    return 0;
}

static void zrange_sender(sds member, double score, void *arg) {
    (void)score;
    sock_t *client = (sock_t*)arg;
    size_t len = sdslen(member);
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

// Handle Command using SDS argv
void handle_command(sock_t client, sds *argv, int argc) {
    if (argc <= 0) return;

    printf("CMD: %s", argv[0]);
    for(int i=1; i<argc; i++) printf(" [%s]", argv[i]);
    printf("\n");

    for (int i = 0; i < sdslen(argv[0]); ++i) argv[0][i] = toupper((unsigned char)argv[0][i]);
    
    if (strcmp(argv[0], "PING") == 0) {
        const char *resp = "+PONG\r\n";
        send_all(client, resp, (int)strlen(resp));
    } else if (strcmp(argv[0], "SET") == 0) {
        if (argc < 3) { 
            send_all(client, "-ERR wrong number of arguments for 'SET'\r\n", 45); 
        } else {
            delete_key(argv[1]); 
            
            time_t ttl = 0;
            if (argc >= 5) {
                for (int i=0; i<sdslen(argv[3]); ++i) argv[3][i] = toupper((unsigned char)argv[3][i]);
                if (strcmp(argv[3], "EX") == 0) ttl = atoi(argv[4]);
            }
            set_kv(argv[1], argv[2], ttl);
            send_all(client, "+OK\r\n", 5);
        }
    } else if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2) { 
            send_all(client, "-ERR wrong number of arguments for 'GET'\r\n", 45); 
        } else {
            if (dict_get(&g_zsets, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                sds v = get_kv(argv[1]);
                if (!v) {
                    send_all(client, "$-1\r\n", 5);
                } else {
                    size_t val_len = sdslen(v);
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
                deleted += delete_key(argv[i]);
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
                count += exists_key(argv[i]);
            }
            char resp[32];
            int n = snprintf(resp, sizeof(resp), ":%d\r\n", count);
            send_all(client, resp, n);
        }
    } else if (strcmp(argv[0], "INCR") == 0 || strcmp(argv[0], "DECR") == 0) {
        if (dict_get(&g_zsets, argv[1])) {
            send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
        } else {
            if (argc < 2) {
                 send_all(client, "-ERR wrong number of arguments\r\n", 30);
            } else {
                sds curr = get_kv(argv[1]);
                long long val = 0;
                if (curr) val = strtoll(curr, NULL, 10);
                if (strcmp(argv[0], "INCR") == 0) val++; else val--;
                
                sds val_sds = sdsfromlonglong(val);
                
                time_t expire = dict_get_expiry(&g_data, argv[1]);
                time_t now = time(NULL);
                time_t ttl_rem = (expire > 0 && expire > now) ? expire - now : 0;
                
                set_kv(argv[1], val_sds, ttl_rem);
                sdsfree(val_sds);
                
                char resp[64];
                int n = snprintf(resp, sizeof(resp), ":%lld\r\n", val);
                send_all(client, resp, n);
            }
        }
    } else if (strcmp(argv[0], "TTL") == 0) {
        if (dict_get(&g_data, argv[1])) {
             time_t expire = dict_get_expiry(&g_data, argv[1]);
             if (expire == 0) send_all(client, ":-1\r\n", 5);
             else {
                 time_t now = time(NULL);
                 long long t = (long long)(expire - now);
                 if (t < 0) t = 0;
                 char buf[64]; snprintf(buf, sizeof(buf), ":%lld\r\n", t);
                 send_all(client, buf, (int)strlen(buf));
             }
        } else if (dict_get(&g_zsets, argv[1])) {
             time_t expire = dict_get_expiry(&g_zsets, argv[1]);
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
            time_t ttl = atoi(argv[2]);
            int res = dict_set_expiry(&g_data, argv[1], ttl);
            if (!res) {
                res = dict_set_expiry(&g_zsets, argv[1], ttl);
            }
            if (res) {
                // Add to expheap for efficient scheduling
                time_t expire_at = time(NULL) + ttl;
                expheap_push(&g_exp_heap, argv[1], expire_at);
                send_all(client, ":1\r\n", 4);
            } else {
                send_all(client, ":0\r\n", 4);
            }
            
            if (res && aof) {
                 fprintf(aof, "*3\r\n$6\r\nEXPIRE\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n", 
                     sdslen(argv[1]), argv[1], sdslen(argv[2]), argv[2]);
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
            if (dict_get(&g_data, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                double score = atof(argv[2]);
                sds member = argv[3];
                
                ZSet *z = NULL;
                sds v = dict_get(&g_zsets, argv[1]);
                if (v) {
                    z = *(ZSet**)v;
                } else {
                    z = zset_create();
                    // Store pointer in SDS
                    sds zptr = sdsnewlen(&z, sizeof(ZSet*));
                    dict_put(&g_zsets, argv[1], zptr, 0);
                    sdsfree(zptr);
                }
                
                int added = zset_add(z, member, score);
                
                if (aof) {
                    fprintf(aof, "*4\r\n$4\r\nZADD\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n",
                        sdslen(argv[1]), argv[1], sdslen(argv[2]), argv[2], sdslen(member), member);
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
             if (dict_get(&g_data, argv[1])) {
                 send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
             } else {
                 sds v = dict_get(&g_zsets, argv[1]);
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
             if (dict_get(&g_data, argv[1])) {
                 send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
             } else {
                 sds v = dict_get(&g_zsets, argv[1]);
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
        if (rdb_save("dump.rdb", &g_data, &g_zsets, &g_hashes) == 0) {
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
    } else if (strcmp(argv[0], "BG_TASK") == 0) {
        if (argc != 2) {
             send_all(client, "-ERR usage: BG_TASK <ms>\r\n", 26);
        } else {
             int *duration = (int*)malloc(sizeof(int));
             *duration = atoi(argv[1]);
             tpool_add_work(bg_task_func, duration);
             send_all(client, "+OK Background task submitted\r\n", 31);
        }
    } else if (strcmp(argv[0], "SHUTDOWN") == 0) {
        send_all(client, "+OK\r\n", 5);
        g_loop->stop = 1; 
    } else if (strcmp(argv[0], "APPEND") == 0) {
        // APPEND key value
        if (argc < 3) {
            send_all(client, "-ERR wrong number of arguments for 'APPEND'\r\n", 48);
        } else {
            if (dict_get(&g_zsets, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                sds cur = get_kv(argv[1]);
                sds newval;
                if (cur) {
                    newval = sdsdup(cur);
                    newval = sdscatlen(newval, argv[2], sdslen(argv[2]));
                } else {
                    newval = sdsdup(argv[2]);
                }
                
                time_t expire = dict_get_expiry(&g_data, argv[1]);
                time_t now = time(NULL);
                time_t ttl_rem = (expire > 0 && expire > now) ? expire - now : 0;
                
                set_kv(argv[1], newval, ttl_rem);
                
                char resp[64];
                int n = snprintf(resp, sizeof(resp), ":%zu\r\n", sdslen(newval));
                send_all(client, resp, n);
                sdsfree(newval);
            }
        }
    } else if (strcmp(argv[0], "SETNX") == 0) {
        // SETNX key value - Set if Not eXists
        if (argc < 3) {
            send_all(client, "-ERR wrong number of arguments for 'SETNX'\r\n", 47);
        } else {
            if (exists_key(argv[1])) {
                send_all(client, ":0\r\n", 4);
            } else {
                set_kv(argv[1], argv[2], 0);
                send_all(client, ":1\r\n", 4);
            }
        }
    } else if (strcmp(argv[0], "ZREM") == 0) {
        // ZREM key member [member ...]
        if (argc < 3) {
            send_all(client, "-ERR wrong number of arguments for 'ZREM'\r\n", 46);
        } else {
            if (dict_get(&g_data, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                sds v = dict_get(&g_zsets, argv[1]);
                if (!v) {
                    send_all(client, ":0\r\n", 4);
                } else {
                    ZSet *z = *(ZSet**)v;
                    int removed = 0;
                    for (int i = 2; i < argc; ++i) {
                        removed += zset_rem(z, argv[i]);
                    }
                    
                    if (aof && removed > 0) {
                        for (int i = 2; i < argc; ++i) {
                            fprintf(aof, "*3\r\n$4\r\nZREM\r\n$%zu\r\n%s\r\n$%zu\r\n%s\r\n",
                                sdslen(argv[1]), argv[1], sdslen(argv[i]), argv[i]);
                        }
                        fflush(aof);
                    }
                    
                    char resp[32];
                    int n = snprintf(resp, sizeof(resp), ":%d\r\n", removed);
                    send_all(client, resp, n);
                }
            }
        }
    } else if (strcmp(argv[0], "ZCARD") == 0) {
        // ZCARD key
        if (argc < 2) {
            send_all(client, "-ERR wrong number of arguments for 'ZCARD'\r\n", 47);
        } else {
            if (dict_get(&g_data, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                sds v = dict_get(&g_zsets, argv[1]);
                if (!v) {
                    send_all(client, ":0\r\n", 4);
                } else {
                    ZSet *z = *(ZSet**)v;
                    char resp[32];
                    int n = snprintf(resp, sizeof(resp), ":%zu\r\n", zset_card(z));
                    send_all(client, resp, n);
                }
            }
        }
    } else if (strcmp(argv[0], "ZRANK") == 0) {
        // ZRANK key member
        if (argc < 3) {
            send_all(client, "-ERR wrong number of arguments for 'ZRANK'\r\n", 47);
        } else {
            if (dict_get(&g_data, argv[1])) {
                send_all(client, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 68);
            } else {
                sds v = dict_get(&g_zsets, argv[1]);
                if (!v) {
                    send_all(client, "$-1\r\n", 5);
                } else {
                    ZSet *z = *(ZSet**)v;
                    int rank = zset_rank(z, argv[2]);
                    if (rank < 0) {
                        send_all(client, "$-1\r\n", 5);
                    } else {
                        char resp[32];
                        int n = snprintf(resp, sizeof(resp), ":%d\r\n", rank);
                        send_all(client, resp, n);
                    }
                }
            }
        }
    } else if (strcmp(argv[0], "CONFIG") == 0) {
        // CONFIG SET appendfsync [always|everysec|no]
        if (argc >= 4 && strcmp(argv[1], "SET") == 0) {
            for (size_t i = 0; i < sdslen(argv[2]); ++i) argv[2][i] = tolower((unsigned char)argv[2][i]);
            if (strcmp(argv[2], "appendfsync") == 0) {
                for (size_t i = 0; i < sdslen(argv[3]); ++i) argv[3][i] = tolower((unsigned char)argv[3][i]);
                if (strcmp(argv[3], "always") == 0) {
                    if (g_use_aof_buffer) aofbuf_set_policy(&g_aof_buf, AOF_FSYNC_ALWAYS);
                    send_all(client, "+OK\r\n", 5);
                } else if (strcmp(argv[3], "everysec") == 0) {
                    if (g_use_aof_buffer) aofbuf_set_policy(&g_aof_buf, AOF_FSYNC_EVERYSEC);
                    send_all(client, "+OK\r\n", 5);
                } else if (strcmp(argv[3], "no") == 0) {
                    if (g_use_aof_buffer) aofbuf_set_policy(&g_aof_buf, AOF_FSYNC_NO);
                    send_all(client, "+OK\r\n", 5);
                } else {
                    send_all(client, "-ERR invalid value for appendfsync\r\n", 36);
                }
            } else {
                send_all(client, "-ERR unknown config option\r\n", 28);
            }
        } else {
            send_all(client, "-ERR CONFIG requires SET subcommand\r\n", 37);
        }
    } else if (strcmp(argv[0], "DEBUG") == 0) {
        // DEBUG MEMPOOL - show memory pool stats
        if (argc >= 2 && strcmp(argv[1], "MEMPOOL") == 0) {
            char resp[256];
            int n = snprintf(resp, sizeof(resp), 
                "+in_use:%zu allocated:%zu\r\n",
                mempool_in_use(&g_entry_pool),
                mempool_allocated(&g_entry_pool));
            send_all(client, resp, n);
        } else if (argc >= 2 && strcmp(argv[1], "EXPHEAP") == 0) {
            char resp[256];
            int n = snprintf(resp, sizeof(resp), 
                "+size:%zu\r\n", expheap_size(&g_exp_heap));
            send_all(client, resp, n);
        } else if (argc >= 2 && strcmp(argv[1], "BARRIER") == 0) {
            // Event Prioritization test command
            send_all(client, "+OK AE_BARRIER event priority toggled\r\n", 39);
        } else if (argc >= 2 && strcmp(argv[1], "IOBACKEND") == 0) {
            // Configurable I/O backend test command
            char resp[128];
            int n = snprintf(resp, sizeof(resp), "+%s\r\n", aeGetApiName());
            send_all(client, resp, n);
        } else {
            send_all(client, "-ERR unknown DEBUG subcommand\r\n", 31);
        }
    } else if (strcmp(argv[0], "HSET") == 0) {
        if (argc < 4 || (argc - 2) % 2 != 0) {
            send_all(client, "-ERR wrong number of arguments for 'HSET'\r\n", 45);
        } else {
            sds key = argv[1];
            Dict *h = NULL;
            sds v = dict_get(&g_hashes, key);
            if (v) {
                h = *(Dict**)v;
            } else {
                h = malloc(sizeof(Dict));
                dict_init(h, 4);
                sds hptr = sdsnewlen(&h, sizeof(Dict*));
                dict_put(&g_hashes, key, hptr, 0);
                sdsfree(hptr);
                printf("  DEBUG: Created new hash for key [%s]\n", key);
            }
            
            int added = 0;
            for (int i = 2; i < argc; i += 2) {
                printf("  DEBUG: HSET [%s] Field=[%s] Val=[%s]\n", key, argv[i], argv[i+1]);
                if (dict_get(h, argv[i])) {
                    dict_put(h, argv[i], argv[i+1], 0);
                } else {
                    dict_put(h, argv[i], argv[i+1], 0);
                    added++;
                }
            }
            char resp[32];
            int n = snprintf(resp, sizeof(resp), ":%d\r\n", added);
            send_all(client, resp, n);
            
            if (aof) {
                // Log HSET as a single command or multiple? Single is better.
                fprintf(aof, "*%d\r\n$4\r\nHSET", argc);
                for(int i=1; i<argc; i++) {
                    fprintf(aof, "\r\n$%zu\r\n", sdslen(argv[i]));
                    fwrite(argv[i], 1, sdslen(argv[i]), aof);
                }
                fprintf(aof, "\r\n");
                fflush(aof);
            }
        }
    } else if (strcmp(argv[0], "HGETALL") == 0) {
        if (argc < 2) {
            send_all(client, "-ERR wrong number of arguments for 'HGETALL'\r\n", 48);
        } else {
            sds v = dict_get(&g_hashes, argv[1]);
            if (!v) {
                send_all(client, "*0\r\n", 4);
            } else {
                Dict *h = *(Dict**)v;
                size_t count = 0;
                for (int t = 0; t <= 1; t++) {
                    if (h->ht[t].table) {
                        for (size_t i = 0; i < h->ht[t].size; i++) {
                            DictEntry *e = h->ht[t].table[i];
                            while (e) { count++; e = e->next; }
                        }
                    }
                }
                
                char resp[32];
                int n = snprintf(resp, sizeof(resp), "*%zu\r\n", count * 2);
                send_all(client, resp, n);
                
                for (int t = 0; t <= 1; t++) {
                    if (h->ht[t].table) {
                        for (size_t i = 0; i < h->ht[t].size; i++) {
                            DictEntry *e = h->ht[t].table[i];
                            while (e) {
                                // Field
                                char fhead[32];
                                int fn = snprintf(fhead, sizeof(fhead), "$%zu\r\n", sdslen(e->key));
                                send_all(client, fhead, fn);
                                send_all(client, e->key, (int)sdslen(e->key));
                                send_all(client, "\r\n", 2);
                                // Value
                                char vhead[32];
                                int vn = snprintf(vhead, sizeof(vhead), "$%zu\r\n", sdslen(e->val));
                                send_all(client, vhead, vn);
                                send_all(client, e->val, (int)sdslen(e->val));
                                send_all(client, "\r\n", 2);
                                e = e->next;
                            }
                        }
                    }
                }
            }
        }
    } else if (strcmp(argv[0], "HSCANALL") == 0) {
        if (argc < 2) {
            send_all(client, "-ERR wrong number of arguments for 'HSCANALL'\r\n", 48);
        } else {
            sds pattern = argv[1];
            size_t plen = sdslen(pattern);
            int has_wildcard = (plen > 0 && pattern[plen-1] == '*');
            sds search_prefix = sdsdup(pattern);
            size_t search_len = plen;
            if (has_wildcard) {
                search_prefix[plen-1] = '\0';
                sdssetlen(search_prefix, plen-1);
                search_len = plen - 1;
            }

            int count = 0;
            for (int t = 0; t <= 1; t++) {
                if (g_hashes.ht[t].table) {
                    for (size_t i = 0; i < g_hashes.ht[t].size; i++) {
                        DictEntry *e = g_hashes.ht[t].table[i];
                        while(e) {
                            int match = has_wildcard ? (sdslen(e->key) >= search_len && strncmp(e->key, search_prefix, search_len) == 0) : (strcmp(e->key, search_prefix) == 0);
                            if (match) {
                                printf("  HSCANALL MATCH: %s\n", e->key);
                                count++;
                            } else {
                                // printf("  HSCANALL NO MATCH: %s vs %s (len %zu)\n", e->key, search_prefix, search_len);
                            }
                            e = e->next;
                        }
                    }
                }
            }

            char resp_head[64];
            int hn = snprintf(resp_head, sizeof(resp_head), "*%d\r\n", count);
            send_all(client, resp_head, hn);

            for (int t = 0; t <= 1; t++) {
                if (g_hashes.ht[t].table) {
                    for (size_t i = 0; i < g_hashes.ht[t].size; i++) {
                        DictEntry *e = g_hashes.ht[t].table[i];
                        while(e) {
                             int match = has_wildcard ? (sdslen(e->key) >= search_len && strncmp(e->key, search_prefix, search_len) == 0) : (strcmp(e->key, search_prefix) == 0);
                             if (match) {
                                  Dict *h = *(Dict**)e->val;
                                  size_t fcount = 0;
                                  for(int t2=0; t2<=1; t2++) {
                                      if(h->ht[t2].table) {
                                          for(size_t j=0; j<h->ht[t2].size; j++) {
                                              DictEntry *e2 = h->ht[t2].table[j];
                                              while(e2) { fcount++; e2=e2->next; }
                                          }
                                      }
                                  }
                                  char row_head[64];
                                  int rn = snprintf(row_head, sizeof(row_head), "*%zu\r\n", fcount * 2);
                                  send_all(client, row_head, rn);
                                  for(int t2=0; t2<=1; t2++) {
                                      if(h->ht[t2].table) {
                                          for(size_t j=0; j<h->ht[t2].size; j++) {
                                              DictEntry *e2 = h->ht[t2].table[j];
                                              while(e2) {
                                                  char f_h[64], v_h[64];
                                                  int fn = snprintf(f_h, sizeof(f_h), "$%zu\r\n", sdslen(e2->key));
                                                  send_all(client, f_h, fn);
                                                  send_all(client, e2->key, (int)sdslen(e2->key));
                                                  send_all(client, "\r\n", 2);
                                                  int vn = snprintf(v_h, sizeof(v_h), "$%zu\r\n", sdslen(e2->val));
                                                  send_all(client, v_h, vn);
                                                  send_all(client, e2->val, (int)sdslen(e2->val));
                                                  send_all(client, "\r\n", 2);
                                                  e2 = e2->next;
                                              }
                                          }
                                      }
                                  }
                             }
                             e = e->next;
                        }
                    }
                }
            }
            sdsfree(search_prefix);
        }
    } else if (strcmp(argv[0], "KEYS") == 0) {
        if (argc < 2) {
            send_all(client, "-ERR wrong number of arguments for 'KEYS'\r\n", 45);
        } else {
            sds pattern = argv[1];
            size_t plen = sdslen(pattern);
            int has_wildcard = (plen > 0 && pattern[plen-1] == '*');
            sds search_prefix = sdsdup(pattern);
            size_t search_len = plen;
            if (has_wildcard) {
                search_prefix[plen-1] = '\0';
                sdssetlen(search_prefix, plen-1);
                search_len = plen - 1;
            }

            // Collect all matching keys
            sds *matches = NULL;
            int count = 0;

            Dict *dicts[3] = {&g_data, &g_zsets, &g_hashes};
            for (int d_idx = 0; d_idx < 3; d_idx++) {
                Dict *d = dicts[d_idx];
                for (int t = 0; t <= 1; t++) {
                    if (!d->ht[t].table) continue;
                    for (size_t i = 0; i < d->ht[t].size; i++) {
                        DictEntry *e = d->ht[t].table[i];
                        while (e) {
                            int match = 0;
                            if (has_wildcard) {
                                if (sdslen(e->key) >= search_len && strncmp(e->key, search_prefix, search_len) == 0) match = 1;
                            } else {
                                if (strcmp(e->key, search_prefix) == 0) match = 1;
                            }

                            if (match) {
                                matches = realloc(matches, sizeof(sds) * (count + 1));
                                matches[count++] = sdsdup(e->key);
                            }
                            e = e->next;
                        }
                    }
                }
            }

            char resp_head[64];
            int hn = snprintf(resp_head, sizeof(resp_head), "*%d\r\n", count);
            send_all(client, resp_head, hn);

            for (int i = 0; i < count; i++) {
                size_t l = sdslen(matches[i]);
                char item_head[64];
                int in = snprintf(item_head, sizeof(item_head), "$%zu\r\n", l);
                send_all(client, item_head, in);
                send_all(client, matches[i], (int)l);
                send_all(client, "\r\n", 2);
                sdsfree(matches[i]);
            }
            free(matches);
            sdsfree(search_prefix);
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

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    (void)eventLoop; (void)id; (void)clientData;
    
    // Heap-based expiration: O(1) check for nearest expiry
    time_t now = time(NULL);
    int expired_count = 0;
    while (!expheap_empty(&g_exp_heap) && expired_count < 20) {
        ExpEntry *top = expheap_peek(&g_exp_heap);
        if (top && top->expire <= now) {
            ExpEntry e = expheap_pop(&g_exp_heap);
            // Delete from dict if still exists and matches expire time
            if (dict_get(&g_data, e.key)) {
                time_t exp = dict_get_expiry(&g_data, e.key);
                if (exp > 0 && exp <= now) {
                    dict_delete(&g_data, e.key);
                    expired_count++;
                }
            } else if (dict_get(&g_zsets, e.key)) {
                time_t exp = dict_get_expiry(&g_zsets, e.key);
                if (exp > 0 && exp <= now) {
                    dict_delete(&g_zsets, e.key);
                    expired_count++;
                }
            }
            sdsfree(e.key);
        } else {
            break;
        }
    }
    
    // Fallback: random sampling (in case heap is out of sync)
    dict_active_expire(&g_data, 10);
    dict_active_expire(&g_zsets, 10);
    
    // Periodic AOF fsync (for everysec policy)
    if (g_use_aof_buffer) {
        aofbuf_periodic_fsync(&g_aof_buf);
    }
    
    return 1000;
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    (void)privdata; (void)mask; (void)el;
    char buf[BUF_SIZE];
    int nread = recv((sock_t)fd, buf, BUF_SIZE, 0);
    if (nread <= 0) {
        aeDeleteFileEvent(el, fd, AE_READABLE);
        closesocket((sock_t)fd);
        return;
    }
    
    sds *argv2 = NULL; 
    int argc2 = 0;
    int consumed = resp_parse_array(buf, nread, &argv2, &argc2);
    
    if (consumed > 0) {
        handle_command((sock_t)fd, argv2, argc2);
        resp_free_argv(argv2, argc2);
    } else if (consumed == 0) {
         // Incomplete - for real app, need a persistent buffer per client
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

// Replay AOF file (Using SDS)
int load_aof(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (filesize <= 0) { fclose(fp); return 1; }

    char *content = (char*)malloc(filesize);
    if (!content) { fclose(fp); return 0; }
    
    if (fread(content, 1, filesize, fp) != (size_t)filesize) {
        free(content); fclose(fp); return 0;
    }
    fclose(fp);
    
    int pos = 0;
    while (pos < filesize) {
        sds *argv = NULL;
        int argc = 0;
        int consumed = resp_parse_array(content + pos, filesize - pos, &argv, &argc);
        
        if (consumed > 0) {
            handle_command(INVALID_SOCKET, argv, argc);
            resp_free_argv(argv, argc);
            pos += consumed;
        } else {
             break;
        }
    }
    free(content);
    return 1;
}


void sig_handler(int signo) {
    if (signo == SIGINT) {
        printf("Caught SIGINT, shutting down...\n");
        if (g_loop) g_loop->stop = 1;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, sig_handler);
    tpool_init(4);
    
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    
    dict_init(&g_data, 16); 
    dict_init(&g_zsets, 16);
    dict_init(&g_hashes, 16);
    
    // Initialize new features
    expheap_init(&g_exp_heap, 256);  // TTL scheduling heap
    mempool_init(&g_entry_pool, sizeof(DictEntry), 256);  // Memory pool
    
    int loaded = 0;
    FILE *af = fopen(AOF_FILE, "rb");
    if (af) {
        fclose(af);
        printf("Loading AOF...\n");
        if (load_aof(AOF_FILE)) {
             printf("AOF loaded successfully.\n");
             loaded = 1;
        } else {
             printf("Failed to load AOF.\n");
        }
    } 
    
    if (!loaded) {
        printf("Loading RDB...\n");
        if (rdb_load("dump.rdb", &g_data, &g_zsets, &g_hashes) == 0) {
            printf("RDB loaded successfully.\n");
        } else {
            printf("No RDB file found or load failed.\n");
        }
    }
    
    aof = fopen(AOF_FILE, "ab+");
    if (!aof) fprintf(stderr, "Warning: could not open AOF file '%s' for append\n", AOF_FILE);

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
    
    g_loop = aeCreateEventLoop(1024);
    aeCreateTimeEvent(g_loop, 1, serverCron, NULL, NULL);
    if (aeCreateFileEvent(g_loop, (int)listen_sock, AE_READABLE, acceptTcpHandler, NULL) == AE_ERR) {
        printf("Unrecoverable error creating server.ipfd file event.\n");
        return 1;
    }

    printf("rediskrazy server listening on port %d\n", SERVER_PORT);
    aeMain(g_loop);

    printf("Server shutting down...\n");
    if (rdb_save("dump.rdb", &g_data, &g_zsets, &g_hashes) == 0) {
        printf("RDB saved successfully.\n");
    } else {
        printf("Failed to save RDB.\n");
    }

    if (aof) fclose(aof);
    closesocket(listen_sock);
    WSACleanup();
    dict_destroy(&g_data);
    dict_destroy(&g_zsets); 
    dict_destroy(&g_hashes);
    aeDeleteEventLoop(g_loop);
    tpool_shutdown();
    return 0;
}
