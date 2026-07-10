#!/usr/bin/env python3
"""Self-contained: start ale_monitor on a spare port, enumerate audio devices,
try AUDIO_OPEN RX-only, report, and tear down. Does not touch port 8081."""
import subprocess, socket, base64, os, json, time, sys

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "ale_monitor", "Debug", "ale_monitor.exe")
EXE = os.path.abspath(EXE)
PORT = 8083

def wait_port(port, timeout=8):
    dl = time.time() + timeout
    while time.time() < dl:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.2)
    return False

def hs(s, port):
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET / HTTP/1.1\r\nHost: localhost:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (port, key)).encode())
    b = b""
    while b"\r\n\r\n" not in b:
        b += s.recv(4096)
    if b"101" not in b.split(b"\r\n")[0]:
        raise RuntimeError("handshake failed: " + b[:120].decode(errors="replace"))

def send(s, p):
    d = p.encode(); m = os.urandom(4)
    s.sendall(bytes([0x81, 0x80 | len(d)]) + m + bytes(b ^ m[i % 4] for i, b in enumerate(d)))

def recv(s, t=2):
    s.settimeout(t); h = s.recv(2)
    if len(h) < 2: return None
    _, b1 = h; pl = b1 & 0x7F
    if pl == 126: pl = int.from_bytes(s.recv(2), "big")
    if b1 & 0x80: s.recv(4)
    d = b""
    while len(d) < pl: d += s.recv(pl - len(d))
    return d.decode(errors="replace")

def cmd(s, m, port):
    send(s, json.dumps(m)); dl = time.time() + 3
    while time.time() < dl:
        f = recv(s, 1)
        if not f: continue
        try: j = json.loads(f)
        except: continue
        if j.get("id") == m.get("id"): return j
    return None

def main():
    p = subprocess.Popen([EXE, "--port", str(PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        if not wait_port(PORT):
            print("monitor did not start; stderr:", p.stderr.read(500).decode(errors="replace")); return
        s = socket.create_connection(("127.0.0.1", PORT), timeout=3); hs(s, PORT)
        r = cmd(s, {"id": 1, "cmd": "AUDIO_DEVICES"}, PORT)
        if not r:
            print("AUDIO_DEVICES: NO REPLY"); return
        ins = r.get("inputs", [])
        print("AUDIO_DEVICES inputs:", len(ins))
        for d in ins[:8]: print("  ", d)
        if not ins:
            print("no input devices enumerated"); return
        # Mirror the GUI: strip the "IN:"/"OUT:" prefix (WASAPI resolve_device
        # matches the bare device name). Try each input until one opens.
        import re
        strip = lambda s: re.sub(r'^(IN:|OUT:)\s*', '', s)
        for d in ins:
            dev = strip(d)
            r = cmd(s, {"id": 2, "cmd": "AUDIO_OPEN", "in": dev}, PORT)
            print("AUDIO_OPEN '%s':" % dev, json.dumps(r) if r else "NO REPLY")
            if r and r.get("ok"):
                r = cmd(s, {"id": 3, "cmd": "AUDIO_LEVEL"}, PORT)
                print("AUDIO_LEVEL:", json.dumps(r) if r else "NO REPLY")
                cmd(s, {"id": 4, "cmd": "AUDIO_CLOSE"}, PORT)
                break
        r = cmd(s, {"id": 3, "cmd": "AUDIO_LEVEL"}, PORT)
        print("AUDIO_LEVEL:", json.dumps(r) if r else "NO REPLY")
        cmd(s, {"id": 4, "cmd": "AUDIO_CLOSE"}, PORT)
        s.close()
    finally:
        p.terminate()
        try: p.wait(timeout=3)
        except: p.kill()
        err = p.stderr.read(400).decode(errors="replace")
        if "WASAPI" in err or "audio" in err.lower():
            print("--- monitor stderr (audio) ---"); print(err)

if __name__ == "__main__":
    main()