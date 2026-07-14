A logical implementation order is to introduce the call abstraction first, while only enabling **Analog Voice** initially. This establishes the complete session architecture without delaying development for digital voice or data modes, which can be added later without modifying the GUI or session management.

The concept can be structured as follows:

---

# Call Session Architecture

## Overview

The existing **Active Link** window shall be extended with a **Call** function. Selecting **Call** opens a small dialog similar in appearance to the Active Link window and allows the operator to select the desired communication mode.

The call architecture shall be designed from the beginning to support multiple media types while initially implementing only **Analog Voice**. Future Digital Voice and Data modes shall integrate into the same framework without requiring GUI or architectural changes.

---

# Call Menu

```
Call
 ├── Single Call
 │     ├── Analog Voice      (implemented first)
 │     ├── Digital Voice     (future)
 │     └── Data              (future)
 │
 ├── Group Call
 │     ├── Analog Voice
 │     ├── Digital Voice
 │     └── Data
 │
 ├── Net Call
 │     ├── Analog Voice
 │     ├── Digital Voice
 │     └── Data
 │
 └── Star Call
       ├── Analog Voice
       ├── Digital Voice
       └── Data
```

The menu hierarchy remains identical for every call type, ensuring a consistent user interface regardless of the selected communication method.

---

# Phase 1 – Analog Voice

The initial implementation shall support **Single Call → Analog Voice**.

After a successful ALE link establishment, the Session Manager enters **Analog Voice Mode**.

## Session Behavior

Unlike a normal ALE link, an Analog Voice session is considered user-controlled rather than timer-controlled.

The session therefore remains active until one of the following occurs:

* the operator manually terminates the call,
* the remote station disconnects,
* the radio link is lost,
* a protocol timeout or unrecoverable error occurs.

The standard ALE inactivity timeout shall not prematurely terminate an active voice conversation.

---

# Timeout Handling

To prevent interference with voice operation, the session timeout mechanism shall be adapted while Analog Voice Mode is active.

Two implementation strategies are possible.

## Phase 1 (Simple Implementation)

During Analog Voice Mode, the session timeout is simply increased to a very large value (or effectively disabled).

This mirrors the behavior used by many existing ALE implementations and requires minimal architectural changes.

Advantages:

* simple implementation
* proven interoperability
* low development effort
* ideal for the first release

---

## Phase 2 (Improved Implementation)

A more intelligent timeout mechanism can later be introduced.

Instead of relying on a fixed timeout, the Session Manager monitors activity on the selected media channel.

Examples include:

* receive audio energy
* transmit audio activity
* PTT events
* media stream activity

As long as media activity is detected, the timeout is continuously refreshed.

Once media activity stops, the normal inactivity timer resumes counting.

This approach allows automatic call termination after genuine inactivity while avoiding interruption during normal conversation.

---

# Future Digital Voice Mode

Digital Voice will reuse the same Call Session framework introduced for Analog Voice.

The media layer may later support multiple digital voice implementations, including:

* Codec2
* MELPe
* FreeDV
* MIL-STD digital voice modems
* additional future codecs

Digital Voice sessions shall also support optional end-to-end encryption integrated into the media layer.

The GUI and session management remain unchanged; only the selected media engine differs.

---

# Future Data Mode

Data Mode will also operate within the same Call Session framework.

Unlike Analog Voice, the timeout behavior is tied to modem activity.

While the selected data modem is actively transmitting or receiving, the session timeout is continuously reset.

When the modem becomes idle, the inactivity timer resumes normal operation.

This allows long-running transfers without risking unintended session termination while still automatically closing abandoned sessions.

Future Data Mode features may include:

* messaging
* file transfer
* telemetry
* IP tunneling
* custom modem integrations

Optional encryption shall also be supported.

---

# Future Call Types

The session architecture shall be generic enough that additional call types reuse the same media framework.

Supported session types include:

* **Single Call** – one-to-one communication.
* **Group Call** – one-to-many communication.
* **Net Call** – communication with all stations participating in an ALE network.
* **Star Call** – communication coordinated through a central control station.

Each call type may support:

* Analog Voice
* Digital Voice
* Data

