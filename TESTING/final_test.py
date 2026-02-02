
import socket
import time
import os

PORT = 6379
HOST = "127.0.0.1"
RESULT_FILE = "final_results.txt"

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
                    if resp.endswith(b"\r\n"): 
                         if resp.startswith(b"+") or resp.startswith(b":") or resp.startswith(b"-"): break
                         if resp.startswith(b"$"):
                             if resp.startswith(b"$-1\r\n"): break
                             if resp.count(b"\r\n") >= 2: break
                except socket.timeout:
                    break
            decoded = resp.decode('utf-8', errors='ignore')
            log(f"RES: {decoded.strip()}")
            return decoded
        except Exception as e:
            log(f"IO Error: {e}")
            return None

    def assert_resp(self, resp, expected_substr):
        if resp and expected_substr in resp: return True
        return False

    def test_all(self):
        log("--- TEST START ---")
        if not self.connect(): return
        
        # 1. SET/GET
        if not self.assert_resp(self.send_raw("*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$3\r\nval\r\n"), "+OK"): log("FAIL SET")
        if not self.assert_resp(self.send_raw("*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n"), "val"): log("FAIL GET")
        
        # 2. INCR/DECR
        self.send_raw("*3\r\n$3\r\nSET\r\n$1\r\nn\r\n$2\r\n10\r\n")
        if not self.assert_resp(self.send_raw("*2\r\n$4\r\nINCR\r\n$1\r\nn\r\n"), ":11"): log("FAIL INCR")
        if not self.assert_resp(self.send_raw("*2\r\n$4\r\nDECR\r\n$1\r\nn\r\n"), ":10"): log("FAIL DECR")
        
        # 3. TTL
        self.send_raw("*5\r\n$3\r\nSET\r\n$1\r\nt\r\n$1\r\nv\r\n$2\r\nEX\r\n$2\r\n20\r\n")
        resp = self.send_raw("*2\r\n$3\r\nTTL\r\n$1\r\nt\r\n")
        if not resp or ":" not in resp or int(resp.strip()[1:]) <= 0: log("FAIL TTL")
        
        # 4. FLUSHDB
        if not self.assert_resp(self.send_raw("*1\r\n$7\r\nFLUSHDB\r\n"), "+OK"): log("FAIL FLUSHDB")
        if not self.assert_resp(self.send_raw("*2\r\n$6\r\nEXISTS\r\n$1\r\nn\r\n"), ":0"): log("FAIL EXISTS after FLUSH")
        
        log("--- TEST END ---")
        self.disconnect()

if __name__ == "__main__":
    runner = TestRunner()
    runner.test_all()
