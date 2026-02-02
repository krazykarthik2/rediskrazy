
import socket
import time
import os

PORT = 6379
HOST = "127.0.0.1"
RESULT_FILE = "test_results.txt"
AOF_FILE = "../appendonly.aof"

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
            
            self.sock.settimeout(0.5) 
            resp = b""
            while True:
                try:
                    chunk = self.sock.recv(4096)
                    if not chunk: break
                    resp += chunk
                    if resp.endswith(b"\r\n"): 
                         if resp.startswith(b"+") or resp.startswith(b":") or resp.startswith(b"-"):
                             break
                         if resp.startswith(b"$"):
                             if resp.startswith(b"$-1\r\n"):
                                 break
                             if resp.count(b"\r\n") >= 2:
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

    def test_numeric_ops(self):
        log("--- Testing Numeric Ops (INCR/DECR) ---")
        if not self.connect(): return False
        
        success = True
        
        # 1. SET cnt 10
        self.send_raw("*3\r\n$3\r\nSET\r\n$3\r\ncnt\r\n$2\r\n10\r\n")
        
        # 2. INCR cnt -> :11
        if not self.assert_resp(self.send_raw("*2\r\n$4\r\nINCR\r\n$3\r\ncnt\r\n"), ":11"):
             success = False
             log("FAIL: INCR existing (10->11)")

        # 3. DECR cnt -> :10
        if not self.assert_resp(self.send_raw("*2\r\n$4\r\nDECR\r\n$3\r\ncnt\r\n"), ":10"):
             success = False
             log("FAIL: DECR existing (11->10)")

        # 4. INCR newkey -> :1
        self.send_raw("*2\r\n$3\r\nDEL\r\n$6\r\nnewinc\r\n")
        if not self.assert_resp(self.send_raw("*2\r\n$4\r\nINCR\r\n$6\r\nnewinc\r\n"), ":1"):
             success = False
             log("FAIL: INCR new key")
             
        self.disconnect()
        self.results['numeric'] = 'valid' if success else 'failed'
        return success

    def test_ttl_ops(self):
        log("--- Testing TTL ---")
        if not self.connect(): return False
        success = True
        
        # 1. expired key (set with EX 10)
        self.send_raw("*5\r\n$3\r\nSET\r\n$3\r\nexp\r\n$3\r\nval\r\n$2\r\nEX\r\n$2\r\n10\r\n")
        
        # TTL exp -> should be > 0 (e.g. 9 or 10)
        resp = self.send_raw("*2\r\n$3\r\nTTL\r\n$3\r\nexp\r\n") 
        if not resp or not resp.startswith(":"):
            success = False
            log(f"FAIL: TTL format error: {resp}")
        else:
            try:
                val = int(resp.strip()[1:])
                if val <= 0: 
                    success = False
                    log(f"FAIL: TTL expected > 0, got {val}")
            except:
                success = False
                log(f"FAIL: TTL parse error: {resp}")

        # 2. no expiry key
        self.send_raw("*3\r\n$3\r\nSET\r\n$5\r\nnoexp\r\n$1\r\nv\r\n")
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nTTL\r\n$5\r\nnoexp\r\n"), ":-1"):
             success = False
             log("FAIL: TTL on persistent key")

        # 3. missing key
        self.send_raw("*2\r\n$3\r\nDEL\r\n$7\r\nmissing\r\n")
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nTTL\r\n$7\r\nmissing\r\n"), ":-2"):
             success = False
             log("FAIL: TTL on missing key")

        self.disconnect()
        self.results['ttl'] = 'valid' if success else 'failed'
        return success

    def test_flush(self):
        log("--- Testing FLUSHDB ---")
        if not self.connect(): return False
        success = True
        
        # Setup
        self.send_raw("*3\r\n$3\r\nSET\r\n$2\r\nk1\r\n$1\r\nv\r\n")
        
        # Flush
        if not self.assert_resp(self.send_raw("*1\r\n$7\r\nFLUSHDB\r\n"), "+OK"):
            success = False
            log("FAIL: FLUSHDB command")
            
        # Verify empty
        if not self.assert_resp(self.send_raw("*2\r\n$6\r\nEXISTS\r\n$2\r\nk1\r\n"), ":0"):
            success = False
            log("FAIL: Key exists after FLUSHDB")
            
        self.disconnect()
        self.results['flush'] = 'valid' if success else 'failed'
        return success

    def test_persistence(self):
        log("--- Testing Persistence (AOF Verification) ---")
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
        log("DEBUG: Running test_protocol")
        runner.test_protocol()
        log("DEBUG: Running test_db_ops")
        runner.test_db_ops()
        log("DEBUG: Running test_numeric_ops")
        runner.test_numeric_ops()
        log("DEBUG: Running test_ttl_ops")
        runner.test_ttl_ops()
        log("DEBUG: Running test_flush")
        runner.test_flush()
        log("DEBUG: Running test_persistence")
        runner.test_persistence()
        runner.print_report()
    except Exception as e:
        log(f"CRASH: {e}")

if __name__ == "__main__":
    main()
