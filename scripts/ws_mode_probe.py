#!/usr/bin/env python3
"""Self-contained: verify the mode override is global + sticky across filter changes."""
import subprocess, socket, base64, os, json, time, re

EXE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "ale_monitor", "Debug", "ale_monitor.exe"))
PORT = 8084

def wait_port(port, timeout=8):
    dl = time.time() + timeout
    while time.time() < dl:
        try: socket.create_connection(("127.0.0.1", port), timeout=0.5).close(); return True
        except OSError: time.sleep(0.2)
    return False

def hs(s):
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET / HTTP/1.1\r\nHost: localhost:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (PORT, key)).encode())
    b = b""
    while b"\r\n\r\n" not in b: b += s.recv(4096)
    if b"101" not in b.split(b"\r\n")[0]:
        print("HS RESP:", b[:200].decode(errors="replace")); raise SystemExit(2)

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

_id = 0
def cmd(s, m):
    global _id; _id += 1; m["id"] = _id
    send(s, json.dumps(m)); dl = time.time() + 3
    while time.time() < dl:
        f = recv(s, 1)
        if not f: continue
        try: j = json.loads(f)
        except: continue
        if j.get("id") == m["id"]: return j
    return None

def modes(s):
    r = cmd(s, {"cmd": "CHANNELS_LIST"})
    chs = r.get("data", []) if r else []
    return chs

def report(tag, chs):
    en = [c for c in chs if c.get("enabled")]
    dis = [c for c in chs if not c.get("enabled")]
    en_modes = {c["mode"] for c in en}
    dis_modes = {c["mode"] for c in dis}
    print("%-28s total=%d en=%d dis=%d | enabled_modes=%s disabled_modes=%s" %
          (tag, len(chs), len(en), len(dis), en_modes, dis_modes))
    return en_modes, dis_modes

def main():
    p = subprocess.Popen([EXE, "--port", str(PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not wait_port(PORT): print("monitor did not start"); return
        time.sleep(1.0)
        s = socket.create_connection(("127.0.0.1", PORT), timeout=3); hs(s)
        cmd(s, {"cmd": "MON_FILTER", "filter": "all"})
        report("filter=all (file modes)", modes(s))
        cmd(s, {"cmd": "MON_MODE_OVERRIDE", "mode": "USB-D"})
        report("override USB-D", modes(s))
        cmd(s, {"cmd": "MON_FILTER", "filter": "ale"})
        em, dm = report("filter=ale (sticky?)", modes(s))
        ok_ale = em == {"USB-D"} and dm == {"USB-D"}
        cmd(s, {"cmd": "MON_FILTER", "filter": "sel"})
        em2, dm2 = report("filter=sel (sticky?)", modes(s))
        ok_sel = em2 == {"USB-D"} and dm2 == {"USB-D"}
        cmd(s, {"cmd": "MON_FILTER", "filter": "all"})
        em3, dm3 = report("filter=all (sticky?)", modes(s))
        ok_all = em3 == {"USB-D"} and dm3 == {"USB-D"}
        print("\nRESULT: all channels adhere to USB-D across filter changes:", ok_ale and ok_sel and ok_all)
        s.close()
    finally:
        p.terminate()
        try: p.wait(timeout=3)
        except: p.kill()

if __name__ == "__main__":
    main()