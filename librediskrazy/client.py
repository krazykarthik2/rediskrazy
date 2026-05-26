import socket
import shlex
import ctypes
import os

# DLL logic removed. Query translation now runs server-side on port 6380.


class Response:
    """Base class for all RESP responses"""
    def __init__(self, value):
        self._value = value

    @property
    def value(self):
        return self._value

    @property
    def is_error(self) -> bool:
        return False

    @property
    def is_ok(self) -> bool:
        return True

    def __eq__(self, other):
        if isinstance(other, Response):
            return self._value == other._value
        return self._value == other

    def __repr__(self):
        return f"{self.__class__.__name__}({self._value!r})"


class SimpleString(Response):
    """Represents a RESP Simple String (+OK\\r\\n)"""
    pass


class Error(Response):
    """Represents a RESP Error (-ERR ...\\r\\n)"""
    @property
    def is_error(self) -> bool:
        return True

    @property
    def is_ok(self) -> bool:
        return False

    @property
    def message(self) -> str:
        return self._value


class Integer(Response):
    """Represents a RESP Integer (:10\\r\\n)"""
    pass


class BulkString(Response):
    """Represents a RESP Bulk String ($5\\r\\nvalue\\r\\n)"""
    def __eq__(self, other):
        val = self._value
        if val is None:
            return other is None
        if isinstance(other, Response):
            other_val = other._value
        else:
            other_val = other

        # Handle decoding safely when comparing bytes to strings
        if isinstance(val, bytes) and isinstance(other_val, str):
            return val.decode('utf-8', errors='replace') == other_val
        if isinstance(val, str) and isinstance(other_val, bytes):
            return val == other_val.decode('utf-8', errors='replace')
        return val == other_val

    def as_string(self, encoding='utf-8', errors='replace') -> str:
        if self._value is None:
            return ""
        return self._value.decode(encoding, errors)


class Nil(Response):
    """Represents a RESP Nil ($-1\\r\\n or *-1\\r\\n)"""
    def __init__(self):
        super().__init__(None)

    def __eq__(self, other):
        if isinstance(other, Response):
            return other._value is None
        return other is None


class Array(Response):
    """Represents a RESP Array (*2\\r\\n...)"""
    def __len__(self):
        return len(self._value)

    def __getitem__(self, index):
        return self._value[index]

    def __iter__(self):
        return iter(self._value)

    def as_list(self):
        """Converts array elements recursively to their raw values"""
        result = []
        for item in self._value:
            if isinstance(item, Array):
                result.append(item.as_list())
            elif isinstance(item, Response):
                result.append(item.value)
            else:
                result.append(item)
        return result


def encode_cmd(*parts) -> bytes:
    """Encodes command parts into RESP array format"""
    buf = f"*{len(parts)}\r\n".encode()
    for p in parts:
        if isinstance(p, bytes):
            s_p = p
        else:
            s_p = str(p).encode('utf-8')
        buf += f"${len(s_p)}\r\n".encode() + s_p + b"\r\n"
    return buf


class RESPParser:
    """Recursively parses RESP elements from a TCP socket"""
    def __init__(self, sock):
        self.sock = sock
        self.buffer = bytearray()

    def read_until_crlf(self) -> bytes:
        while True:
            idx = self.buffer.find(b"\r\n")
            if idx != -1:
                line = self.buffer[:idx]
                del self.buffer[:idx + 2]
                return bytes(line)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed by server")
            self.buffer.extend(chunk)

    def read_exact(self, n: int) -> bytes:
        while len(self.buffer) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed by server")
            self.buffer.extend(chunk)
        data = self.buffer[:n]
        del self.buffer[:n]
        return bytes(data)

    def parse_response(self) -> Response:
        # Skip any leading invalid characters/leftover bytes (like null bytes)
        while True:
            if not self.buffer:
                chunk = self.sock.recv(4096)
                if not chunk:
                    raise ConnectionError("Connection closed by server")
                self.buffer.extend(chunk)
            
            if self.buffer[0:1] in (b"+", b"-", b":", b"$", b"*"):
                break
            # Discard invalid byte
            del self.buffer[:1]

        type_byte = self.buffer[0:1]
        del self.buffer[:1]

        if type_byte == b"+":
            line = self.read_until_crlf()
            return SimpleString(line.decode('utf-8', errors='replace'))
        elif type_byte == b"-":
            line = self.read_until_crlf()
            return Error(line.decode('utf-8', errors='replace'))
        elif type_byte == b":":
            line = self.read_until_crlf()
            return Integer(int(line))
        elif type_byte == b"$":
            line = self.read_until_crlf()
            length = int(line)
            if length == -1:
                return Nil()
            data = self.read_exact(length)
            self.read_exact(2)  # consume trailing \r\n
            return BulkString(data)
        elif type_byte == b"*":
            line = self.read_until_crlf()
            length = int(line)
            if length == -1:
                return Nil()
            elements = []
            for _ in range(length):
                elements.append(self.parse_response())
            return Array(elements)
        else:
            # Fallback for inline commands or custom string responses
            line = self.read_until_crlf()
            return SimpleString((type_byte + line).decode('utf-8', errors='replace'))


