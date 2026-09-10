"""Real loopback TLS handshakes; temporary certificates and no player data."""
import base64
import hashlib
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

with tempfile.TemporaryDirectory(prefix="ctr-tls-test-") as directory:
    root = Path(directory)
    for name, san in (("matching", "IP:127.0.0.2"), ("wrong", "DNS:unrelated.invalid"),
                      ("untrusted", "IP:127.0.0.2")):
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(root / (name + ".key")), "-out", str(root / (name + ".pem")),
                        "-days", "1", "-subj", "/CN=fixture", "-addext", "subjectAltName=" + san],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # Repeating success/failure creates fresh transport instances, as AP reconnect does.
    for certificate, trust, expected in (("matching", "matching", 0), ("wrong", "wrong", 1),
                                          ("matching", "untrusted", 1), ("matching", "matching", 0),
                                          ("wrong", "wrong", 1)):
        listener = socket.socket()
        listener.bind(("127.0.0.2", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(root / (certificate + ".pem"), root / (certificate + ".key"))
        def serve():
            try:
                raw, _ = listener.accept()
                raw.settimeout(5)
                with context.wrap_socket(raw, server_side=True) as connection:
                    request = b""
                    while b"\r\n\r\n" not in request and len(request) < 16384:
                        chunk = connection.recv(4096)
                        if not chunk:
                            return
                        request += chunk
                    key = next(line.split(b":", 1)[1].strip() for line in request.split(b"\r\n")
                               if line.lower().startswith(b"sec-websocket-key:"))
                    accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
                    connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                    connection.recv(64)
            except (OSError, StopIteration):
                pass
            finally:
                listener.close()
        server = threading.Thread(target=serve)
        server.start()
        result = subprocess.run([sys.argv[1], f"wss://127.0.0.2:{port}", str(root / (trust + ".pem"))],
                                capture_output=True, text=True, timeout=10)
        server.join(timeout=10)
        assert result.returncode == expected, (certificate, trust, result.returncode, result.stdout, result.stderr)
        print(f"PASS certificate={certificate} trust={trust} expected={expected}")
