import socket
import time
import subprocess
import os
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER_EXE = os.path.join(BASE_DIR, "execs", "server.exe")
PORT = 6379
HOST = "127.0.0.1"

class TestRunner:
    def __init__(self):
        self.server_proc = None
        self.qp_proc = None
        self.sock = None

    def start_server(self):
        print("[Runner] Starting Server...")
        if os.path.exists(SERVER_EXE):
             exe = SERVER_EXE
        else:
             print(f"[Runner] Error: server executable not found at {SERVER_EXE}")
             sys.exit(1)
             
        self.server_proc = subprocess.Popen([exe], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        qp_exe = exe.replace("server.exe", "qp_server.exe")
        print(f"[Runner] Starting QP Server at {qp_exe}...")
        self.qp_proc = subprocess.Popen([qp_exe], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        time.sleep(1) # Wait for startup

    def stop_server(self):
        if self.qp_proc:
            print("[Runner] Stopping QP Server...")
            try:
                self.qp_proc.kill()
                self.qp_proc.wait(timeout=2)
            except:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.qp_proc.pid)], 
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.qp_proc = None

        if self.server_proc:
            print("[Runner] Stopping Server...")
            try:
                self.server_proc.kill()
                self.server_proc.wait(timeout=2)
            except:
                # Force kill if still alive
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.server_proc.pid)], 
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.server_proc = None
            time.sleep(1) # Extra time to release socket

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(2)
            self.sock.connect((HOST, PORT))
            return True
        except Exception as e:
            print(f"[Runner] Connection failed: {e}")
            return False

    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_cmd(self, *parts):
        buf = f"*{len(parts)}\r\n".encode()
        for p in parts:
            s_p = str(p)
            buf += f"${len(s_p)}\r\n{s_p}\r\n".encode()
        self.sock.sendall(buf)

    def read_resp(self):
        # Naive RESP reader: loop until we have a complete response?
        # For simple tests, we can just read with a timeout loop until satisfied or regex match?
        # Better: Basic RESP parser or just accumulate.
        self.sock.settimeout(1.0)
        data = b""
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk: break
                data += chunk
                # Heuristic: if we have balanced newlines or specific end patterns
                # Simple check: If we have data and it ends with \r\n, we *might* be done.
                # But ZRANGE returns multiple parts.
                # Let's just wait a tiny bit if we have data? No, slow.
                # Check for array length.
                if len(data) > 0:
                    # Parse type
                    if data.startswith(b"*"):
                        # Array: *N\r\n...
                        try:
                            # Find first CRLF
                            idx = data.find(b"\r\n")
                            if idx != -1:
                                count = int(data[1:idx])
                                # We need count elements.
                                # This requires parsing. Too complex for cheap test?
                                # Let's just try to read enough.
                                pass
                        except:
                            pass
                    
                    # If we seemingly have a full message (ends in \r\n), break?
                    # This is risky for split packets but better than recv(4096) once.
                    # For *2 array: *2\r\n$2\r\nm1\r\n$2\r\nm2\r\n
                    # Ends in \r\n.
                    if data.endswith(b"\r\n"):
                        # If it's an array, ensure we have enough lines?
                        # *N header is 1 line. Each element is 2 lines (length + content).
                        if data.startswith(b"*"):
                             lines = data.count(b"\r\n")
                             # Extract N
                             try:
                                 N = int(data[1:data.find(b"\r\n")])
                                 if lines >= 1 + N * 2:
                                     break
                             except:
                                 pass
                        else:
                            break
        except socket.timeout:
            pass
            
        return data.decode(errors='replace').strip()

    def assert_resp(self, actual, expected_substr, msg):
        if expected_substr in actual:
            print(f"[PASS] {msg}")
            return True
        else:
            print(f"[FAIL] {msg}. Expected '{expected_substr}' in '{actual}'")
            return False

    def run_functional_tests(self):
        print("\n=== Functional Tests ===")
        if not self.connect(): return
        
        # RESET DB
        self.send_cmd("FLUSHDB")
        self.assert_resp(self.read_resp(), "OK", "FLUSHDB")
        
        # 1. PING
        self.send_cmd("PING")
        self.assert_resp(self.read_resp(), "PONG", "PING Command")

        # 2. SET/GET Strings
        self.send_cmd("SET", "key_str", "hello")
        self.assert_resp(self.read_resp(), "OK", "SET String")
        
        self.send_cmd("GET", "key_str")
        self.assert_resp(self.read_resp(), "hello", "GET String")

        # 3. EXISTS
        self.send_cmd("EXISTS", "key_str")
        self.assert_resp(self.read_resp(), ":1", "EXISTS key_str")
        self.send_cmd("EXISTS", "no_key")
        self.assert_resp(self.read_resp(), ":0", "EXISTS no_key")

        # 4. INCR/DECR
        self.send_cmd("SET", "cnt", "10")
        self.read_resp()
        self.send_cmd("INCR", "cnt")
        self.assert_resp(self.read_resp(), ":11", "INCR")
        self.send_cmd("DECR", "cnt")
        self.assert_resp(self.read_resp(), ":10", "DECR")

        # 5. TTL (Basic)
        self.send_cmd("TTL", "cnt")
        self.assert_resp(self.read_resp(), ":-1", "TTL persistent")

        # 6. ZSet Basics
        self.send_cmd("ZADD", "myz", "10", "m1")
        self.assert_resp(self.read_resp(), ":1", "ZADD new")
        
        self.send_cmd("ZADD", "myz", "20", "m2")
        self.assert_resp(self.read_resp(), ":1", "ZADD new 2")
        
        self.send_cmd("ZSCORE", "myz", "m1")
        self.assert_resp(self.read_resp(), "10", "ZSCORE")
        
        self.send_cmd("ZRANGE", "myz", "0", "-1")
        res = self.read_resp()
        if "m1" in res and "m2" in res:
            print("[PASS] ZRANGE content")
        else:
            print(f"[FAIL] ZRANGE content. Got: {res}")

        # ZCARD
        self.send_cmd("ZCARD", "myz")
        self.assert_resp(self.read_resp(), ":2", "ZCARD")
        
        # ZRANK
        self.send_cmd("ZRANK", "myz", "m1")
        self.assert_resp(self.read_resp(), ":0", "ZRANK m1")
        self.send_cmd("ZRANK", "myz", "m2")
        self.assert_resp(self.read_resp(), ":1", "ZRANK m2")
        
        # ZREM
        self.send_cmd("ZREM", "myz", "m1")
        self.assert_resp(self.read_resp(), ":1", "ZREM")
        self.send_cmd("ZCARD", "myz")
        self.assert_resp(self.read_resp(), ":1", "ZCARD after ZREM")

        # APPEND
        self.send_cmd("SET", "app_key", "Hello")
        self.read_resp()
        self.send_cmd("APPEND", "app_key", " World")
        self.assert_resp(self.read_resp(), ":11", "APPEND length")
        self.send_cmd("GET", "app_key")
        self.assert_resp(self.read_resp(), "Hello World", "APPEND value")

        # SETNX
        self.send_cmd("SETNX", "nx_key", "first")
        self.assert_resp(self.read_resp(), ":1", "SETNX new key")
        self.send_cmd("SETNX", "nx_key", "second")
        self.assert_resp(self.read_resp(), ":0", "SETNX existing key")
        self.send_cmd("GET", "nx_key")
        self.assert_resp(self.read_resp(), "first", "SETNX value unchanged")

        # 7. Conflicts
        self.send_cmd("ZADD", "key_str", "1", "m")
        self.assert_resp(self.read_resp(), "WRONGTYPE", "ZADD on String")

        # 8. Expiration Tests
        print("    [Info] Testing ZSET Expiration...")
        self.send_cmd("ZADD", "z_expire", "10", "m1")
        self.read_resp()
        self.send_cmd("EXPIRE", "z_expire", "1")
        self.assert_resp(self.read_resp(), ":1", "EXPIRE z_expire 1s")
        
        # Immediate check (should exist)
        self.send_cmd("EXISTS", "z_expire")
        self.assert_resp(self.read_resp(), ":1", "Exists immediately")
        
        # Wait for expiration (second resolution requires > 1s + boundary margin)
        time.sleep(2.1)
        
        self.send_cmd("EXISTS", "z_expire")
        self.assert_resp(self.read_resp(), ":0", "Expired after sleep")

        # 9. Binary Safe Tests
        print("    [Info] Testing Binary Safety...")
        self.send_cmd("SET", "bin_key", "val\x00ue")
        self.assert_resp(self.read_resp(), "OK", "SET binary val")
        self.send_cmd("GET", "bin_key")
        self.assert_resp(self.read_resp(), "val\x00ue", "GET binary val")

        # 10. AOF Rewrite Tests
        print("    [Info] Testing AOF Rewrite...")
        self.send_cmd("SET", "rewrite_k", "1")
        self.read_resp()
        self.send_cmd("SET", "rewrite_k", "2") # Overwrite
        self.read_resp()
        self.send_cmd("BGREWRITEAOF")
        self.assert_resp(self.read_resp(), "OK", "BGREWRITEAOF command")
        
        # We need to verify persistence after restart
        self.disconnect()
        self.stop_server()
        time.sleep(1)
        self.start_server()
        self.connect()
        self.send_cmd("GET", "rewrite_k")
        self.assert_resp(self.read_resp(), "2", "Modified key persisted after rewrite")
        
        # 11. Shutdown Test
        print("    [Info] Testing SHUTDOWN...")
        self.send_cmd("SHUTDOWN")
        try:
             self.assert_resp(self.read_resp(), "OK", "SHUTDOWN response")
        except (ConnectionResetError, ConnectionAbortedError, ConnectionError):
             print("    [PASS] SHUTDOWN response (connection closed by server)")
        self.disconnect()
        # Wait for assertion of exit code
        try:
             self.server_proc.wait(timeout=2)
             if self.server_proc.returncode == 0:
                 print("    [PASS] Server exited cleanly with code 0")
             else:
                 print(f"    [FAIL] Server exited with code {self.server_proc.returncode}")
        except subprocess.TimeoutExpired:
             print("    [FAIL] Server did not exit in time")
             self.server_proc.kill()

    def test_new_features(self):
        print("\n=== New Feature Tests ===")
        self.start_server()
        self.connect()
        
        # Test CONFIG SET appendfsync
        print("    [Info] Testing CONFIG SET appendfsync...")
        self.send_cmd("CONFIG", "SET", "appendfsync", "always")
        self.assert_resp(self.read_resp(), "OK", "CONFIG SET appendfsync always")
        
        self.send_cmd("CONFIG", "SET", "appendfsync", "everysec")
        self.assert_resp(self.read_resp(), "OK", "CONFIG SET appendfsync everysec")
        
        self.send_cmd("CONFIG", "SET", "appendfsync", "no")
        self.assert_resp(self.read_resp(), "OK", "CONFIG SET appendfsync no")
        
        # Test DEBUG MEMPOOL
        print("    [Info] Testing DEBUG MEMPOOL...")
        self.send_cmd("DEBUG", "MEMPOOL")
        res = self.read_resp()
        if "in_use" in res and "allocated" in res:
            print("[PASS] DEBUG MEMPOOL")
        else:
            print(f"[FAIL] DEBUG MEMPOOL. Got: {res}")
        
        # Test DEBUG EXPHEAP
        print("    [Info] Testing DEBUG EXPHEAP...")
        self.send_cmd("DEBUG", "EXPHEAP")
        res = self.read_resp()
        if "size" in res:
            print("[PASS] DEBUG EXPHEAP")
        else:
            print(f"[FAIL] DEBUG EXPHEAP. Got: {res}")
            
        # Test expheap scheduling by adding key with TTL
        print("    [Info] Testing ExpHeap TTL scheduling...")
        self.send_cmd("SET", "ttl_test", "value", "EX", "1")
        self.read_resp()
        self.send_cmd("DEBUG", "EXPHEAP")
        res = self.read_resp()
        # Heap should have at least 1 entry now
        print(f"    [Info] ExpHeap after SET with TTL: {res}")
        
        # Test DEBUG BARRIER (Event Prioritization API)
        print("    [Info] Testing Event Prioritization API...")
        self.send_cmd("DEBUG", "BARRIER", "1")
        self.read_resp() # Accept whatever error or ok

        # Test CONFIG IOBACKEND
        print("    [Info] Testing Config I/O backend API...")
        self.send_cmd("DEBUG", "IOBACKEND")
        self.read_resp() # Usually will return the backend name
        
        # Test Hashes persistence fixes
        print("    [Info] Testing Hash Commands...")
        self.send_cmd("HSET", "testhash", "field", "val")
        self.read_resp()
        self.send_cmd("EXISTS", "testhash")
        self.assert_resp(self.read_resp(), ":1", "EXISTS sees Hash")
        self.send_cmd("DEL", "testhash")
        self.assert_resp(self.read_resp(), ":1", "DEL removes Hash")
        
        self.disconnect()
        self.stop_server()

    def run_benchmarks(self):
        print("\n=== Benchmarking & Performance ===")
        print("    [Info] Running SET/GET benchmark...")
        self.start_server()
        if not self.connect(): return
        
        start = time.time()
        n_ops = 5000
        for i in range(n_ops):
            self.send_cmd("SET", f"b_{i}", "val")
            self.read_resp()
        set_time = time.time() - start
        
        start = time.time()
        for i in range(n_ops):
            self.send_cmd("GET", f"b_{i}")
            self.read_resp()
        get_time = time.time() - start
        
        print(f"    [Bench] {n_ops} SETs: {set_time:.3f}s ({(n_ops/set_time) if set_time > 0 else 0:.0f} ops/sec)")
        print(f"    [Bench] {n_ops} GETs: {get_time:.3f}s ({(n_ops/get_time) if get_time > 0 else 0:.0f} ops/sec)")
        
        self.disconnect()
        self.stop_server()

    def test_thread_pool(self):
        print("\n=== Thread Pool Tests ===")
        self.start_server()
        self.connect()
        
        # 1. Submit background task (2000ms sleep)
        print("    [Info] Submitting BG_TASK 2000ms...")
        self.send_cmd("BG_TASK", "2000")
        self.assert_resp(self.read_resp(), "Background task submitted", "BG_TASK response")
        
        # 2. Immediately check responsiveness (should not block)
        start_time = time.time()
        self.send_cmd("PING")
        self.assert_resp(self.read_resp(), "PONG", "Server responsiveness during busy background")
        duration = time.time() - start_time
        
        if duration < 0.5:
            print(f"    [PASS] Server responded in {duration:.4f}s (Non-blocking)")
        else:
            print(f"    [FAIL] Server blocked for {duration:.4f}s")
            
        # Cleanup
        self.disconnect()
        # We don't strictly wait for BG task to finish to kill, but tpool_shutdown handles it.
        self.server_proc.terminate()
        self.server_proc.wait() 
             
    def run_persistence_tests(self):
        print("\n=== Persistence Tests ===")
        
        # Delete AOF to ensure RDB is loaded (AOF takes precedence)
        if os.path.exists("appendonly.aof"):
            os.remove("appendonly.aof")
        
        # Server was shut down by previous test, start fresh
        self.start_server()
        
        # Phase 1: Write and Save
        if not self.connect(): return
        
        print("[Persist] Writing data...")
        self.send_cmd("SET", "p_key", "saved_val")
        self.read_resp()
        self.send_cmd("ZADD", "p_zset", "5", "zmem")
        self.read_resp()
        
        print("[Persist] Sending SAVE...")
        self.send_cmd("SAVE")
        res = self.read_resp()
        self.assert_resp(res, "OK", "SAVE Command")
        
        self.disconnect()
        self.stop_server()
        
        # Phase 2: Restart and Verify
        print("[Persist] Restarting server...")
        self.start_server()
        if not self.connect(): return
        
        print("[Persist] Verifying data...")
        self.send_cmd("GET", "p_key")
        self.assert_resp(self.read_resp(), "saved_val", "Restore String")
        
        self.send_cmd("ZSCORE", "p_zset", "zmem")
        self.assert_resp(self.read_resp(), "5", "Restore ZSet")
        
        self.disconnect()

    def test_librediskrazy(self):
        print("\n=== librediskrazy Library Tests ===")
        from librediskrazy import Client, SimpleString, Integer, BulkString, Array, Nil, Error
        
        client = Client(HOST, PORT)
        try:
            client.connect()
            
            # PING
            resp = client.ping()
            self.assert_resp(resp.value, "PONG", "librediskrazy: ping()")
            if not isinstance(resp, SimpleString):
                print(f"[FAIL] Expected SimpleString object, got {type(resp)}")
            
            # SET & GET
            resp = client.set("lib_key", "lib_value")
            self.assert_resp(resp.value, "OK", "librediskrazy: set()")
            if not isinstance(resp, SimpleString):
                print(f"[FAIL] Expected SimpleString object, got {type(resp)}")

            resp = client.get("lib_key")
            self.assert_resp(resp.as_string(), "lib_value", "librediskrazy: get() as_string()")
            if resp != "lib_value":
                print(f"[FAIL] Equality comparison failed: {resp} != 'lib_value'")
            if not isinstance(resp, BulkString):
                print(f"[FAIL] Expected BulkString object, got {type(resp)}")

            # EXISTS
            resp = client.exists("lib_key")
            if not isinstance(resp, Integer) or resp != 1:
                print(f"[FAIL] Expected Integer(1) object, got {resp}")
            else:
                print("[PASS] librediskrazy: exists(lib_key) -> Integer(1)")

            # DEL
            resp = client.delete("lib_key")
            if not isinstance(resp, Integer) or resp != 1:
                print(f"[FAIL] Expected Integer(1) object after delete, got {resp}")
            else:
                print("[PASS] librediskrazy: delete(lib_key) -> Integer(1)")

            # GET Nil
            resp = client.get("lib_key")
            if not isinstance(resp, Nil) or resp.value is not None or resp != None:
                print(f"[FAIL] Expected Nil object, got {resp}")
            else:
                print("[PASS] librediskrazy: get(missing_key) -> Nil")

            # INCR / DECR
            client.set("lib_counter", "41")
            resp = client.incr("lib_counter")
            if not isinstance(resp, Integer) or resp != 42:
                print(f"[FAIL] Expected Integer(42), got {resp}")
            else:
                print("[PASS] librediskrazy: incr(lib_counter) -> Integer(42)")

            resp = client.decr("lib_counter")
            if not isinstance(resp, Integer) or resp != 41:
                print(f"[FAIL] Expected Integer(41), got {resp}")
            else:
                print("[PASS] librediskrazy: decr(lib_counter) -> Integer(41)")

            # ZSet Ops
            client.delete("lib_zset")
            resp = client.zadd("lib_zset", "1.5", "member1")
            if not isinstance(resp, Integer) or resp != 1:
                print(f"[FAIL] Expected Integer(1) for zadd, got {resp}")
            else:
                print("[PASS] librediskrazy: zadd() -> Integer(1)")

            resp = client.zadd("lib_zset", "2.5", "member2")
            resp = client.zscore("lib_zset", "member1")
            if not isinstance(resp, BulkString) or resp != "1.5":
                print(f"[FAIL] Expected score '1.5', got {resp}")
            else:
                print("[PASS] librediskrazy: zscore() -> BulkString('1.5')")

            resp = client.zrange("lib_zset", "0", "-1")
            if not isinstance(resp, Array):
                print(f"[FAIL] Expected Array response for zrange, got {type(resp)}")
            else:
                raw_list = resp.as_list()
                if b"member1" in raw_list and b"member2" in raw_list:
                    print("[PASS] librediskrazy: zrange() returned elements in list")
                else:
                    print(f"[FAIL] Expected members in zrange, got {raw_list}")

            # Error response check
            resp = client.execute("INVALID_COMMAND_NAME")
            if not isinstance(resp, Error) or not resp.is_error:
                print(f"[FAIL] Expected Error object, got {resp}")
            else:
                print(f"[PASS] librediskrazy: invalid command returned Error object with message: {resp.message}")

            # Query string check
            resp = client.query("SET query_key 'hello query'")
            if resp != "OK":
                print(f"[FAIL] client.query() failed to SET, got {resp}")
            resp = client.query("GET query_key")
            if resp != "hello query":
                print(f"[FAIL] client.query() failed to GET, got {resp}")
            else:
                print("[PASS] librediskrazy: client.query() syntax parsed and executed successfully")

        finally:
            client.close()

    def test_sql_interface(self):
        print("\n=== librediskrazy SQL Interface Tests ===")
        from librediskrazy import Client

        client = Client(HOST, PORT)
        try:
            client.connect()
            client.flushdb()

            res = client.execute_sql("CREATE DATABASE sqldb")
            self.assert_resp(str(res), "Status", "execute_sql: CREATE DATABASE")

            res = client.execute_sql("USE sqldb")
            self.assert_resp(str(res), "PONG", "execute_sql: USE")

            res = client.execute_sql("CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT)")
            self.assert_resp(str(res), "Status", "execute_sql: CREATE TABLE")

            res = client.execute_sql("INSERT INTO users VALUES ('10', 'Alice', '30')")
            self.assert_resp(str(res), "Status", "execute_sql: INSERT")

            res = client.execute_sql("SELECT * FROM users WHERE key = '10'")
            if res and isinstance(res, list) and len(res) > 0:
                print(f"[PASS] execute_sql: SELECT single row -> {res}")
                if res[0].get("name") == "Alice" and res[0].get("age") == "30":
                    print("[PASS] execute_sql: Row dictionary content matches")
                else:
                    print(f"[FAIL] execute_sql: Row dictionary content mismatch: {res}")
            else:
                print(f"[FAIL] execute_sql: SELECT single row failed: {res}")

            res = client.execute_sql("SELECT * FROM users")
            if res and len(res) >= 1:
                print("[PASS] execute_sql: SELECT all rows")
            else:
                print(f"[FAIL] execute_sql: SELECT all rows failed: {res}")

        finally:
            client.disconnect()

    def run_all(self):
        try:
            # Ensure any previous instance is killed? 
            # Ideally we just start.
            self.start_server()
            self.run_functional_tests()
            # Start server again for librediskrazy tests (functional tests calls SHUTDOWN)
            self.start_server()
            self.test_librediskrazy()
            self.test_sql_interface()
            self.stop_server()
            # Persistence test will stop and restart server
            self.run_persistence_tests()
            # Thread pool test will stop and restart server
            self.test_thread_pool()
            # New feature tests
            self.test_new_features()
            # Run benchmarks logic
            self.run_benchmarks()
        finally:
            self.stop_server()

if __name__ == "__main__":
    runner = TestRunner()
    runner.run_all()
