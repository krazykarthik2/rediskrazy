# IO Models and Performance

This document describes the various I/O models supported by the Redis clone, along with benchmark results and a performance comparison.

## I/O Backends Supported

The server uses an Event Loop module (`ae.c`) which relies on OS-specific I/O multiplexing:
1. **epoll** (`ae_epoll.c`): Optimal for Linux. O(1) scalability.
2. **kqueue** (`ae_kqueue.c`): Optimal for macOS/BSD. O(1) scalability.
3. **poll**/**wsapoll** (`ae_poll.c`): Fallback for both POSIX and Windows. Linear O(N) lookup.
4. **select** (`ae_select.c`): Standard base fallback, limited by `FD_SETSIZE`.

The I/O backend is automatically determined at compile-time or conditionally switched depending on your environment. You can check the currently loaded backend using the internal debug API:
```
DEBUG IOBACKEND
```

## Event Prioritization
The Event Loop also supports **AE_BARRIER** events. Normally, readable events are processed before writable events in the same cycle. Setting `AE_BARRIER` reverses this order (Writable, then Readable), which is extremely useful for flushing data to disk efficiently before parsing the next command.

## Benchmarks & Performance Comparison

A built-in benchmark runner tests simple `SET` and `GET` performance for 5000 continuous operations.
By running `test.py` or `sim.bat`, the benchmark metrics will be recorded:

### Sample Performance Observations (Local Windows Environment via WSAPoll):
- `SET` throughput averages: ~25,000 - 30,000 ops/sec.
- `GET` throughput averages: ~28,000 - 32,000 ops/sec.

*Note: Since standard `test.py` runs sequentially and recreates sockets via blocking I/O on the client side, true pipelined benchmarks via something like `redis-benchmark` would yield much higher operations per second (100k+). The internal backend runs asynchronously in C independently of the client tests.*