class Client:
    """High-level abstraction for interacting with rediskrazy database"""
    def __init__(self, host="127.0.0.1", port=6379):
        self.host = host
        self.port = port
        self.sock = None
        self.parser = None

    def connect(self):
        if self.sock is not None:
            return
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(2.0)
        self.sock.connect((self.host, self.port))
        self.parser = RESPParser(self.sock)

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None
            self.parser = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def execute(self, *args) -> Response:
        """Sends command parts, reads RESP, and returns parsed Response object"""
        self.connect()
        data = encode_cmd(*args)
        self.sock.sendall(data)
        return self.parser.parse_response()

    def query(self, cmd_str) -> Response:
        """Gives out a query by parsing a command string and returning Response object"""
        parts = shlex.split(cmd_str)
        return self.execute(*parts)

    # Database and general command methods
    def ping(self, message=None) -> Response:
        if message is not None:
            return self.execute("PING", message)
        return self.execute("PING")

    def set(self, key, value, ex=None) -> Response:
        if ex is not None:
            return self.execute("SET", key, value, "EX", ex)
        return self.execute("SET", key, value)

    def get(self, key) -> Response:
        return self.execute("GET", key)

    def delete(self, *keys) -> Response:
        return self.execute("DEL", *keys)

    def exists(self, key) -> Response:
        return self.execute("EXISTS", key)

    def incr(self, key) -> Response:
        return self.execute("INCR", key)

    def decr(self, key) -> Response:
        return self.execute("DECR", key)

    def ttl(self, key) -> Response:
        return self.execute("TTL", key)

    def expire(self, key, seconds) -> Response:
        return self.execute("EXPIRE", key, seconds)

    def flushdb(self) -> Response:
        return self.execute("FLUSHDB")

    def save(self) -> Response:
        return self.execute("SAVE")

    def bgrewriteaof(self) -> Response:
        return self.execute("BGREWRITEAOF")

    def shutdown(self) -> Response:
        try:
            return self.execute("SHUTDOWN")
        except (ConnectionError, socket.error):
            # Server immediately drops connection on shutdown, normal
            return SimpleString("OK")

    # Sorted Set command methods
    def zadd(self, key, score, member) -> Response:
        return self.execute("ZADD", key, score, member)

    def zrange(self, key, start, stop) -> Response:
        return self.execute("ZRANGE", key, start, stop)

    def zscore(self, key, member) -> Response:
        return self.execute("ZSCORE", key, member)

    def zrem(self, key, member) -> Response:
        return self.execute("ZREM", key, member)

    def zcard(self, key) -> Response:
        return self.execute("ZCARD", key)

    def zrank(self, key, member) -> Response:
        return self.execute("ZRANK", key, member)

    # Hash methods
    def hset(self, key, field, value) -> Response:
        return self.execute("HSET", key, field, value)

    # Debug and extension methods
    def bg_task(self, ms) -> Response:
        return self.execute("BG_TASK", ms)

    def config_set(self, parameter, value) -> Response:
        return self.execute("CONFIG", "SET", parameter, value)

    def debug_mempool(self) -> Response:
        return self.execute("DEBUG", "MEMPOOL")

    def debug_expheap(self) -> Response:
        return self.execute("DEBUG", "EXPHEAP")

    def debug_barrier(self, val) -> Response:
        return self.execute("DEBUG", "BARRIER", val)

    def debug_iobackend(self) -> Response:
        return self.execute("DEBUG", "IOBACKEND")

    def disconnect(self):
        """Disconnect from the database server"""
        self.close()

    def execute_sql(self, sql_query) -> list:
        """Sends raw SQL to the C-based Query Processor on port 6380 and returns list of dictionaries"""
        import json
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        try:
            sock.connect((self.host, 6380))
            # Send query terminated by CRLF
            cmd = (sql_query.strip() + "\r\n").encode('utf-8')
            sock.sendall(cmd)
            
            # Read RESP response using client parser
            parser = RESPParser(sock)
            resp = parser.parse_response()
            
            if isinstance(resp, Error):
                raise ValueError(resp.message)
            elif isinstance(resp, BulkString):
                return json.loads(resp.as_string())
            else:
                raise RuntimeError(f"Unexpected response type from query processor: {type(resp)}")
        finally:
            sock.close()



