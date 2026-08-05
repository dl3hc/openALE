# LQA Exchange and Auto-Relink — Technical Documentation

## Overview

openALE implements two related mechanisms from MIL-STD-188-141B Appendix A:

1. **Bilateral LQA Exchange (KA1-Response, A.5.4.4):** During the 3-way handshake,
   both stations exchange their local channel measurements via CMD words. After a
   successful link, both stations therefore have **bidirectional** quality data for
   the currently used channel.

2. **Auto-Relink (A.5.4.5):** After linking, the station monitors whether a known
   channel would be significantly better bilaterally than the active one. If so, the
   link is properly terminated via TWAS and immediately rebuilt on the better channel.

Not implemented: A.5.5 Frequency-Select-CMD (requires split-VFO / full-duplex operation;
for simplex operation, TWAS+Relink provides the same effect without additional hardware
requirements).

---

## Part 1: Bilateral LQA Exchange (KA1-Response)

### Specification Background (A.5.4.4)

MIL-STD-188-141B defines a 24-bit data field in the CMD-LQA word (character value `'a'`, Table A-XIV) with the following subfields:

| Field   | Bits | Meaning                                                     |
| ------- | ---- | ----------------------------------------------------------- |
| `KA1`   | 13   | 1 = remote station should report its LQA back               |
| `SINAD` | 5    | Received SINAD code (0–30; 31 = no value; higher = better)  |
| `BER`   | 5    | Received BER code (0–30; 31 = no value; **lower = better**) |
| `MP`    | 5    | Multipath propagation in ms (0–30; 31 = no value)           |

Both stations independently measure the input quality (`FROM` direction: what they
receive themselves). The CMD exchange provides the remote station with the `TO`
direction (what the remote station receives from the own station).

### Handshake Sequence

```
SAM (Calling Station)                    JOE (Called Station)
──────────────────────                    ──────────────────────
initiate_call() →
  KA1=1 set in pending_lqa_cmd_
  (own SINAD/BER for first channel
  pre-filled from LQA database)

CALL:  …TO:JOE  CMD:'a'(KA1=1,SINAD,BER,MP)  TIS:SAM…
                                          →  Word received in on_received_word()
                                             pending_bilateral_payload_ stored

                                          maybe_emit_call_alert() →
                                            update_bilateral(freq, SAM, SINAD, BER, MP)
                                            [JOE's LQA DB: SAM bilateral data populated]
                                            KA1=1 detected →
                                            LQA report words for SAM generated +
                                            own SINAD/BER (KA1=0) stored in pending_lqa_cmd_

RESPONSE:  …FROM:JOE  CMD:'a'(KA1=0,SINAD,BER,MP)  [LQA report words]  TIS:JOE…
  Word received in on_received_word() →
  pending_bilateral_payload_ stored

ACK:  …TO:JOE  TIS:SAM…
                                          LINKED

on_operator_event(LINK_ESTABLISHED) →
  update_bilateral(freq, JOE, SINAD, BER, MP)
  [SAM's LQA DB: JOE bilateral data populated]
LINKED
```

After the handshake, **both stations** have bilateral data for the current channel:

