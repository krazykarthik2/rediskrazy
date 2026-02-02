rediskrazy - minimal Redis clone (C)

Features implemented in this demo:
- TCP server (single-threaded event loop using select)
- Minimal RESP parser (arrays of bulk strings)
- Commands: PING, SET, GET
- In-memory hashmap store
- TTL (expiration) with lazy deletion and periodic cleanup
- Append-only file (AOF) for persistence: `appendonly.aof`

Files:
- `server.c` - the Redis-like server implementation
- `client.c` - a tiny demo client that sends PING, SET, GET
- `build.bat` - builds the project; object files and logs are placed in `./temp/rediskrazy_build`
- `run.bat` - builds, starts server in a new window, runs demo client

Build & run (Windows, with gcc in PATH such as MinGW-w64):
1. Open `cmd.exe` in the project folder.
2. Run:

```
build.bat
```

3. To run the demo (build + start server + run client):

```
run.bat
```

Notes & assumptions:
- This is intentionally small and not production-ready.
- Requires a Windows build of `gcc` (MinGW or similar). The `build.bat` links `ws2_32` for Winsock.
- The RESP parser is minimal and may reject malformed inputs.
- AOF format is written as RESP arrays for `SET` commands.

Next steps (suggested):
- Add more data types (lists, hashes)
- Improve RESP parsing and support inline commands
- Implement graceful shutdown and AOF rewrite
- Add config flags for port, AOF path, etc.
