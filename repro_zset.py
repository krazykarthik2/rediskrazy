import socket
import time

def send_cmd(s, parts):
    buf = f"*{len(parts)}\r\n".encode()
    for p in parts:
        buf += f"${len(str(p))}\r\n{p}\r\n".encode()
    s.sendall(buf)
    
def read_resp(s):
    # Simple reader, doesn't handle all RESP perfectly but enough for these tests
    # We just want to print what we get
    s.settimeout(2)
    try:
        data = s.recv(4096)
        print(f"RES: {data.decode(errors='replace')}")
    except Exception as e:
        print(f"RES: Error/Timeout {e}")

def run():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 6379))
    except Exception as e:
        print(f"Could not connect: {e}")
        return

    print("--- ZADD New ---")
    send_cmd(s, ["ZADD", "myzset", "10", "m1"])
    read_resp(s)

    print("--- ZADD New 2 ---")
    send_cmd(s, ["ZADD", "myzset", "20", "m2"])
    read_resp(s)
    
    print("--- Conflict ---")
    send_cmd(s, ["SET", "k", "v"])
    read_resp(s)
    send_cmd(s, ["ZADD", "k", "1", "m"])
    read_resp(s)
    send_cmd(s, ["DEL", "k"])
    read_resp(s)
    send_cmd(s, ["ZADD", "k", "1", "m"])
    read_resp(s)
    send_cmd(s, ["SET", "k", "v2"])
    read_resp(s)
    send_cmd(s, ["GET", "k"])
    read_resp(s)
    
    s.close()

if __name__ == "__main__":
    run()
