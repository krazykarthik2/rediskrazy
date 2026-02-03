import socket
import time
import subprocess
import os

SERVER_EXE = "execs/server.exe"

def send_cmd(s, parts):
    buf = f"*{len(parts)}\r\n".encode()
    for p in parts:
        buf += f"${len(str(p))}\r\n{p}\r\n".encode()
    s.sendall(buf)
    
def read_resp(s):
    try:
        data = s.recv(4096)
        print(f"RES: {data.decode(errors='replace').strip()}")
        return data.decode(errors='replace').strip()
    except Exception as e:
        print(f"RES: Error/Timeout {e}")
        return ""

def test_persistence():
    print("--- Starting Server (1st Run) ---")
    proc = subprocess.Popen([SERVER_EXE], shell=False)
    time.sleep(2) # Wait for startup
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 6379))
        
        print("SET k1 v1")
        send_cmd(s, ["SET", "k1", "v1"])
        read_resp(s)
        
        print("ZADD z1 10 m1")
        send_cmd(s, ["ZADD", "z1", "10", "m1"])
        read_resp(s)
        
        print("SAVE")
        send_cmd(s, ["SAVE"])
        res = read_resp(s)
        if "OK" not in res:
            print("FAIL: SAVE command failed")
        
        s.close()
    except Exception as e:
        print(f"FAIL: {e}")
    
    print("--- Killing Server ---")
    proc.terminate()
    proc.wait()
    time.sleep(1)
    
    print("--- Starting Server (2nd Run - Restore) ---")
    proc = subprocess.Popen([SERVER_EXE], shell=False)
    time.sleep(2)
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 6379))
        
        print("GET k1 (expect v1)")
        send_cmd(s, ["GET", "k1"])
        read_resp(s)
        
        print("ZSCORE z1 m1 (expect 10)")
        send_cmd(s, ["ZSCORE", "z1", "m1"])
        read_resp(s)
        
        s.close()
    except Exception as e:
        print(f"FAIL: {e}")
        
    proc.terminate()
    print("Test Complete")

if __name__ == "__main__":
    test_persistence()
