# Threading model — ale_bridge / WsServer / WASAPI

This documents the concurrency of the live GUI path
(`apps/ale_bridge.cpp` + `apps/bridge/ws_server.*` + `src/App/audio_device.cpp` +
`src/Modem/ale2g_modem.cpp`), the synchronization that makes it safe, the one
known limitation, and a concrete upgrade path if it ever needs lifting.

> Scope: this is the *bridge* (GUI ↔ live `ALEController`) path. `apps/ale_cli.cpp`
> uses the same controller/audio threads but a stdin reader instead of a socket;
> the audio-thread parts below apply equally to it.

## The three threads

### 1. WsServer I/O thread (`WsServer::io_thread_main`)
Owns the listening socket. In a loop it `accept()`s a connection, reads the HTTP
request head, then branches:
- **Plain HTTP GET** → `serve_static()` reads a file from the web root, writes the
  response, closes the socket, loops back to `accept()`. (Serves the apps/gui/
  static files: `index.html`, `app.js`, `styles.css`.)
- **WebSocket upgrade** → completes the RFC6455 handshake, then runs
  `handle_client()`, whose `recv()` loop parses frames and pushes received
  **text** payloads onto `recv_queue_` (guarded by `recv_mutex_`). Control frames
  (ping/close) are handled inline.

This thread **never** calls `send_text()` / `send_binary()`.

### 2. Main loop (`main()` in ale_bridge.cpp)
The single owner of the `ALEController`. Each iteration:
- `ctrl.update(t)`
- if an audio device is attached: `audio->tick(rx_buf)` (drains the WASAPI RX
  queue) then `ctrl.feed_audio(rx_buf)`
- drains `recv_queue_` via `ws.pop_message()`, runs `dispatch_command()`, and
  replies with `ws.send_text(...)`
- detects `ALEState` changes and emits a `state` event via `ws.send_text(...)`

All controller callbacks (`on_status_changed`, `on_link_established`,
`on_call_received`, `on_link_terminated`, `on_amd_received`) fire **synchronously
inside `ctrl.update()` / `dispatch_command()`**, i.e. on this thread, and call
`ws.send_text(...)`.

> **Spectrum frames run here too.** The spectrum callback set with
> `ctrl.set_spectrum_callback(...)` is invoked from
> `feed_audio() → Demodulator::push_samples() → compute_spectrum_() →
> spectrum_cb_`, which all execute on the **main loop thread** (push_samples is
> called from feed_audio, not from the WASAPI thread). Earlier comments claimed
> this fired "from the audio capture thread" — that is wrong and has been
> corrected in `ws_server.h` and `ale_bridge.cpp`.

### 3. WASAPI audio thread (`WasapiDevice::audio_loop`, src/App/audio_device.cpp)
Created in `open()`, joined in `close()`. Waits on three Win32 events
(render / capture / stop) and moves PCM:
- **TX**: pulls 49-bit symbol frames via `sym_pull_` (guarded by `sym_src_mtx_`),
  tone-generates + resamples to device rate, hands samples to WASAPI render.
- **RX**: reads WASAPI capture, resamples to 8 kHz, pushes into `rx_queue_`
  (guarded by `rx_mtx_`); the main loop drains it in `tick()`.
- Increments `frames_rendered_` (atomic) for the main loop's frame-completion
  callbacks.

## Synchronization summary

| Shared state            | Producer            | Consumer            | Guard            |
|-------------------------|---------------------|---------------------|------------------|
| `recv_queue_`           | I/O thread          | main loop           | `recv_mutex_`    |
| outgoing frames (wire)  | main loop           | (socket)            | `send_mutex_`    |
| `client_` socket handle | I/O thread / stop() | senders             | `std::atomic`    |
| `rx_queue_`             | audio thread        | main loop (`tick`)  | `rx_mtx_`        |
| `sym_pull_`             | main loop (`open`)  | audio thread        | `sym_src_mtx_`   |
| `frames_rendered_`      | audio thread        | main loop (`tick`)  | `std::atomic`    |
| `web_root_`             | set before `start()`| I/O thread          | none (read-only) |

`send_text()` / `send_binary()` are both serialised by `send_mutex_`. Today every
send originates on the main loop, so the mutex is not strictly required — it is
kept deliberately so a future off-main-thread sender stays wire-safe (frames must
not interleave).

`web_root_` is set once via `set_web_root()` **before** `start()` spawns the I/O
thread (the thread-start is the happens-before edge) and is never mutated after,
so the I/O thread reads it without a lock.

## Known limitation — single client, blocking

`WsServer` is single-client by design ("one operator GUI per running bridge", see
the header doc). While a WebSocket session is open, `handle_client()` owns the I/O
thread and parks in its `recv()` loop, so the `accept()` loop **cannot serve any
further HTTP requests until the client disconnects**.

This is fine for the normal flow: the browser fetches `index.html` → `app.js` →
`styles.css` over HTTP first, and only *then* does `app.js` open the WebSocket. A
mid-session reload tears down the old WebSocket (freeing the I/O thread) before
fetching again. What is **not** supported:
- a second browser tab connecting while one is live,
- any HTTP fetch *during* an open WebSocket session (e.g. a late `favicon.ico`,
  an image added later).

The backlog was raised to `listen(..., 16)` so the burst of parallel connections
during initial page load is not dropped, but they are still serviced one-at-a-time
by the single I/O thread before the WS opens.

## Upgrade path (if true HTTP+WS concurrency is needed)

Move the WebSocket *session* off the accept thread so `accept()` keeps running:

1. On WS upgrade, spawn the session (`handle_client`) on its own thread (`std::jthread`
   or a detached `std::thread` tracked for join on `stop()`), then loop back to
   `accept()` immediately. The accept loop can then keep serving static HTTP and
   accept further connections concurrently.
2. Decide the multi-client policy. The current model has a single `client_` and a
   single `send_*` target. Two real options:
   - **Keep single-operator:** accept a new WS only if none is active (else serve
     it a 503 / reject the upgrade), but still serve HTTP concurrently. Smallest
     change; preserves the "one operator GUI" contract.
   - **Go multi-client:** replace `client_` with a registry of connections and make
     `send_text()`/`send_binary()` fan out (or target). Larger change; the
     controller's event callbacks would broadcast to all connected GUIs.
3. Lifetime/locking: `client_` lifetime now spans another thread — guard
   close/teardown so `send_*` cannot write to a socket being closed (the existing
   `send_mutex_` plus an atomic "valid" flag, or per-connection ownership in the
   multi-client design). `stop()` must join all session threads.

Given the project's explicit single-operator design assumption, option (2a)
(detached session thread + reject second WS, keep HTTP concurrent) is the
recommended minimal upgrade if the limitation ever bites; full multi-client is a
deliberate scope expansion, not a bug fix.