without requiring separate implementations of the media subsystem.

---

# Recommended Implementation Roadmap

1. Implement the generic **Call Session** framework.
2. Add the **Call** dialog to the Active Link window.
3. Implement **Single Call → Analog Voice**.
4. Use a **high timeout value** during Analog Voice Mode for the initial release.
5. Introduce **activity-based timeout management** as a later enhancement.
6. Extend the framework with **Digital Voice** media engines.
7. Add **Data Mode** with modem-aware timeout handling.
8. Enable **Group**, **Net**, and **Star** call variants using the existing session framework.

This staged approach delivers a usable feature early while establishing a scalable architecture that accommodates future media types, encryption, and advanced ALE session capabilities without requiring further redesign.



 **Dynamic Audio Path Management for ALE and Voice Operation**

  Implement a dynamic audio routing mechanism that automatically switches the radio audio path between ALE signaling and user communication.

  The radio interface shall always be connected through a virtual audio device (e.g., Virtual Audio Cable on Windows or a loopback device on Linux). Depending on the current ALE state, ownership of this audio path shall change automatically.

  **Idle State (No Active ALE Link)**

  * The ALE modem shall have exclusive access to the virtual audio path.
  * Incoming radio audio shall be routed to the ALE decoder.
  * Outgoing ALE signaling generated by the modem shall be transmitted to the radio.
  * The system shall continuously monitor the channel and perform all normal ALE functions, including sounding, scanning, decoding, and link establishment.

  **Linked State (Active ALE Link)**

  * Once an ALE link has been successfully established, the ALE modem shall immediately release exclusive ownership of the radio audio path.
  * The virtual audio path shall instead become a transparent bidirectional pass-through between the radio and the selected voice audio device.
  * Received radio audio shall be routed to the user's speakers or to the speaker of a connected smartphone or tablet.
  * Audio captured from the user's microphone (desktop or mobile device) shall be routed to the radio transmitter whenever Push-to-Talk (PTT) is active.
  * This transparent audio path shall support analog voice initially and shall be designed to accommodate future digital voice codecs and other data modems without requiring architectural changes.

  **Link Termination**

  * When the ALE link is terminated for any reason, ownership of the virtual audio path shall automatically return to the ALE modem.
  * The transparent voice pass-through shall be disabled.
  * The modem shall immediately resume normal ALE operation, including channel monitoring, sounding, scanning, and the transmission and reception of ALE signaling.

  The audio routing mechanism shall be implemented as a clearly defined state machine that guarantees exclusive ownership of the modem audio path at all times. The transition between ALE signaling mode and transparent communication mode shall occur automatically based solely on the ALE link state, without requiring manual reconfiguration of audio devices.
   --- That works pretty good now. but theres a major design flaw: When connected to another station and active voice (phone patch), you can't terminate the link anymore, because the termination seqeuence does not reach the modem (audio path) of the connected station, because its blocked by live audio path.
# Feature Proposal: Unified Audio Transport, Link Management, and Media Scheduling Architecture

## Objective

Replace the current binary audio ownership model with a unified audio transport architecture that permanently owns the radio audio interface while dynamically scheduling protocol signaling and user media. This architecture eliminates the current limitation where ALE control messages (such as disconnect sequences) cannot be transmitted during an active voice session because the modem no longer has access to the radio audio path.

The new design shall preserve all existing functionality while providing a scalable foundation for future protocol extensions, including in-link PSK control bursts, bilateral LQA exchange, automatic channel migration, digital voice codecs, and additional data services.

---

# Background

The current implementation assumes two mutually exclusive operating modes:

* ALE signaling mode
* Transparent voice mode

During ALE signaling mode, the ALE modem has exclusive ownership of the virtual radio audio device.

During an established link, ownership is transferred entirely to the transparent voice path so that microphone audio is routed directly to the transmitter and received audio is routed directly to the user's speakers.

While this model is simple, it introduces a fundamental architectural limitation.

Once voice mode becomes active:

* the ALE modem no longer receives receive audio,
* the modem cannot inject transmit audio,
* disconnect sequences cannot be transmitted,
* on-link protocol extensions cannot be supported,
* future channel-management features become impossible.

