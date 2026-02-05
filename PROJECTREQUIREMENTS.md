# 🚀 Redis Clone – Project Requirements

## Part 1️⃣ Core System Foundations

### 🧱 Step 0: Concurrent I/O Models (Foundations)
- [x] Blocking I/O
- [x] Non-blocking I/O
- [x] I/O multiplexing
  - [x] select - ae_select.c
  - [x] poll - ae_poll.c (WSAPoll on Windows)
  - [x] epoll / kqueue - ae_epoll.c (Linux), ae_kqueue.c (BSD/macOS)

---

### 🧱 Step 1: Event Loop (Part 1)
- [x] Single-threaded event loop
- [x] File descriptor registration
- [x] Read events
- [x] Write events
- [x] Callback-based dispatch
- [x] Non-blocking sockets

---

### 🧱 Step 2: Event Loop (Part 2)
- [x] Timer events
- [x] Time-based callbacks
- [ ] Event prioritization
- [x] Safe event removal
- [x] Graceful shutdown handling

---

## Part 2️⃣ Networking & Server Core

### 🧱 Step 3: Key-Value Server (MVP)
- [x] TCP server
- [x] RESP protocol
- [x] Commands
  - [x] PING
  - [x] SET
  - [x] GET
  - [x] DEL
  - [x] EXISTS
  - [x] INCR / DECR
  - [x] APPEND
  - [x] SETNX
  - [x] TTL
  - [x] EXPIRE
  - [x] FLUSHDB
  - [x] SAVE
  - [x] BGREWRITEAOF
  - [x] SHUTDOWN
- [x] In-memory key-value store
- [x] Single-threaded command execution
- [x] Client input/output buffers

---

## Part 3️⃣ Data Structures & Algorithms (Advanced Topics)

### 🧱 Step 4: Hashtables (Part 1)
- [x] Custom hash table
- [x] Collision handling
- [x] Load factor tracking
- [x] Incremental rehashing

---

### 🧱 Step 5: Hashtables (Part 2)
- [x] Dynamic resizing
- [x] Rehash pause avoidance (Incremental Rehashing)
- [x] Performance testing

---

### 🧱 Step 6: Data Serialization
- [x] Binary-safe encoding
- [x] Simple Dynamic Strings (SDS) Implementation
- [x] Command serialization
- [x] Snapshot encoding
- [x] AOF compatibility

---

### 🧱 Step 7: Balanced Binary Tree
- [x] AVL or Red-Black Tree
- [x] Insert / delete / search
- [x] Range queries
- [x] Ordered traversal

---

### 🧱 Step 8: Sorted Set
- [x] Score–member model
- [x] Hash table backing
- [x] Balanced tree ordering
- [x] Commands
  - [x] ZADD
  - [x] ZRANGE
  - [x] ZSCORE
  - [x] ZREM
  - [x] ZCARD
  - [x] ZRANK

---

## Part 4️⃣ Time, Expiration & Caching

### 🧱 Step 9: Timer and Timeout
- [x] Timer manager
- [x] Millisecond precision
- [x] Nearest-timer lookup
- [x] Event loop integration

---

### 🧱 Step 10: Cache Expiration with TTL
- [x] TTL per key
- [x] Lazy expiration
- [x] Active expiration cycle
- [x] Scheduling (heap or time-wheel) - ExpHeap min-heap implementation
- [x] Memory cleanup policies - Memory pool allocator

---

## Part 5️⃣ Persistence

### 🧱 Step 11: Persistence
- [x] Append-Only File (AOF)
  - [x] Command logging
  - [x] AOF rewrite
  - [x] fsync policy - AofBuffer with configurable policies (always/everysec/no)
  - [x] Background rewrite - Thread pool integration ready
  - [x] Write batching - AofBuffer implementation
- [x] Snapshotting (RDB-style)
  - [ ] Fork-based snapshotting (N/A on Windows)

---

## Part 6️⃣ Concurrency Enhancements

### 🧱 Step 12: Thread Pool
- [x] Background disk I/O (via `BG_TASK` proof-of-concept)
- [x] Async persistence (Infrastructure ready)
- [x] Main-thread command execution
- [x] Thread-safe job queue
- [ ] Work stealing (optional)
- [x] Graceful Shutdown (Signal Handling & `SHUTDOWN` command)

---

## Part 7️⃣ I/O Models (Final Integration)

### 🧱 Step 13: IO MODELS
- [ ] Configurable I/O backend
- [ ] Benchmarking
- [ ] Performance comparison
- [ ] Documentation

---

## 🧱 Additional Memory & Storage Enhancements
- [x] Memory pooling - MemPool implementation
- [x] Allocator strategy - Fixed-size block allocator
- [x] Defragmentation - mempool_defrag() placeholder

---

## 🧪 Usage
```bash
build.bat   # compile
run.bat     # start server and run tests
```