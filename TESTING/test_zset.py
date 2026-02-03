import socket
import time
import os

PORT = 6379
HOST = "127.0.0.1"
RESULT_FILE = "test_zset_results.txt"

if os.path.exists(RESULT_FILE):
    try:
        os.remove(RESULT_FILE)
    except:
        pass

def log(msg):
    print(msg)
    with open(RESULT_FILE, "a") as f:
        f.write(msg + "\n")

class TestRunner:
    def __init__(self):
        self.results = {}
        self.sock = None

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(2)
            self.sock.connect((HOST, PORT))
            return True
        except Exception as e:
            log(f"Connection failed: {e}")
            return False

    def disconnect(self):
        if self.sock:
            self.sock.close()

    def send_raw(self, data):
        try:
            log(f"REQ: {data.strip()}")
            self.sock.sendall(data.encode('utf-8'))
            
            self.sock.settimeout(0.5) 
            resp = b""
            while True:
                try:
                    chunk = self.sock.recv(4096)
                    if not chunk: break
                    resp += chunk
                    # Simple heuristic: stop if we see \r\n at end and it looks complete?
                    # For ZRANGE *N, we need to read multiple lines.
                    if resp.startswith(b"*"):
                        # Count items
                        try:
                             lines = resp.split(b"\r\n")
                             if len(lines) > 0 and lines[0].startswith(b"*"):
                                 count = int(lines[0][1:])
                                 # We expect count * 2 + 1 lines (header + 2 lines per bulk) roughly?
                                 # wait, bulk string is header + data.
                                 # *2\r\n$3\r\nval\r\n$3\r\nval2\r\n
                                 # lines: ["*2", "$3", "val", "$3", "val2", ""]
                                 # Number of \r\n should be 1 + count * 2 ?
                                 # No, bulk string format: $L\r\nDATA\r\n (2 CRLFs per item).
                                 # Array header: *N\r\n (1 CRLF).
                                 # Total CRLFs = 1 + N * 2.
                                 if resp.count(b"\r\n") >= 1 + count * 2:
                                      break
                        except:
                             pass
                    elif resp.endswith(b"\r\n"): 
                        break
                except socket.timeout:
                    break
            
            decoded = resp.decode('utf-8', errors='ignore')
            log(f"RES: {decoded.strip()}")
            return decoded
        except Exception as e:
            log(f"IO Error: {e}")
            return None

    def assert_resp(self, resp, expected_substr):
        if resp and expected_substr in resp:
            return True
        return False
        
    def test_zset_basics(self):
        log("--- Testing ZSET Basics ---")
        if not self.connect(): return False
        
        success = True
        
        # 1. ZADD myzset 10 m1 -> :1
        if not self.assert_resp(self.send_raw("*4\r\n$4\r\nZADD\r\n$6\r\nmyzset\r\n$2\r\n10\r\n$2\r\nm1\r\n"), ":1"):
            success = False; log("FAIL: ZADD new")

        # 2. ZADD myzset 20 m2 -> :1
        if not self.assert_resp(self.send_raw("*4\r\n$4\r\nZADD\r\n$6\r\nmyzset\r\n$2\r\n20\r\n$2\r\nm2\r\n"), ":1"):
            success = False; log("FAIL: ZADD new 2")

        # 3. ZADD myzset 15 m1 -> :0 (update)
        if not self.assert_resp(self.send_raw("*4\r\n$4\r\nZADD\r\n$6\r\nmyzset\r\n$2\r\n15\r\n$2\r\nm1\r\n"), ":0"):
             success = False; log("FAIL: ZADD update")

        # 4. ZSCORE myzset m1 -> 15
        if not self.assert_resp(self.send_raw("*3\r\n$6\r\nZSCORE\r\n$6\r\nmyzset\r\n$2\r\nm1\r\n"), "15"):
             success = False; log("FAIL: ZSCORE m1")
             
        # 5. ZRANGE myzset 0 -1 -> m1, m2 (since 15 < 20)
        resp = self.send_raw("*4\r\n$6\r\nZRANGE\r\n$6\r\nmyzset\r\n$1\r\n0\r\n$2\r\n-1\r\n")
        if "m1" not in resp or "m2" not in resp:
            success = False; log("FAIL: ZRANGE content")
        if resp.find("m1") > resp.find("m2"):
            success = False; log("FAIL: ZRANGE order (expected m1...m2)")
            
        self.disconnect()
        self.results['basics'] = 'valid' if success else 'failed'
        return success
        
    def test_zset_conflicts(self):
        log("--- Testing ZSET Conflicts ---")
        if not self.connect(): return False
        success = True
        
        # 1. SET k v
        self.send_raw("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n")
        
        # 2. ZADD k 1 m -> WRONGTYPE
        if not self.assert_resp(self.send_raw("*4\r\n$4\r\nZADD\r\n$1\r\nk\r\n$1\r\n1\r\n$1\r\nm\r\n"), "WRONGTYPE"):
            success = False; log("FAIL: ZADD on String")
            
        # 3. DEL k
        self.send_raw("*2\r\n$3\r\nDEL\r\n$1\r\nk\r\n")
        
        # 4. ZADD k 1 m -> OK
        if not self.assert_resp(self.send_raw("*4\r\n$4\r\nZADD\r\n$1\r\nk\r\n$1\r\n1\r\n$1\r\nm\r\n"), ":1"):
             success = False; log("FAIL: ZADD after DEL")
             
        # 5. SET k v -> OK (Overwrite ZSet)
        if not self.assert_resp(self.send_raw("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$2\r\nv2\r\n"), "+OK"):
             success = False; log("FAIL: SET overwrite ZSet")
             
        # 6. GET k -> v
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nGET\r\n$1\r\nk\r\n"), "v2"):
             success = False; log("FAIL: GET after overwrite")
             
        self.disconnect()
        self.results['conflicts'] = 'valid' if success else 'failed'
        return success

    def print_report(self):
        log("\n" + "="*30)
        log("       TEST REPORT       ")
        log("="*30)
        for k, v in self.results.items():
            log(f"{k}: {v}")
        log("="*30)

if __name__ == "__main__":
    t = TestRunner()
    t.test_zset_basics()
    t.test_zset_conflicts()
    t.print_report()