The problem is therefore architectural rather than implementation-specific.

---

# Proposed Architecture

The radio interface shall become a permanently owned resource managed by a dedicated Audio Transport layer.

Individual subsystems shall no longer own the radio audio path directly.

Instead, they shall communicate through well-defined interfaces provided by the Audio Transport.

The architecture is divided into four logical layers.

## 1. Audio Transport Layer

Responsibilities:

* permanent ownership of the virtual radio audio device
* continuous receive stream acquisition
* continuous transmit stream generation
* sample routing
* receive duplication
* transmit arbitration
* timing synchronization

The Audio Transport shall remain active regardless of ALE state.

Neither the ALE modem nor the voice subsystem shall directly open or close the radio audio device.

---

## 2. Link Management Layer

This layer is responsible for all protocol-level communication.

Responsibilities include:

* ALE signaling
* sounding
* scanning
* decoding
* link establishment
* disconnect
* keepalive
* future PSK control bursts
* bilateral LQA exchange
* channel migration negotiation
* capability negotiation
* future protocol extensions

The Link Management layer always remains active while the Audio Transport is running.

It is never disconnected from receive audio.

---

## 3. Media Layer

The Media Layer provides user payloads.

Initially:

* analog voice

Future extensions:

* Codec2
* MELPe
* FreeDV
* digital voice
* text messaging
* file transfer
* telemetry
* modem-based data services

The Media Layer does not directly key the transmitter.

Instead it submits transmission requests to the scheduler.

---

## 4. Session Layer

The Session Layer coordinates the overall communication state.

Example high-level states:

* Idle
* Scanning
* Link Establishment
* Session Active
* Migration
* Disconnect
* Link Recovery

Within Session Active:

* waiting for PTT
* receiving voice
* transmitting voice
* protocol message pending
* migration pending
* disconnect pending

Voice transmission is therefore no longer synonymous with ownership of the radio.

---

# Receive Path

Receive audio shall always enter the Audio Transport.

The transport distributes received samples to interested consumers.

For example:

* ALE decoder
* future PSK decoder
* voice playback
* future digital voice decoder

Protocol bursts shall be intercepted and decoded without interrupting user voice unless required by protocol timing.

---

# Transmit Path

All outgoing transmissions shall be scheduled by a central transmit scheduler.

Subsystems submit transmission requests.

Examples:

* ALE disconnect
* ALE acknowledgement
* LQA update
* migration proposal
* keepalive
* voice
* future digital payloads

The scheduler determines transmission order according to protocol priority.

Voice shall not permanently own the transmitter.

Instead, it becomes another payload producer.

---

# On-Link PSK Bursts

The architecture shall support MIL-STD-188-141D style PSK bursts transmitted immediately before user voice.

Example transmission:

PTT pressed

↓

optional LQA burst

↓

optional control burst

↓

analog voice

The scheduler shall be responsible for constructing the complete transmit sequence.

This mechanism shall later support:

* bilateral LQA exchange
* channel migration
* protocol acknowledgements
* capability negotiation

without redesigning the audio architecture.

---

# Bilateral LQA Exchange

Future protocol extensions may periodically exchange updated LQA measurements while a session remains active.

Both stations may compare:

* measured receive quality
* database LQA values
* current channel quality
* alternative channels

If both stations support this feature, they may negotiate migration to a better frequency.

A possible sequence is:

1. Exchange updated LQA values.
2. Compare candidate channels.
3. Propose optimal channel.
4. Receive acknowledgement.
5. Terminate current link.
6. Tune both radios.
7. Automatically establish a new ALE link.
8. Resume media session.

This extension should be capability-negotiated so that interoperability with standard ALE stations is preserved.

---

# Scheduler Concept

The scheduler becomes the sole authority for transmitter access.

Illustrative priority:

1. Emergency protocol traffic
2. Disconnect
3. Migration negotiation
4. Link management
5. LQA exchange
6. Voice
7. Future data services

This allows protocol traffic to briefly preempt media without requiring the audio path itself to change ownership.

---

# Backward Compatibility

This refactoring shall preserve all currently implemented functionality.