* SAM's DB: `bilateral_sinad`/`bilateral_ber` for JOE on this frequency = what JOE
  received from SAM (= TO direction from SAM's perspective)
* JOE's DB: `bilateral_sinad`/`bilateral_ber` for SAM = what SAM received from JOE

### Implementation (Key Points)

| Step                                 | Function                                            | File                                                |
| ------------------------------------ | --------------------------------------------------- | --------------------------------------------------- |
| SAM encodes KA1=1                    | `initiate_call()` Block A4                          | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| JOE receives CMD                     | `on_received_word()` → `pending_bilateral_payload_` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| JOE writes bilateral data + responds | `maybe_emit_call_alert()` Block A5 + C5 TX          | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| SAM receives JOE's CMD               | `on_received_word()` → `pending_bilateral_payload_` | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| SAM writes bilateral data            | `on_operator_event(LINK_ESTABLISHED)`               | [ale_controller.cpp](../src/App/ale_controller.cpp) |
| Data storage                         | `LQADatabase::update_bilateral()`                   | [lqa_database.cpp](../src/LQA/lqa_database.cpp)     |

---

## Part 2: Bilateral Channel Score (A.5.4.5)

### Scoring Formula

```
bilateral_channel_score(entry) = (FROM quality + TO quality) / 2
```

* **FROM quality:** Locally measured input quality (soundings, BER/SNR during LINKED).
  Falls back to `entry.score` (Composite) if no own SINAD measurement is available.
* **TO quality:** SINAD/BER reported by the peer via CMD 'a' for our transmission
  (`bilateral_sinad` code in dB, higher = better; `bilateral_ber` code lower = better).
  The weaker of the two BER/SINAD values limits the TO quality.

The average of both directions results in the bilateral score (Spec A.5.4.5.1: "sum
of the two LQA values"; normalized to the 0–30 scale through division, identical
ranking). Higher is better; with equal scores the more balanced path wins (tiebreaker).
If `bilateral_sinad > 30` (no value received), the unilateral composite score is used
as fallback (so ranking remains meaningful without bilateral data).

### Channel Ranking

```cpp
// rank_channels_for_station(peer) — from lqa_analyzer.cpp
// Returns channels sorted by bilateral_channel_score() (best first)
auto ranked = lqa_analyzer_.rank_channels_for_station(peer);
ranked[0].frequency_hz  // best frequency
ranked[0].score         // bilateral score
```

This ranking is already used automatically by `initiate_call()`: During a call, the
channel with the highest bilateral score is attempted first.

---

## Part 3: Auto-Relink (evaluate_relink)

### Concept

After linking, the calling station can detect that another channel would now be
significantly better — for example, because propagation conditions have changed or
because soundings/previous links have provided new bilateral data for other frequencies.

The standard does not provide its own control mechanism in the LINKED state (A.5.5
Frequency-Select-CMD requires split-VFO). The specification-compliant solution is TWAS

* re-link:

1. Properly terminate the active link via TWAS
2. Immediately initiate a new call on the now-best channel
3. JOE automatically returns to SCANNING after TWAS and accepts the new call

### Trigger Conditions

`evaluate_relink()` is called in every `update()` tick when:

* `relink_enabled = true` (configuration)
* `lqa_enabled = true` (LQA recording active)
* State machine is in state `LINKED`
* No relink already in progress (`pending_relink_addr_.empty()`)

Within `evaluate_relink()`, the following also applies:

```
Hysteresis guard:  rx_ber_settle_ms_ > 0
                  AND (now - rx_ber_settle_ms_) >= 4 × Tdrw  (≈ 640 ms)
```

`rx_ber_settle_ms_` is set as soon as the first BER measurement in LINKED state is
stable. This prevents a relink from being triggered immediately after linking before
the active channel quality measurement has stabilized.

### Relink Decision Logic

```
ranked = rank_channels_for_station(peer)   // bilateral ranking
best_freq  = ranked[0].frequency_hz
best_score = ranked[0].score

cur_score  = ranked[i].score  (i: index of currently used channel)

Trigger when:
  best_freq != current_freq
  AND best_score > cur_score + relink_improvement_threshold
```

`relink_improvement_threshold` (default: 5.0 points on the 0–30 scale) provides
hysteresis against "channel thrashing": the switch only occurs if the improvement is
significant and not merely marginal.

### Sequence After Triggering

```
evaluate_relink() sets pending_relink_addr_ = peer
evaluate_relink() calls sm_.terminate_link() → SM sends TWAS, transitions → IDLE

next update() tick:
  pending_relink_addr_ not empty
  State == IDLE or SCANNING
  → initiate_call(pending_relink_addr_)
     (uses rank_channels_for_station() → best channel attempted first)
  pending_relink_addr_.clear()

JOE receives new CALL, responds, 3-way handshake on new channel
```

### Sequence Diagram

```
SAM (LINKED on 14.250 MHz)             JOE (LINKED on 14.250 MHz)
───────────────────────────             ────────────────────────────

[new LQA sounding received:
 7.100 MHz bilateral score 22 > 14.250 MHz score 12 + threshold 5]

evaluate_relink() →
  pending_relink_addr_ = "JOE"
  sm_.terminate_link()

SM → sends TWAS ─────────────────────► JOE receives TWAS
SM → IDLE                               JOE → SCANNING

update():
  State==IDLE, pending_relink_addr_="JOE"
  → initiate_call("JOE")
    rank[0] = 7.100 MHz (score 22)

CALL on 7.100 MHz ───────────────────► [JOE scans, receives CALL on 7.100 MHz]

            ◄── 3-way handshake ──►

LINKED on 7.100 MHz                    LINKED on 7.100 MHz
(better channel)                        (better channel)
```

### Implementation (Key Points)

| Element                                           | File / Location                                                |
| ------------------------------------------------- | -------------------------------------------------------------- |
| `evaluate_relink()`                               | [ale_controller.cpp:941](../src/App/ale_controller.cpp)        |
| Call hook in `update()`                           | [ale_controller.cpp:742](../src/App/ale_controller.cpp)        |
| Relink execution hook in `update()`               | [ale_controller.cpp:750](../src/App/ale_controller.cpp)        |
| `pending_relink_addr_` member                     | [ale_controller.h](../include/App/ale_controller.h)            |
| `relink_enabled` / `relink_improvement_threshold` | [ale_station_config.h:74](../include/App/ale_station_config.h) |

---

## Configuration and Persistence

### Configuration Fields (ALEStationConfig)

```cpp
// include/App/ale_station_config.h
bool  relink_enabled              = false;  // Auto-Relink on/off
float relink_improvement_threshold = 5.0f;  // Minimum score improvement (0–30)
```

### Settings File (export_settings / import_settings)

```ini
relink_enabled=1
relink_improvement_threshold=5.000000
```

### WebSocket Bridge API (ale_bridge.cpp)

**Set Relink settings:**

```json
→ { "cmd": "RELINK_SET", "relink_enabled": true, "relink_threshold": 5.0 }
← { "ok": true }
```

**Query Relink settings:**

```json
→ { "cmd": "RELINK_GET" }
← { "ok": true, "relink_enabled": true, "relink_threshold": 5.0 }
```

### GUI (apps/gui/)

In the LQA tab of the settings:

* **Auto-Relink** toggle (checkbox `cfgAutoRelink`) — enables/disables Relink
* **Threshold** numeric field (`cfgRelinkThreshold`, 1–30 points) — minimum improvement
* Changes are immediately pushed via `applyRelinkToBridge()` → `RELINK_SET`
* On connect, the core state is read via `syncRelinkFromBridge()` → `RELINK_GET`

---

## Threshold Recommendations

| Scenario                                                   | Threshold          |
| ---------------------------------------------------------- | ------------------ |
| Aggressive optimization (many channels, stable conditions) | 2–3 points         |
| Standard (balanced)                                        | 5 points (Default) |
| Stable, few interruptions                                  | 8–10 points        |
| Relink only for large quality differences                  | 15+ points         |

**Note:** Every relink briefly interrupts ongoing communication (TWAS + new
3-way handshake, approx. 3–10 seconds depending on scanning configuration). Thresholds
that are too small can cause "channel thrashing" under unstable propagation conditions.

---

## Limitations and Boundary Conditions

* **Only calling station initiates:** Only SAM (the originally calling station) executes
  `evaluate_relink()`. JOE does not send its own TWAS for relink. For bidirectional
  relink, the function would need to be active on both sides — which could however lead
  to race conditions (both sending TWAS simultaneously).

* **LQA data must exist:** Without previous soundings or links to other frequencies,
  `rank_channels_for_station()` is empty and relink is not triggered.
  Auto-Relink is therefore only useful if the station actively receives soundings and
  records LQA.

* **Relink direction:** The new channel is determined by the LQA ranking at the time
  of relink. If new soundings have arrived in the meantime, this may be a different
  channel than the one that triggered the relink.

---

## Part 4: Enhanced Frequency-Select (CMD 'f', A.5.6.3.2)

### Concept

Instead of unilateral TWAS+Relink (Auto-Relink), the proposing station can negotiate
a better channel bilaterally with the remote station before sending TWAS.
The protocol uses the standard CMD 'f' (TABLE A-XVI = Frequency-Select) as a
Post-Link-Orderwire.

**Backward compatibility:** Standard ALE-2G stations ignore CMD 'f' in the LINKED state
according to A.5.6.3.2d — the proposal runs into a timeout and the link remains active.

### CMD 'f' Word Format (A.5.6.3.2)

**Word 1 — CMD 'f' (21-bit Payload):**

| Bits    | Content                                     |
| ------- | ------------------------------------------- |
| [20:14] | `'f'` = 1100110 (Frequency-Select CMD code) |
| [13:8]  | Control = 000000 (absolute)                 |
| [7:4]   | 100-Hz BCD = 0                              |
| [3:0]   | 10-Hz BCD = 0                               |

**Word 2 — DATA (21-bit Payload) — BCD Frequency Identifier:**

| Bits    | Content     |
| ------- | ----------- |
| [20]    | 0 (W4 = 0)  |
| [19:16] | 10-MHz BCD  |
| [15:12] | 1-MHz BCD   |
| [11:8]  | 100-kHz BCD |
| [7:4]   | 10-kHz BCD  |
| [3:0]   | 1-kHz BCD   |

DATA payload = 0 (all BCD fields zero) = **Reject Sentinel** (no real frequency
starts with 0 MHz and 0 kHz in every field).

### Sequence

```
Station A (Proposer, LINKED)       Station B (Responder, LINKED)
──────────────────────────         ──────────────────────────────
evaluate_freq_proposal() →
  better channel detected
  fs_phase_ = PROPOSED
  send_freq_select_orderwire(freq)

CMD 'f' + DATA(freq) + TIS:SAM ──► on_received_word(): CMD 'f' + DATA captured
                                    handle_freq_select_proposal(freq)
                                    LQA evaluates: score better than threshold?

                  ACCEPT:           send_freq_select_orderwire(freq)   [Echo]
   ◄── CMD 'f' + DATA(freq) + TIS:JOE ──
   handle_freq_select_response(freq)
   fs_phase_ = EXECUTING
   pending_relink_addr_ = peer
   sm_.terminate_link() → TWAS

                  REJECT:           send_freq_select_orderwire(0)       [freq=0]
   ◄── CMD 'f' + DATA(0) + TIS:JOE ──
   handle_freq_select_response(0)
   fs_phase_ = IDLE
   fs_cooldown_ms_ = now + 60s
   [Link remains active]

                  TIMEOUT (Standard station):
   [no DATA follow after 3s]
   fs_phase_ = IDLE
   [Link remains active]
```

### CMD Character Code Detection (Specification Bug Fix)

CMD character codes such as `'f'` (0x66), `'a'` (0x61), `'n'` (0x6E) are located in the
b7b6="11" range (0x60–0x7F) of 7-bit ASCII and **fall outside both Basic38 and Expanded64**.
The existing decoder therefore sets `word.address[0]='?'` and `word.valid=false` for received
CMD words over radio.

**Fix:** `cmd_char_code(word)` in `ale_freq_select.h` reads the CMD character code directly
from `word.raw_payload >> 14` and bypasses the faulty character-set validation.
This approach is consistently applied for all CMD detections in `on_received_word()`
(`'a'`, `'n'`, `'r'`, `'f'`).

### Collision Resolution

If both stations send a proposal simultaneously:

* Lexicographically lower self-address **backs off** (discards own proposal)
* Lexicographically higher self-address **has priority**

### Configuration

```cpp
// include/App/ale_station_config.h
bool enhanced_freq_select = false;  // CMD 'f' negotiation on/off
```

Settings file:

```ini
enhanced_freq_select=1
```

WebSocket bridge:

```json
→ { "cmd": "FREQ_SELECT_SET", "enhanced_freq_select": true }
← { "ok": true }

→ { "cmd": "FREQ_SELECT_GET" }
← { "ok": true, "enhanced_freq_select": true }
```

### Backward Compatibility Matrix

| Scenario                   | Behavior                                                                  |
| -------------------------- | ------------------------------------------------------------------------- |
| Both Standard ALE-2G       | No CMD 'f' in LINKED state; TWAS+Relink if Auto-Relink=ON                 |
| SAM Enhanced, JOE Standard | CMD 'f' to JOE → ignored according to A.5.6.3.2d → timeout → link remains |
| Both Enhanced              | CMD 'f' + DATA negotiation → coordinated TWAS+Relink or rejection         |
| Enhanced disabled          | `evaluate_relink()` as before — no change                                 |
| CALLING/HANDSHAKE/SOUNDING | Completely unchanged                                                      |

### Implementation (Key Points)

| Element                         | File / Location                                                        |
| ------------------------------- | ---------------------------------------------------------------------- |
| BCD Encoding/Decoding           | [ale_freq_select.h](../include/Protocol/Control/ale_freq_select.h)     |
| `evaluate_freq_proposal()`      | [ale_controller.cpp](../src/App/ale_controller.cpp)                    |
| `handle_freq_select_response()` | [ale_controller.cpp](../src/App/ale_controller.cpp)                    |
| `handle_freq_select_proposal()` | [ale_controller.cpp](../src/App/ale_controller.cpp)                    |
| `trigger_linked_orderwire()`    | [ale_state_machine.cpp](../src/Protocol/Control/ale_state_machine.cpp) |
| `cmd_char_code()` helper        | [ale_freq_select.h](../include/Protocol/Control/ale_freq_select.h)     |
| `enhanced_freq_select` config   | [ale_station_config.h](../include/App/ale_station_config.h)            |

Written back — this requires an explicit `STATION_SAVE`.