if __name__ == "__main__":
    import sys

    # ANSI escape sequences for premium colored terminal output
    BLUE = "\033[1;34m"
    GREEN = "\033[1;32m"
    RED = "\033[1;31m"
    CYAN = "\033[1;36m"
    YELLOW = "\033[1;33m"
    RESET = "\033[0m"

    print(f"{BLUE}===================================================={RESET}")
    print(f"{BLUE}          librediskrazy Python Wrapper CLI          {RESET}")
    print(f"{BLUE}===================================================={RESET}")
    
    host = "127.0.0.1"
    port = 6379
    print(f"Connecting to {host}:{port}...")
    
    client = Client(host, port)
    def print_sql_table(rows):
        if not rows:
            print("Empty set")
            return
        
        headers = list(rows[0].keys())
        widths = {h: len(h) for h in headers}
        for row in rows:
            for h in headers:
                val_str = str(row[h]) if row[h] is not None else "NULL"
                if len(val_str) > widths[h]:
                    widths[h] = len(val_str)
                    
        sep = "+" + "+".join("-" * (widths[h] + 2) for h in headers) + "+"
        
        print(sep)
        print("|" + "|".join(f" {h.ljust(widths[h])} " for h in headers) + "|")
        print(sep)
        
        for row in rows:
            row_str_parts = []
            for h in headers:
                val = row[h]
                val_str = str(val) if val is not None else "NULL"
                row_str_parts.append(f" {val_str.ljust(widths[h])} ")
            print("|" + "|".join(row_str_parts) + "|")
            
        print(sep)

    try:
        client.connect()
        print(f"{GREEN}[SUCCESS] Connected to server.{RESET}")
        print("Type commands (e.g., 'SET key val', 'GET key' or 'SELECT * FROM table'). Type 'exit' to quit.\n")
        
        while True:
            try:
                line = input(f"{BLUE}redis-py> {RESET}")
            except (KeyboardInterrupt, EOFError):
                print()
                break
            
            line = line.strip()
            if not line:
                continue
            if line.lower() == 'exit':
                break
            
            # Determine if it's an SQL command
            first_word = line.split(None, 1)[0].lower()
            sql_keywords = {'select', 'insert', 'delete', 'update', 'create', 'use', 'list', 'drop', 'show', 'desc', 'describe'}
            
            if first_word in sql_keywords:
                try:
                    res = client.execute_sql(line)
                    print_sql_table(res)
                except Exception as e:
                    print(f"{RED}(error) SQL error: {e}{RESET}")
                continue
                
            try:
                resp = client.query(line)
                # Formatted response output based on RESP types
                if isinstance(resp, SimpleString):
                    print(f"{GREEN}{resp.value}{RESET}")
                elif isinstance(resp, Error):
                    print(f"{RED}(error) {resp.message}{RESET}")
                elif isinstance(resp, Integer):
                    print(f"{CYAN}(integer) {resp.value}{RESET}")
                elif isinstance(resp, BulkString):
                    val = resp.value
                    if val is None:
                        print(f"{YELLOW}(nil){RESET}")
                    else:
                        print(f"{GREEN}{repr(resp.as_string())}{RESET}")
                elif isinstance(resp, Nil):
                    print(f"{YELLOW}(nil){RESET}")
                elif isinstance(resp, Array):
                    lst = resp.as_list()
                    for idx, item in enumerate(lst):
                        print(f"{CYAN}{idx+1}){RESET} {repr(item)}")
                else:
                    print(resp)
            except Exception as e:
                print(f"{RED}(error) Client error: {e}{RESET}")
    except Exception as e:
        print(f"{RED}[ERROR] Connection failed: {e}{RESET}")
    finally:
        client.close()
        print(f"\n{BLUE}Disconnected. Goodbye!{RESET}")

