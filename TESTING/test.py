
import socket
import time
import os

PORT = 6379
HOST = "127.0.0.1"
RESULT_FILE = "test_results.txt"
AOF_FILE = "../appendonly.aof" # Relative to TESTING/

if os.path.exists(RESULT_FILE):
    os.remove(RESULT_FILE)

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
            log(f"Connected to {HOST}:{PORT}")
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
            
            # Robust read: read until we have enough data or timeout
            # For this simple test, we expect short responses. 
            # We'll read chunks until we stop getting data or get a reasonable amount
            
            self.sock.settimeout(0.5) 
            resp = b""
            while True:
                try:
                    chunk = self.sock.recv(4096)
                    if not chunk: break
                    resp += chunk
                    # Heuristic: if we have newlines and it looks complete, stop
                    # (Simple check for this test case)
                    if resp.endswith(b"\r\n"): 
                         # Better heuristic for RESP:
                         # Simple String (+): 1 CRLF
                         # Bulk String ($): 2 CRLFs (header + data)
                         # Integer (:): 1 CRLF
                         if resp.startswith(b"+") or resp.startswith(b":") or resp.startswith(b"-"):
                             break
                         if resp.startswith(b"$"):
                             # Bulk string: need 2 CRLFs, UNLESS it's $-1 (null) which has 1
                             if resp.startswith(b"$-1\r\n"):
                                 break
                             if resp.count(b"\r\n") >= 2:
                                 break
                         # Array (*): Complicated, let's rely on timeout if we can't parse easily
                         # or just wait for timeout if unsure
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

    def test_protocol(self):
        log("--- Testing Protocol (PING/SET/GET) ---")
        if not self.connect(): return False
        
        success = True
        
        # PING
        if not self.assert_resp(self.send_raw("*1\r\n$4\r\nPING\r\n"), "+PONG"):
            success = False
            log("FAIL: PING")
        
        # SET
        if not self.assert_resp(self.send_raw("*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nisvalid\r\n"), "+OK"):
            success = False
            log("FAIL: SET")
            
        # GET
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nGET\r\n$5\r\nmykey\r\n"), "isvalid"):
            success = False
            log("FAIL: GET")

        self.disconnect()
        self.results['multitesting'] = 'valid' if success else 'failed'
        return success

    def test_persistence(self):
        log("--- Testing Persistence (AOF Verification) ---")
        # Check if AOF exists and contains the key we just set
        if not os.path.exists(AOF_FILE):
             log(f"AOF File not found at {AOF_FILE}")
             self.results['persistence'] = 'failed (no file)'
             return False
        
        try:
            with open(AOF_FILE, 'r') as f:
                content = f.read()
                if "mykey" in content and "isvalid" in content:
                    self.results['persistence'] = 'valid'
                    return True
                else:
                    log("AOF content missing expected key/value")
                    self.results['persistence'] = 'failed (data missing)'
                    return False
        except Exception as e:
            log(f"Error reading AOF: {e}")
            self.results['persistence'] = 'error'
            return False

    def test_db_ops(self):
        log("--- Testing DB Ops (DEL/EXISTS) ---")
        if not self.connect(): return False
        
        success = True
        
        # 1. SET key
        self.send_raw("*3\r\n$3\r\nSET\r\n$4\r\ntest\r\n$3\r\nval\r\n")
        
        # 2. EXISTS test -> :1
        if not self.assert_resp(self.send_raw("*2\r\n$6\r\nEXISTS\r\n$4\r\ntest\r\n"), ":1"):
             success = False
             log("FAIL: EXISTS on existing key")

        # 3. EXISTS non-existent -> :0
        if not self.assert_resp(self.send_raw("*2\r\n$6\r\nEXISTS\r\n$6\r\nnokey1\r\n"), ":0"):
             success = False
             log("FAIL: EXISTS on missing key")

        # 4. DEL test -> :1
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nDEL\r\n$4\r\ntest\r\n"), ":1"):
             success = False
             log("FAIL: DEL existing key")

        # 5. EXISTS after DEL -> :0
        if not self.assert_resp(self.send_raw("*2\r\n$6\r\nEXISTS\r\n$4\r\ntest\r\n"), ":0"):
             success = False
             log("FAIL: EXISTS after DEL")

        # 6. DEL non-existent -> :0
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nDEL\r\n$4\r\ntest\r\n"), ":0"):
             success = False
             log("FAIL: DEL missing key")

        self.disconnect()
        self.results['db_ops'] = 'valid' if success else 'failed'
        return success

    def print_report(self):
        log("\n" + "="*30)
        log("       TEST REPORT       ")
        log("="*30)
        for k, v in self.results.items():
            log(f"{k}: {v}")
        log("="*30)

def main():
    try:
        runner = TestRunner()
        
        # 1. Test Basic Commands
        log("DEBUG: Calling test_protocol")
        runner.test_protocol()
        
        # 2. Test DB Ops
        log("DEBUG: Calling test_db_ops")
        runner.test_db_ops()
        
        # 3. Test Persistence (by checking AOF side-effect)
        log("DEBUG: Calling test_persistence")
        runner.test_persistence()
        
        # Report
        runner.print_report()
    except Exception as e:
        with open("crash.log", "w") as f:
            import traceback
            traceback.print_exc(file=f)

if __name__ == "__main__":
    main()
