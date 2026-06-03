# STANAG 5066 – Implementation Summary for Phoenix Nest MARS Suite

## What it is

STANAG 5066 (Edition 3 is current) defines the **Subnetwork Interface for HF data communications**.
It can be understood as a **socket-like abstraction layer** between application software and HF radio/modem systems.

---

## Architecture Overview

```
Client Applications
        │
        ▼
Subnetwork Interface (S_* Primitives)   ← API boundary
        │
        ▼
Subnetwork Sublayers
    ├─────────────────────────────────────────────
    │ Channel Access (ALE integration)
    │ Data Transfer (ARQ / non-ARQ modes)
    │ Subnetwork Management
    └─────────────────────────────────────────────
        │
        ▼
Link / Modem Layer (MIL-STD-188-110)
        │
        ▼
Physical Layer (Radio)
```

---

## S_Primitives (API Layer)

Applications communicate via **service primitives** over a TCP-like interface.

| Primitive                                | Purpose                            |
| ---------------------------------------- | ---------------------------------- |
| S_BIND_REQUEST / S_BIND_RESPONSE         | Client registers, receives SAP ID  |
| S_UNBIND_REQUEST / S_UNBIND_RESPONSE     | Client disconnects                 |
| S_UNIDATA_REQUEST / S_UNIDATA_INDICATION | Non-ARQ (unreliable) data transfer |
| S_DATA_REQUEST / S_DATA_INDICATION       | ARQ (reliable) data transfer       |
| S_EXPEDITED_DATA_*                       | Priority traffic                   |
| S_MANAGEMENT_*                           | Configuration, status, control     |

---

## Key Concepts

### Node Address

* 7 bytes
* Typically maps to ALE addressing
* Supports group/broadcast addressing

---

### Service Access Points (SAPs)

* Logical endpoints (similar to TCP ports)
* Applications bind to SAPs to receive traffic
* Priority range: **0–15** (15 = highest priority)

---

## Delivery Modes

| Mode                   | ARQ | Use Case                                |
| ---------------------- | --- | --------------------------------------- |
| Non-ARQ                | No  | Broadcast, time-critical, poor channels |
| ARQ                    | Yes | Reliable point-to-point transfer        |
| ARQ + Selective Repeat | Yes | Large or long messages                  |

---

## Physical Interface

* Standard: **TCP/IP (port 5066)**
* Legacy: Serial / HDLC framing (rare in modern systems)

---

## PDU Structure (Wire Format)

```
┌─────────┬────────┬─────────────┬─────────────┐
│ Version │ Type   │ Length      │ Payload     │
│ (4 bit) │(4 bit) │ (16 bit)    │ (variable)  │
└─────────┴────────┴─────────────┴─────────────┘
```

### Type Values (examples)

* 0x01: S_BIND_REQUEST
* 0x02: S_UNBIND_REQUEST
* 0x03: S_BIND_ACCEPTED
* 0x04: S_BIND_REJECTED
* 0x05: S_UNBIND_INDICATION
* 0x06: S_HARD_LINK_*
* 0x07: S_UNIDATA_*
* 0x08: S_DATA_*

---

## ALE Integration (Channel Access Sublayer)

STANAG 5066 interacts with **MIL-STD-188-141 ALE**:

* **Hard Links**: Dedicated circuit maintained by ALE
* **Soft Links**: On-demand connections per message
* **Broadcast**: No link establishment, direct transmission

### Flow Translation

`S_DATA_REQUEST` typically triggers:

1. ALE call setup (if no active link exists)
2. Modem data transmission burst
3. ALE teardown (for soft links)

---

## Implementation Scope

### Minimum Viable Implementation

* TCP listener on port **5066**
* S_BIND / S_UNBIND handling
* S_UNIDATA (non-ARQ mode)
* PDU encode/decode layer

---

### Full Implementation

1. S_DATA with ARQ state machine
2. Hard/soft link management
3. Multi-client support
4. Expedited data queueing

---

## Implementation Notes (Phoenix)

* Separate module: `phoenix-stanag-5066`
* Test against known HF stacks (e.g. Harris radios, node-hf legacy implementations)
* Start with **non-ARQ mode first**
* Focus on interface correctness before ARQ complexity
* Define SAP mapping clearly for MARS applications

---

## Reference Documents

* STANAG 5066 Edition 3 (NATO UNCLASSIFIED)
* MIL-STD-187-721 (US implementation guidance)
* STANAG 5066 Annex F (S_Primitives specification)
