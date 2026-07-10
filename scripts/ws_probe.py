#!/usr/bin/env python3
"""Minimal RFC6455 client to probe ale_monitor's new commands."""
import socket, base64, os, json, time, sys

HOST, PORT = "127.0.0.1", 8081

def handshake(s):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET / HTTP/1.1\r\n"
        "Host: localhost:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n" % (PORT, key)
    ).encode()
    s.sendall(req)
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    if b"101" not in buf.split(b"\r\n")[0]:
        raise RuntimeError("no 101: " + buf[:64].decode(errors="replace"))

def send_text(s, payload):
    data = payload.encode()
    mask = os.urandom(4)
    header = bytearray([0x81])
    n = len(data)
    if n < 126:
        header.append(0x80 | n)
    elif n < 65536:
        header += bytes([0x80 | 126, (n >> 8) & 0xff, n & 0xff])
    else:
        header += bytes([0x80 | 127]) + n.to_bytes(8, "big")
    header += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    s.sendall(bytes(header) + masked)

def recv_frame(s, timeout=3.0):
    s.settimeout(timeout)
    hdr = s.recv(2)
    if len(hdr) < 2:
        return None
    b0, b1 = hdr
    plen = b1 & 0x7F
    if plen == 126:
        ext = s.recv(2); plen = int.from_bytes(ext, "big")
    elif plen == 127:
        ext = s.recv(8); plen = int.from_bytes(ext, "big")
    if b1 & 0x80:  # masked
        s.recv(4)
    data = b""
    while len(data) < plen:
        data += s.recv(plen - len(data))
    return data.decode(errors="replace")

def cmd(s, msg):
    send_text(s, json.dumps(msg))
    # read until we see the matching reply id (skip async events)
    deadline = time.time() + 3
    while time.time() < deadline:
        f = recv_frame(s, timeout=1.0)
        if f is None:
            continue
        try:
            j = json.loads(f)
        except Exception:
            continue
        if j.get("id") == msg.get("id"):
            return j
    return None

def main():
    s = socket.create_connection((HOST, PORT), timeout=3)
    handshake(s)
    print("connected")

    r = cmd(s, {"id": 1, "cmd": "MON_CONFIG_GET"})
    print("MON_CONFIG_GET:", json.dumps(r) if r else "NO REPLY")

    r = cmd(s, {"id": 2, "cmd": "TIMING_SET", "scan_dwell_ms": 1500})
    print("TIMING_SET dwell=1500:", json.dumps(r) if r else "NO REPLY")

    r = cmd(s, {"id": 3, "cmd": "TIMING_GET"})
    print("TIMING_GET:", json.dumps(r) if r else "NO REPLY")

    r = cmd(s, {"id": 4, "cmd": "MON_FILTER", "filter": "ale"})
    print("MON_FILTER ale:", json.dumps(r) if r else "NO REPLY")

    r = cmd(s, {"id": 5, "cmd": "CHANNELS_LIST"})
    if r:
        chs = r.get("data", [])
        enabled = [c for c in chs if c.get("enabled")]
        disabled = [c for c in chs if not c.get("enabled")]
        ale = [c for c in chs if str(c.get("label","")).endswith("ALE")]
        sel = [c for c in chs if str(c.get("label","")).endswith("SEL")]
        print("CHANNELS_LIST: total=%d enabled=%d disabled=%d  (ALE=%d SEL=%d)"
              % (len(chs), len(enabled), len(disabled), len(ale), len(sel)))
        # verify: with filter=ale, all SEL channels must be disabled
        sel_disabled = all(not c.get("enabled") for c in sel)
        ale_enabled  = all(c.get("enabled") for c in ale)
        print("  all SEL disabled:", sel_disabled, "| all ALE enabled:", ale_enabled)
    else:
        print("CHANNELS_LIST: NO REPLY")

    r = cmd(s, {"id": 6, "cmd": "MON_MODE_OVERRIDE", "mode": "USB-D"})
    print("MON_MODE_OVERRIDE USB-D:", json.dumps(r) if r else "NO REPLY")
    r = cmd(s, {"id": 7, "cmd": "CHANNELS_LIST"})
    if r:
        modes = set(c.get("mode") for c in r.get("data", []) if c.get("enabled"))
        print("  enabled channel modes after override:", modes)
    s.close()

if __name__ == "__main__":
    main()