Specifically:

* existing ALE operation
* sounding
* scanning
* decoding
* link establishment
* transparent analog voice
* existing PTT handling
* existing virtual audio device support

The new architecture shall behave identically from the user's perspective unless enhanced protocol features are enabled.

---

# Future Expansion

The architecture should be explicitly designed to support future additions without structural redesign, including:

* Codec2
* MELPe
* FreeDV
* digital voice
* encrypted voice
* GPS exchange
* messaging
* telemetry
* file transfer
* protocol extensions
* adaptive channel management

These additions should require only new payload producers or protocol modules rather than changes to the Audio Transport.

---

# Instructions for the Coding Agent

Before implementing any changes, perform a comprehensive architectural analysis of the existing codebase.

The objective is **not** to immediately rewrite the implementation, but first to understand how closely the existing architecture already aligns with this proposal.

The analysis should include, at minimum:

* the current audio architecture,
* ownership of audio devices,
* current routing of receive and transmit streams,
* ALE modem integration,
* transparent voice implementation,
* PTT handling,
* state machine implementation,
* thread model,
* synchronization mechanisms,
* interfaces between the modem and audio subsystem,
* existing extension points,
* assumptions that rely on exclusive audio ownership.

Identify which components can be reused unchanged and which require modification.

For every subsystem, classify the expected work as one of:

* reusable without modification,
* reusable with minor refactoring,
* requires significant redesign,
* should be replaced.

Produce a dependency map identifying which modules are affected by the proposed architecture and the expected impact on neighboring components.

Evaluate whether an Audio Transport abstraction can be introduced incrementally without disrupting existing behavior.

Determine whether a scheduler can initially wrap the existing transmit path before later becoming the primary transmission mechanism.

Assess whether receive audio can be duplicated to multiple consumers while preserving current ALE functionality.

Identify potential race conditions, latency concerns, timing constraints, and protocol impacts introduced by the new design.

Finally, propose a staged migration plan that minimizes risk and allows each phase to be tested independently.

**Under no circumstances shall the implementation break existing functionality or introduce regressions.**

The preferred approach is an incremental refactoring in which existing behavior continues to function throughout the transition. New abstractions should initially coexist with the current implementation where practical, with legacy code removed only after the replacement has been fully validated.

If any proposed architectural change carries a significant risk of destabilizing the system, document the risk, explain why it exists, and propose a safer incremental alternative before implementation begins.

---

---

call ablauf GUI:
call should open a little window like the "active link" window.
then there would be a menu:
call -> single_call ->//
					| Button: AVoice --> AnalogVoice --> in AnalogVoice Mode, wird der Timeout-Timer so gesetzt, dass er nicht mit der Vocie Operation interferiert. Grundsätzlich wird im AnalogVoice Mode der Call erst mit manuellem beenden der Verbindung beendet. referenzen lösen das darüber, dass der timeout timer einfach sehr hoch gesetzt wird. wir könnten das übernehmen, und wahlweise zusätzlich die aktivität auf diesem kanal auswerten, um festzustellen ob noch voice aktiv ist. 
					| Button: DVoice --> DigitalVoice --> DigitalVoice wird über unterschiedliche Implementierungen ermöglicht werden. Vorgesehen ist codec2 und diverse MIL STD Digital Voice Modems. DigitalVoice Modes sollen ein Encryption Feature erhalten. DigitalVoice Mode wird später noch implementiert.
					| Button: DATA --> im DATA Mode wird der Timout-Timer abhängig von der Verwendung des Datenmodes gesetzt. Solange Data Mode = Active, wird das Timeout zurückgesetzt. Der Timeout-Timer beginnt wieder zu zählen, sobald das Data-Modem nicht mehr aktiv ist. Data Modes sollen ein Encryption Feature erhalten. Data Mode wird später noch implementiert.
					
call -> group_call ->//
					| AnalogVoice
					| DigitalVoice
					| DATA
					
call -> net_call ->//
					| AnalogVoice
					| DigitalVoice
					| DATA
					
call -> star_call ->//
					| AnalogVoice
					| DigitalVoice
					| DATA
					
					

---