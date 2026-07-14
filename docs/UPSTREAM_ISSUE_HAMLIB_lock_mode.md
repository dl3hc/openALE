# Upstream issue draft — Hamlib

> Copy everything below into a new issue at https://github.com/Hamlib/Hamlib/issues
> (drop this header). Attach `mockA_out.txt` / `mockB_out.txt` wire logs if requested.

---

**Title:** `rig_set_mode()` silently discards the mode command (returns RIG_OK) based on an uninitialized variable when the peer does not implement `\get_lock_mode`

## Summary

`rig_set_mode()` consults an **uninitialized** local `int locked_mode` when the
lock-state query fails without writing its out-parameter. Whether a
`rig_set_mode()` call actually transmits anything to the rig then depends on
stack garbage — while the call still returns `RIG_OK`. With the NET_RIGCTL
backend this is trivially triggered by any rigctld-protocol server that does
not implement `\get_lock_mode` (e.g. **Quisk**'s built-in hamlib server, which
replies `RPRT -4`).

Affected: observed on a 4.5-era build (netrigctl 20221214.0); the same code is
still present in current master (`src/rig.c`, `rig_set_mode`).

## The failure chain

`src/rig.c`, `rig_set_mode()`:

```c
int HAMLIB_API rig_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
    const struct rig_caps *caps;
    int retcode;
    int locked_mode;                       // (1) UNINITIALIZED
    ...
    rig_get_lock_mode(rig, &locked_mode);  // (2) return code IGNORED

    if (locked_mode) { return (RIG_OK); }  // (3) silent elision, reports success
```

`rigs/dummy/netrigctl.c`, `netrigctl_get_lock_mode()`:

```c
int netrigctl_get_lock_mode(RIG *rig, int *lock)
{
    ...
    ret = netrigctl_transaction(rig, cmdbuf, strlen(cmdbuf), buf);

    if (ret == 0)
    {
        return -RIG_EPROTO;            // (4) *lock never written
    }

    sscanf(buf, "%d", lock);           // (5) buf may be "RPRT -4" -> sscanf
    return (RIG_OK);                   //     converts NOTHING, *lock never
}                                      //     written, yet RIG_OK is returned
```

`netrigctl_transaction()` returns the RPRT code when the first reply line is an
`RPRT` line, and leaves that line in `buf`:

* Server replies `RPRT -4` (command unimplemented — e.g. Quisk):
  `ret = -4` → the `ret == 0` guard does **not** fire → step (5): `sscanf` on
  `"RPRT -4"` matches nothing, `*lock` is untouched, `RIG_OK` is returned.
* Server replies a data-less `RPRT 0`: `ret == 0` → `-RIG_EPROTO`, `*lock`
  also untouched.

In both cases `rig_set_mode()` (which ignores the return code anyway) then
branches on an **uninitialized** `locked_mode`. If the garbage is nonzero, the
mode command is never sent, and the caller gets `RIG_OK`.

## Why this is nasty in practice

* The behaviour is deterministic per call path but flips with **unrelated code
  changes in the client application** (different stack usage). We chased a
  regression where a pure logging refactor in our application made *all* mode
  commands silently disappear when controlling Quisk — down to this variable.
* Verification via `rig_get_mode()` readback + re-send does not help: the
  re-sent `rig_set_mode()` takes the same elision path.
* `rig_set_freq()` is unaffected (no lock probe), so the classic user symptom
  is "frequency changes work, mode changes don't, no errors anywhere".

## Reproduction

1. Run any rigctld-protocol mock that answers `\get_lock_mode` with `RPRT -4`
   (or run Quisk 4.2.x and use its hamlib server on port 4532).
2. Connect with model 2 (NET_RIGCTL), then issue several `rig_set_mode()`
   calls with interleaved `rig_get_mode()` calls.
3. Compare the client's return codes (all `RIG_OK`) with the server's wire
   log.

Our wire log (6 × `rig_set_mode`, mock answering `RPRT -4`): only **4 of 6**
`M` commands arrived; the two elided calls returned `RIG_OK` in ~0 ms without
any `netrigctl_set_mode` in the TRACE output. With the identical sequence
against a mock answering `0` (parseable lock value), **6 of 6** arrived —
confirming the lock probe as the deciding factor.

```
# mock wire log, server replies "RPRT -4" to \get_lock_mode:
[LOCK] get_lock_mode        <- call 1 ... M command NEVER ARRIVES (elided)
[LOCK] get_lock_mode        <- call 2
[TRX] Mode PKTUSB
[LOCK] get_lock_mode        <- call 3
[TRX] Mode PKTUSB
[LOCK] get_lock_mode        <- call 4 ... M command NEVER ARRIVES (elided)
[LOCK] get_lock_mode        <- call 5
[TRX] Mode PKTLSB
[LOCK] get_lock_mode        <- call 6
[TRX] Mode PKTUSB
```

## Suggested fix

1. `src/rig.c`, `rig_set_mode()`: initialize and honor the return code —

   ```c
   int locked_mode = 0;
   if (rig_get_lock_mode(rig, &locked_mode) != RIG_OK) { locked_mode = 0; }
   ```

2. `rigs/dummy/netrigctl.c`, `netrigctl_get_lock_mode()`: propagate failure —
   return an error when the transaction failed (`ret < 0`) **or** when
   `sscanf(buf, "%d", lock) != 1`, so `*lock` is guaranteed written on
   `RIG_OK`.

Either change alone fixes the silent elision; both together make the contract
explicit.
