# openALE

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)]() [![License](https://img.shields.io/badge/license-MIT-blue)]() [![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)]()

**Repository:** https://github.com/dl3hc/openALE
**Maintainer:** DL3HC
**License:** MIT

> **Early development status.** This project is in a very early stage of development. Expect
> rough edges, unfinished features, bugs, and design decisions that still need to be revisited
> and polished. Several spec-conformant features — including Group/Net calls, SELCALL, and the
> FED-STD-1052 (FS-1052) ARQ modem/modes — are in some cases only partially prepared, not fully
> implemented, or not implemented at all. Support from other developers (code, testing, review,
> documentation) is actively sought — see [Contributing](#contributing).
>
> **No warranty — use at your own risk.** This software controls real radio transmitters. The
> maintainer assumes no liability for damage to equipment or for personal injury resulting from
> its use. You are solely responsible for verifying correct, safe, and legal operation of your
> station before transmitting.

openALE is a C++17 implementation of **MIL-STD-188-141B Automatic Link Establishment (ALE)**
and **FED-STD-1052 ARQ** for HF radio, built for amateur and experimental HF networks. It
started as a fork of [PC-ALE 2.0](https://github.com/Alex-Pennington/PC-ALE) by Alex Pennington
(AAM402/KY4OLB) and has since diverged substantially — see [Acknowledgments](#acknowledgments)
for attribution.

---

## Features

- **2G ALE protocol stack** — 8-FSK modem, Golay(24,12) FEC, word/preamble parsing, link state
  machine (IDLE → SCANNING → CALLING → HANDSHAKE → LINKED → SOUNDING)
- **AQC-ALE extensions** — Data Elements, traffic classes, slotted response --> TBD
- **FED-STD-1052 ARQ** — selective-repeat ARQ, CRC-32, configurable data rates --> TBD
- **LQA** — bilateral BER/SINAD-led channel scoring, auto-sounding, auto-relink, and an optional
  solar-elevation/SFI propagation-aware ranking factor
- **Radio control** — Hamlib CAT/PTT (required at build time; see [Build](#build))
- **WebSocket bridge + Web GUI** — the `openALE` binary serves a browser-based desktop and
  mobile UI directly, no separate web server needed
- **ale_monitor** — passive RX-only traffic/LQA monitor
- **Channel/Net model** — per-net channel files (`.ale`), per-net scanning/sounding policy

---

## Build

Requires a C++17 compiler, CMake ≥ 3.15, and **Hamlib** (mandatory — the build fails with a
`FATAL_ERROR` if Hamlib isn't found, since the bridge unconditionally calls into it).

**Linux:**
```bash
sudo apt install libhamlib-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --verbose
```

**Windows (one-time, MSYS2 MinGW64 shell):**
```bash
pacman -S git base-devel mingw-w64-x86_64-toolchain automake autoconf libtool
bash scripts/build_hamlib.sh
```
This builds Hamlib 4.5.2 as a static library into `libs/hamlib-built/`, which CMake then
detects and links automatically:
```powershell
mkdir build; cd build; cmake ..; cmake --build .
```
End users don't need to do this — built binaries are provided with each release.

---

## Run

```bash
./build/openALE --port 8765 [--mobile] [--remote]
```
Then open `http://localhost:8765` in a browser for the desktop UI (or the `--mobile` UI on a
phone). `--remote` binds to `0.0.0.0` for LAN access instead of localhost only.

For scripted/headless control, `tools/ale_cli.py` drives the same WebSocket API. For testing
without radio hardware, `radio_mock.exe` emulates a Hamlib `rigctld`-compatible TRX over TCP.


---

## Documentation

- [AMD Orderwire](docs/AMD_ORDERWIRE.md) — Automatic Message Display (A.5.7.2)
- [Channel/Net/Contact Format](docs/CHANNEL_NET_FORMAT.md) — `.ale` station file + WS management API
- [LQA Bilateral Exchange & Auto-Relink](docs/LQA_BILATERAL_RELINK.md) — A.5.4.4/A.5.4.5
- [Propagation-Aware LQA Scoring](docs/LQA_PROPAGATION.md) — solar-elevation/SFI ranking factor
- [Voice Audio Routing](docs/VOICE_AUDIO_ROUTING.md)
- [Threading Model](docs/THREADING.md)

---

## Standards Compliance

| Parameter | Value | Standard |
|-----------|-------|----------|
| ALE | MIL-STD-188-141B Appendix A | 2G ALE |
| ARQ | FED-STD-1052 | Data Link Protocol |
| Modulation | 8-FSK, 750–2500 Hz | Narrowband HF |
| Symbol rate | 125 baud | 250 Hz tone spacing |
| FEC | Golay (24,12) | 3-bit correction |
| Sample rate | 8000 Hz | Audio sampling |

---

## Contributing

Most existing ALE software is either commercial, licensed, or gated behind organizational
membership. openALE exists because ham radio — and the tools built for it — should be open,
free, and inspectable by anyone: to learn from, to teach with, to study and extend for research,
and to actually use if you live somewhere without telecommunications infrastructure, or simply
can't afford access to it. HF/ALE needs no infrastructure, no subscriptions, and has no gatekeeper.

Contributions welcome:
code/PRs, bug reports, hardware compatibility testing, on-air interop testing, and documentation.

---

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

openALE was originally forked from [PC-ALE 2.0](https://github.com/Alex-Pennington/PC-ALE) by
Alex Pennington (AAM402/KY4OLB); the platform-abstraction interfaces originate from
[PC-ALE-PAL](https://github.com/Alex-Pennington/PC-ALE-PAL). The codebase has since diverged
substantially from the original. Built against the public **MIL-STD-188-141B** and
**FED-STD-1052** specifications.
