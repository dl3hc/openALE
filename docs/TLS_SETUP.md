# TLS (HTTPS/WSS) Setup

## Why

Browser APIs the Operator Audio Interface depends on — `getUserMedia`/
`enumerateDevices` (mic capture, device listing) and `AudioWorklet` (see
`docs/VOICE_AUDIO_ROUTING.md` §3.1) — only work in a **secure context**:
HTTPS, or `http://localhost`/`127.0.0.1` (which browsers special-case as
secure regardless of TLS). Opening the GUI from another device on the LAN —
`http://192.168.x.x:port/`, the whole point of the `--remote` flag — is
**not** a secure context, so those APIs are simply undefined in the browser:
no code-level workaround exists. `--tls` fixes this by serving the GUI over
real HTTPS/WSS instead.

## Usage

```
openALE --port 8080 --remote --tls
```

On first run with `--tls` and no `--cert`/`--key`, a self-signed certificate
and private key are generated automatically (`openale_cert.pem` /
`openale_key.pem` in the working directory) and reused on every subsequent
run. To supply your own certificate instead (e.g. one issued by an internal
CA):

```
openALE --port 8080 --remote --tls --cert mycert.pem --key mykey.pem
```

Then open `https://<lan-ip>:8080/` (or `https://localhost:8080/` on the same
machine) from a browser.

## The one-time browser warning

Self-signed certificates always trigger a browser warning on first connect
("Your connection is not private" / similar) — that's inherent to
self-signed TLS, not a bug. Click through it once ("Advanced → Proceed to
…", wording varies by browser); the browser remembers the exception for
that origin afterward. There is no way to avoid this without either a
CA-issued certificate (see `--cert`/`--key` above) or a browser-side flag
like `chrome://flags/#unsafely-treat-insecure-origin-as-secure`.

## Scope and known limitations

- **TLS-only, not dual-mode.** When `--tls` is enabled, the port only speaks
  TLS — there's no simultaneous plaintext fallback on the same port (avoids
  protocol-sniffing complexity). Without `--tls`, the bridge behaves exactly
  as before — plain HTTP/WS, no TLS involved at all.
- **No HTTP keep-alive.** Every static asset (index.html, app.js, styles.css,
  audio-worklets.js, …) is served as a fresh TCP connection with its own full
  TLS handshake — this server has never supported keep-alive, TLS or not.
  For a LAN tool serving a handful of small files this is a minor, one-time
  page-load cost, not something worth the complexity of fixing.
- **`rigctld_server.cpp`'s netrigctl-compat server is unaffected** — it's a
  separate plaintext TCP protocol for external tuner tools, not a browser
  connection, so it has no secure-context requirement to begin with.
- **10-year self-signed certificate validity.** Long enough that you won't
  need to re-trust a freshly regenerated cert in every browser you use
  unexpectedly; delete `openale_cert.pem`/`openale_key.pem` to force
  regeneration (e.g. if you want a fresh keypair).

## Implementation notes

TLS is provided by [mbedTLS](https://github.com/Mbed-TLS/mbedtls) (vendored
via CMake `FetchContent`, pinned to the 3.6.x LTS branch — see
`CMakeLists.txt`). `apps/bridge/tls_support.h`/`.cpp` wraps mbedTLS behind an
opaque `TlsConn`/`TlsServerContext` pair so `apps/bridge/ws_server.cpp` never
touches mbedTLS's own headers/types directly. Non-blocking handshake/I/O
integrates into the existing single-I/O-thread `poll()` loop the same way
partial-HTTP-request accumulation already did — no new threads, no change to
the threading model documented at the top of `ws_server.h`.
