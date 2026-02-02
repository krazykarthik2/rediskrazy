# 🚀 Redis Clone – Project Requirements

## Part 1️⃣ Core System Foundations

### 🧱 Step 0: Concurrent I/O Models (Foundations)
- [ ] Blocking I/O
- [ ] Non-blocking I/O
- [ ] I/O multiplexing
  - [x] select
  - [ ] poll
  - [ ] epoll / kqueue
- [ ] Performance comparison & tradeoffs

---

### 🧱 Step 1: Event Loop (Part 1)
- [x] Single-threaded event loop
- [x] File descriptor registration
- [x] Read events
- [ ] Write events
- [ ] Callback-based dispatch
- [x] Non-blocking sockets

---

### 🧱 Step 2: Event Loop (Part 2)
- [ ] Timer events
- [ ] Time-based callbacks
- [ ] Event prioritization
- [ ] Safe event removal
- [ ] Graceful shutdown handling

---

## Part 2️⃣ Networking & Server Core

### 🧱 Step 3: Key-Value Server (MVP)
- [x] TCP server
- [x] RESP protocol
- [x] Commands
  - [x] PING
  - [x] SET
  - [x] GET
- [x] In-memory key-value store
- [x] Single-threaded command execution
- [x] Client input/output buffers

---

## Part 3️⃣ Data Structures & Algorithms (Advanced Topics)

### 🧱 Step 4: Hashtables (Part 1)
- [x] Custom hash table
- [x] Collision handling
- [x] Load factor tracking
- [ ] Incremental rehashing

---

### 🧱 Step 5: Hashtables (Part 2)
- [x] Dynamic resizing
- [ ] Rehash pause avoidance
- [ ] Performance testing

---

### 🧱 Step 6: Data Serialization
- [ ] Binary-safe encoding
- [ ] Command serialization
- [ ] Snapshot encoding
- [ ] AOF compatibility

---

### 🧱 Step 7: Balanced Binary Tree
- [ ] AVL or Red-Black Tree
- [ ] Insert / delete / search
- [ ] Range queries
- [ ] Ordered traversal

---

### 🧱 Step 8: Sorted Set
- [ ] Score–member model
- [ ] Hash table backing
- [ ] Balanced tree ordering
- [ ] Commands
  - [ ] ZADD
  - [ ] ZRANGE
  - [ ] ZSCORE

---

## Part 4️⃣ Time, Expiration & Caching

### 🧱 Step 9: Timer and Timeout
- [ ] Timer manager
- [ ] Millisecond precision
- [ ] Nearest-timer lookup
- [ ] Event loop integration

---

### 🧱 Step 10: Cache Expiration with TTL
- [x] TTL per key
- [x] Lazy expiration
- [x] Active expiration cycle
- [ ] Scheduling (heap or time-wheel)
- [ ] Memory cleanup policies

---

## Part 5️⃣ Persistence

### 🧱 Step 11: Persistence
- [x] Append-Only File (AOF)
  - [x] Command logging
  - [ ] AOF rewrite
- [ ] Snapshotting (RDB-style)

---

## Part 6️⃣ Concurrency Enhancements

### 🧱 Step 12: Thread Pool
- [ ] Background disk I/O
- [ ] Async persistence
- [ ] Main-thread command execution
- [ ] Thread-safe job queue
- [ ] Work stealing (optional)

---

## Part 7️⃣ I/O Models (Final Integration)

### 🧱 Step 13: IO MODELS
- [ ] Configurable I/O backend
- [ ] Benchmarking
- [ ] Performance comparison
- [ ] Documentation

---

## 🧪 Usage
```bash
build.bat   # compile
run.bat     # start server and run tests
