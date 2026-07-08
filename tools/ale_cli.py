#!/usr/bin/env python3
"""
ale_cli.py — Command-line interface for openALE (WebSocket/JSON).

Connects to a running openALE instance and provides an interactive
REPL for all ALEController commands.  Async events (link established,
AMD received, state changes) are printed as they arrive, interleaved
with the prompt.

Usage:
    python tools/ale_cli.py --port 8765          # interactive REPL
    python tools/ale_cli.py --port 8765 --cmd status   # one-shot, then exit

Requirements:
    pip install websockets
"""

import argparse
import asyncio
import json
import sys

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package not found.  Install with:", file=sys.stderr)
    print("         pip install websockets", file=sys.stderr)
    sys.exit(1)


# ── Command help ──────────────────────────────────────────────────────────────

HELP = """\
Station control
  status              Current SM state, self address, link active
  scan                Start channel scanning
  available           Stop scanning, go IDLE (listen only)
  sound               Send a sounding on the current channel
  stop                Emergency stop — all TX off immediately

Call handling
  call <addr>         Initiate individual call to <addr>
  accept              Accept an incoming call
  reject              Reject an incoming call (TWAS)
  terminate           Terminate the active link

Messaging
  amd <text>          Queue AMD orderwire text for the next outgoing call

Configuration (read-only queries)
  channels            List configured channels
  contacts            List contacts / address book
  lqa                 Show LQA table (all recorded link-quality entries)
  self                List own addresses
  vfo                 Current VFO frequency, mode, tune step, PTT

Meta
  help                Show this help
  quit / exit         Disconnect and exit
"""


# ── Command → bridge message ──────────────────────────────────────────────────

def build_message(line: str) -> dict | None:
    """Map a user input line to a bridge JSON payload.  Returns None on error."""
    parts = line.strip().split(None, 1)
    if not parts:
        return None
    verb = parts[0].lower()
    rest = parts[1].strip() if len(parts) > 1 else ""

    simple = {
        "status":    "STATUS",
        "scan":      "SCAN",
        "available": "AVAILABLE",
        "sound":     "SOUND",
        "stop":      "EMERGENCY_STOP",
        "accept":    "ACCEPT",
        "reject":    "REJECT",
        "terminate": "TERMINATE",
        "channels":  "CHANNELS_LIST",
        "contacts":  "CONTACTS_LIST",
        "lqa":       "LQA_LIST",
        "self":      "SELF_ADDR_LIST",
        "vfo":       "VFO_GET",
    }

    if verb in simple:
        return {"cmd": simple[verb]}

    if verb == "call":
        if not rest:
            print("  usage: call <addr>")
            return None
        return {"cmd": "CALL", "addr": rest.upper()}

    if verb == "amd":
        if not rest:
            print("  usage: amd <text>")
            return None
        return {"cmd": "AMD", "text": rest.upper()}

    if verb in ("help", "?", "h"):
        print(HELP)
        return None

    print(f"  unknown command '{verb}' — type 'help' for a list")
    return None


# ── Reply / event rendering ───────────────────────────────────────────────────

def _fmt_reply(msg: dict) -> str:
    ok  = msg.get("ok", True)
    out = ["OK" if ok else "FAIL"]

    # Generic field rendering (order matters for readability)
    for key, label in [
        ("state",        "state"),
        ("self",         "self"),
        ("link_active",  "link"),
        ("freq_hz",      "freq_hz"),
        ("mode",         "mode"),
        ("tune_step_hz", "step_hz"),
        ("ptt",          "ptt"),
        ("connected",    "rig"),
        ("status",       None),
        ("error",        "ERROR"),
    ]:
        if key not in msg:
            continue
        val = msg[key]
        if key == "link_active":
            val = "yes" if val else "no"
        if key == "connected":
            val = "connected" if val else "disconnected"
        lbl = label if label else key
        out.append(f"{lbl}={val}")

    if "data" in msg:
        data = msg["data"]
        if not data:
            out.append("(empty)")
        else:
            lines = [" ".join(out)]
            for item in data:
                lines.append("  " + json.dumps(item, ensure_ascii=False))
            return "\n".join(lines)

    return "  " + "  ".join(out)


