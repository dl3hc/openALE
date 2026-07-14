# Upstream issue draft — Quisk

> Copy everything below into an issue/mail for the Quisk maintainer
> (James Ahlstrom, N2ADR — https://github.com/jimahlstrom/quisk or the
> n2adr.com contact). Replace `[hamlib issue link]` once the Hamlib issue
> from `UPSTREAM_ISSUE_HAMLIB_lock_mode.md` is filed. Drop this header.

---

**Title:** Hamlib server: implement `get_lock_mode`/`set_lock_mode` — works around a Hamlib client bug that silently discards mode commands

## Summary

Quisk's built-in hamlib server (`HamlibHandlerRig2`, port 4532) answers
`\get_lock_mode` with `RPRT -4` via the generic `UnImplemented()` fallback.
That reply is **protocol-correct** — but it triggers a bug in the Hamlib
*client* library (4.5 through current master) that can make Hamlib-based
applications silently lose **all mode changes** sent to Quisk. Implementing
the two lock-mode handlers (small patch below) fully defuses the problem for
every Hamlib client, past and future.

## The Hamlib client bug (reported upstream: [hamlib issue link])

Hamlib sends `\get_lock_mode` before **every** `rig_set_mode()`. Inside
Hamlib (`src/rig.c`):

```c
int locked_mode;                       // uninitialized
rig_get_lock_mode(rig, &locked_mode);  // return code ignored
if (locked_mode) { return (RIG_OK); }  // mode command silently dropped
```

When Quisk replies `RPRT -4`, Hamlib's netrigctl backend runs
`sscanf("RPRT -4", "%d", lock)` — which converts nothing and leaves the
variable **uninitialized** — and still returns success. Whether the `M`
(set mode) command is then actually transmitted to Quisk depends on stack
garbage in the client application.

## Impact for Quisk users

* Any application that controls Quisk through Hamlib's NET_RIGCTL backend
  (WSJT-X, fldigi, JS8Call, custom CAT software, ...) can silently lose mode
  changes: `rig_set_mode()` reports success, nothing arrives at Quisk.
* Frequency changes are unaffected (no lock probe in `rig_set_freq`), so the
  observed symptom is the confusing "frequency follows, mode doesn't".
* Because the trigger is uninitialized memory, the failure appears and
  disappears with unrelated changes in the client application — we bisected
  one such regression (a logging refactor flipped it from "works" to "no mode
  command ever arrives") down to exactly this mechanism, verified on the wire
  with a protocol mock: 6 `rig_set_mode()` calls, only 4 `M` commands on the
  wire against an `RPRT -4` reply — 6 of 6 once the server replies a
  parseable lock value.

Even after Hamlib fixes its side, patched Quisk protects users running the
many already-deployed Hamlib versions.

## Suggested patch (against quisk 4.2.53, `quisk.py`)

Mirrors rigctld: reply the lock state as a plain value line so Hamlib's
`sscanf("%d")` always succeeds. State is kept per connection, like the other
handler state.

```diff
--- quisk.py.orig
+++ quisk.py
@@ -742,6 +742,7 @@
     self.vfo = "Main"
     self.split_mode = 0
     self.split_vfo = 'VFO'
+    self.lock_mode = 0
     h = self.Handlers = {}
     h[''] = self.ErrProtocol
     h['dump_state']	= self.DumpState
@@ -765,6 +766,8 @@
     h['set_split_freq']	= self.SetSplitFreq
     h['get_split_vfo']	= self.GetSplitVfo
     h['set_split_vfo']	= self.SetSplitVfo
+    h['get_lock_mode']	= self.GetLockMode
+    h['set_lock_mode']	= self.SetLockMode
     self.MakeDumpState()
   def MakeDumpState(self):
     dump_state = []
@@ -1130,6 +1133,19 @@
       self.ErrParam()
     else:
       self.app.pttButton.SetValue(ptt, True)
+  def GetLockMode(self):
+    # Hamlib sends \get_lock_mode before every rig_set_mode.  Replying
+    # "RPRT -4" (unimplemented) leaves hamlib's lock variable UNINITIALIZED
+    # and rig_set_mode may silently discard the mode command (hamlib bug,
+    # rig.c rig_set_mode).  Reply with a real value so the variable is set.
+    self.Reply('Locked', self.lock_mode, 0)
+  def SetLockMode(self):
+    lock = self.GetParamNumber()
+    try:
+      self.lock_mode = int(lock)
+      self.Reply(0)
+    except:
+      self.ErrParam()
   def GetFunc(self):
     name = self.GetParamName()
     if name == '?':	# send back supported functions
```

## Verification

```
$ telnet localhost 4532
\get_lock_mode
0
\set_lock_mode 0
RPRT 0
```

and with Hamlib: `rigctl -m 2 -r localhost:4532 M USB 0` now reliably reaches
Quisk on every call (verify with `HAMLIB_DEBUG=1`-style server logging or
`m` readback).