def _fmt_event(msg: dict) -> str:
    ev = msg.get("event", "?")
    if ev == "state":
        return f"\n  ── {msg.get('value', '?')}\n"
    if ev == "link_established":
        return f"\n  ◆ LINKED  peer={msg.get('peer', '?')}\n"
    if ev == "link_terminated":
        return f"\n  ◇ TERMINATED  reason={msg.get('reason', '?')}\n"
    if ev == "call_received":
        return f"\n  ▶ INCOMING  from={msg.get('caller', '?')}  →  accept / reject\n"
    if ev == "amd_received":
        return f"\n  ✉ AMD  from={msg.get('from', '?')}  {msg.get('text', '')}\n"
    if ev == "status":
        return f"  · {msg.get('msg', '')}"
    return f"  [event] {json.dumps(msg, ensure_ascii=False)}"


# ── Core REPL logic ───────────────────────────────────────────────────────────

_req_id = 0

def _next_id() -> int:
    global _req_id
    _req_id += 1
    return _req_id


async def run_repl(ws, one_shot: str | None) -> None:
    loop    = asyncio.get_event_loop()
    pending: dict[int, asyncio.Future] = {}

    async def recv_loop() -> None:
        async for raw in ws:
            if isinstance(raw, bytes):
                continue  # spectrum binary frames — not relevant in CLI
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if "event" in msg:
                print(_fmt_event(msg), flush=True)
            elif "id" in msg:
                fut = pending.pop(msg["id"], None)
                if fut and not fut.done():
                    fut.set_result(msg)

    recv_task = asyncio.create_task(recv_loop())

    async def send(payload: dict) -> dict | None:
        rid = _next_id()
        payload["id"] = rid
        fut: asyncio.Future = loop.create_future()
        pending[rid] = fut
        await ws.send(json.dumps(payload))
        try:
            return await asyncio.wait_for(asyncio.shield(fut), timeout=6.0)
        except asyncio.TimeoutError:
            pending.pop(rid, None)
            print("  (no reply — timeout)")
            return None

    # One-shot mode: run one command, print reply, exit.
    if one_shot is not None:
        payload = build_message(one_shot)
        if payload:
            reply = await send(payload)
            if reply:
                print(_fmt_reply(reply))
        recv_task.cancel()
        return

    # Interactive REPL
    print("ale_cli  connected  —  type 'help' for commands, 'quit' to exit\n")

    while True:
        try:
            line = await loop.run_in_executor(None, lambda: input("› "))
        except (EOFError, KeyboardInterrupt):
            break

        line = line.strip()
        if not line:
            continue
        if line.lower() in ("quit", "exit", "q"):
            break

        payload = build_message(line)
        if payload is None:
            continue

        reply = await send(payload)
        if reply:
            print(_fmt_reply(reply))

    recv_task.cancel()


# ── Entry point ───────────────────────────────────────────────────────────────

async def main() -> None:
    ap = argparse.ArgumentParser(
        prog="ale_cli",
        description="Interactive CLI for openALE (WebSocket/JSON)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Example:\n"
               "  python tools/ale_cli.py --port 8765\n"
               "  python tools/ale_cli.py --port 8765 --cmd status\n",
    )
    ap.add_argument("--port", type=int, required=True,
                    help="openALE WebSocket port (same --port used to start openALE)")
    ap.add_argument("--host", default="127.0.0.1",
                    help="bridge host (default: 127.0.0.1)")
    ap.add_argument("--cmd", metavar="COMMAND",
                    help="run a single command and exit (non-interactive)")
    args = ap.parse_args()

    url = f"ws://{args.host}:{args.port}"
    print(f"Connecting to {url} …")

    try:
        async with websockets.connect(url) as ws:
            await run_repl(ws, one_shot=args.cmd)
    except (ConnectionRefusedError, OSError):
        print(f"\nERROR: could not connect to {url}", file=sys.stderr)
        print(f"       Start the bridge first:  openALE --port {args.port}", file=sys.stderr)
        sys.exit(1)

    print("\nDisconnected.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
