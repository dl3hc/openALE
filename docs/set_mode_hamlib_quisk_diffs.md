Plan approved. Implementing now — starting with the openALE fix in hamlib_radio.cpp.

● Update(src\PAL\radios\hamlib_radio.cpp)
Added 14 lines
        // front-end (Quisk) may override the mode on a band change. A cached read
        // would just echo the value we set and defeat the verification.
        rig_set_cache_timeout_ms(rig_, HAMLIB_CACHE_MODE, 0);

        // hamlib's rig_set_mode() begins with an UNINITIALIZED `int locked_mode`,
        // fills it via rig_get_lock_mode() (return code ignored) and silently
        // returns RIG_OK WITHOUT transmitting when it is nonzero. Over netrigctl
        // the \get_lock_mode transaction fails against servers that don't
        // implement it (Quisk: "RPRT -4"; sscanf on that buffer writes nothing),
        // so whether ANY mode command reaches the radio depends on stack garbage
        // (hamlib 4.5 rig.c:2218; still present in upstream master rig.c:2812).
        // Nulling the backend hook makes rig_get_lock_mode() fall back to
        // rig->state.lock_mode — a real, zero-initialized field — so the elision
        // path is deterministically dead and every rig_set_mode() transmits.
        // Bonus: removes one wire round-trip per mode set (scan path gets faster).
        rig_->caps->get_lock_mode = nullptr;
        rig_->state.lock_mode = 0;
    }

    // Serielle Schnittstelle: DTR/RTS-Leitungszustand nach Open setzen,

Now the stale comment in set_channel():

● Update(src\PAL\radios\hamlib_radio.cpp)
Added 6 lines, removed 4 lines
    // Order: frequency FIRST, mode LAST. Some SDR front-ends (Quisk) restore a
    // per-band saved mode on a frequency change; sending mode last — then having
    // assert_mode() verify via live readback and re-send on mismatch — makes
    // openALE's channel mode authoritative. assert_mode()'s diagnostic prints
    // (level >= Info) provide the natural I/O latency (~5 ms on Windows stderr
    // that lets the SDR's async band restore complete before the retry fires.
    // No explicit sleep; no scan-rate impact.
    // openALE's channel mode authoritative. If the SDR's band restore lands
    // asynchronously AFTER assert_mode() returned, the deferred sync backstop
    // (ALEController::tick_mode_verify -> sync_from_radio) corrects it — no
    // sleeps here, no scan-rate impact. Both mechanisms only work because
    // start() neutralized hamlib's get_lock_mode probe: otherwise re-sent mode
    // commands may be silently elided inside rig_set_mode() (see start()).
    // Mode is sent on EVERY hop (no mode_changed guard): the rig may have been
    // retuned externally between sync_from_radio() polls. VFO = RIG_VFO_CURR;
    // passband = RIG_PASSBAND_NORMAL.

● Update(src\PAL\radios\hamlib_radio.cpp)
Added 7 lines, removed 1 line
// an SDR front-end (Quisk) may restore a per-band saved mode after a frequency
// change, silently reverting our mode. Because HAMLIB_CACHE_MODE is 0 (see
// start()), rig_get_mode() is a live query and actually observes that override
// The re-sends only reach the wire because start() nulled the backend's
// get_lock_mode hook — hamlib's rig_set_mode() otherwise consults an
// uninitialized lock flag and may elide the command while returning RIG_OK.
// A band-memory restore fires once per frequency change, so re-sending the mode
// wins; the attempt cap only bounds a persistently-rejecting backend.
// wins; the attempt cap only bounds a persistently-rejecting backend. If the
// restore lands after this loop already returned, the loop legitimately reports
// success on the pre-revert mode — the deferred tick_mode_verify checks
// (+300/700/1500 ms -> sync_from_radio) catch and correct that case.
int HamlibRadio::assert_mode(RadioMode mode) {
    const rmode_t target = to_hamlib_mode(mode);
    const char* mname = rig_strrmode(target);

openALE side done. Now the Quisk patch — first let me see GetPtt/SetPtt to match the class style.

  Searched for 1 pattern, ran 1 shell command

● Update(E:\repos\quisk\.venv\Lib\site-packages\quisk\quisk.py)
Added 1 line
    self.vfo = "Main"
    self.split_mode = 0
    self.split_vfo = 'VFO'
    self.lock_mode = 0
    h = self.Handlers = {}
    h[''] = self.ErrProtocol
    h['dump_state']  = self.DumpState

● Update(E:\repos\quisk\.venv\Lib\site-packages\quisk\quisk.py)
Added 2 lines
    h['set_split_freq']      = self.SetSplitFreq
    h['get_split_vfo']       = self.GetSplitVfo
    h['set_split_vfo']       = self.SetSplitVfo
    h['get_lock_mode']       = self.GetLockMode
    h['set_lock_mode']       = self.SetLockMode
    self.MakeDumpState()
  def MakeDumpState(self):
    dump_state = []

● Update(E:\repos\quisk\.venv\Lib\site-packages\quisk\quisk.py)
Added 13 lines
      self.ErrParam()
    else:
      self.app.pttButton.SetValue(ptt, True)
  def GetLockMode(self):
    # Hamlib sends \get_lock_mode before every rig_set_mode.  Replying
    # "RPRT -4" (unimplemented) leaves hamlib's lock variable UNINITIALIZED
    # and rig_set_mode may silently discard the mode command (hamlib bug,
    # rig.c rig_set_mode).  Reply with a real value so the variable is set.
    self.Reply('Locked', self.lock_mode, 0)
  def SetLockMode(self):
    lock = self.GetParamNumber()
    try:
      self.lock_mode = int(lock)
      self.Reply(0)
    except:
      self.ErrParam()
  def GetFunc(self):
    name = self.GetParamName()
    if name == '?': # send back supported functions

  Ran 1 shell command

● Update(apps\radio_mock.cpp)
Added 6 lines

static bool g_verbose = false;   // --verbose: prints every raw command

// \get_lock_mode reply. Default mimics QUISK ("RPRT -4" = unimplemented),
// which leaves hamlib's uninitialized lock variable UNWRITTEN and lets
// rig_set_mode() elide mode commands based on stack garbage (hamlib bug).
// --lock-mode ok mimics real rigctld ("0\n" data line = unlocked).
static bool g_lock_mode_ok = false;

// ── Radio state ───────────────────────────────────────────────────────────────

static double      s_freq_hz = 14'000'000.0;

● Update(apps\radio_mock.cpp)
Added 17 lines
        return "0\nVFOA\nRPRT 0\n";
    }

    // ── Lock mode ─────────────────────────────────────────────────────────
    // hamlib sends "\get_lock_mode" before EVERY rig_set_mode. The reply shape
    // decides whether hamlib's (uninitialized) lock variable gets written:
    //   "0\nRPRT 0\n" (real rigctld)  → lock=0 parsed, set_mode proceeds
    //   "RPRT -4\n"   (QUISK)         → sscanf matches nothing, lock stays
    //                                    stack garbage → set_mode may be elided
    if (cmd == "get_lock_mode") {
        if (g_verbose) {
            std::printf("[LOCK] get_lock_mode -> %s\n",
                        g_lock_mode_ok ? "0 (rigctld-style)" : "RPRT -4 (QUISK-style)");
            std::fflush(stdout);
        }
        return g_lock_mode_ok ? "0\nRPRT 0\n" : "RPRT -4\n";
    }
    if (cmd.rfind("set_lock_mode", 0) == 0)
        return g_lock_mode_ok ? "RPRT 0\n" : "RPRT -4\n";

    // ── PTT ───────────────────────────────────────────────────────────────
    // Short: "T 1"   Long: "set_ptt 1"
    if (cmd.rfind("set_ptt", 0) == 0 ||

● Update(apps\radio_mock.cpp)
Added 2 lines
            port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i],
            g_verbose = true;
        else if (std::strcmp(argv[i], "--lock-mode") == 0 && i + 1 < argc)
            g_lock_mode_ok = (std::strcmp(argv[++i], "ok") == 0);
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]); return 0;
        }

● Update(apps\radio_mock.cpp)
Added 4 lines, removed 2 lines
        "  %s --port N [--verbose]\n"
        "\n"
        "Options:\n"
        "  --port N    TCP-Port des rigctld-Listeners (Pflichtfeld)\n"
        "  --verbose   Zeigt jeden Rohbefehl von hamlib\n"
        "  --port N         TCP-Port des rigctld-Listeners (Pflichtfeld)\n"
        "  --verbose        Zeigt jeden Rohbefehl von hamlib\n"
        "  --lock-mode M    Antwort auf \\get_lock_mode: 'enimpl' (Default,\n"
        "                   QUISK-artig: RPRT -4) oder 'ok' (rigctld-artig: 0)\
        "  --lock-mode M    Antwort auf \\get_lock_mode: 'enimpl' (Default,\n"
        "                   QUISK-artig: RPRT -4) oder 'ok' (rigctld-artig: 0)\n"
        "\n"
        "Protokoll-Hinweise:\n"
        "  [WARN] VFO-prefixed … → hamlib sendet rigctld_vfo_mode=1-Befehle\n"

● Update(apps\radio_mock.cpp)
Added 2 lines
 *   s / \get_split_vfo  → liefert Split-Status (0 + VFOA) + RPRT 0
 *   T <0|1> / \set_ptt  → schaltet PTT
 *   t / \get_ptt        → liefert PTT-Status
 *   \get_lock_mode      → "RPRT -4" (Default, QUISK-artig) bzw. "0" + RPRT 0
 *                         mit --lock-mode ok (rigctld-artig)
 *   Q / q               → Verbindung beenden
 *   alles andere        → RPRT 0  (ignoriert, kein Fehler)
 *

Now the regression test. Let me check the existing test style and the CMake wiring for hamlib-linked targets.

  Searched for 1 pattern, read 1 file, listed 1 directory, ran 3 shell commands

Style captured. Writing the regression test now:

● Write(tests\radio\unit\test_mode_reassert.cpp)
Wrote 320 lines to tests\radio\unit\test_mode_reassert.cpp
/**
 * @file test_mode_reassert.cpp
 * @brief Regression guard: mode re-assertion must survive hamlib's lock-probe UB.
 *
 * hamlib's rig_set_mode() begins with an UNINITIALIZED `int locked_mode`,
 * fills it via rig_get_lock_mode() (return code ignored) and silently returns
 * RIG_OK WITHOUT transmitting when it is nonzero. Over netrigctl the
 * \get_lock_mode transaction fails against servers that don't implement it
 * (Quisk replies "RPRT -4"; sscanf on that buffer writes nothing), so whether
 * ANY mode command reaches the radio depends on stack garbage (hamlib 4.5
… +310 lines

  Searched for 1 pattern, read 1 file
  Searched for 1 pattern, read 1 file

● Update(CMakeLists.txt)
Added 13 lines
target_include_directories(test_sfi_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SfiService COMMAND test_sfi_service)

# Regression: hamlib lock-probe UB must stay neutralized (HamlibRadio::start())
# and the mode re-assert backstop must converge against a Quisk-style server
# whose band memory reverts the mode asynchronously after a frequency change.
# Real pal::HamlibRadio over NET_RIGCTL against an in-process mock — needs hamlib.
if(HAMLIB_FOUND)
    add_executable(test_mode_reassert tests/radio/unit/test_mode_reassert.cpp)
    target_link_libraries(test_mode_reassert ale_pal)
    if(WIN32)
        target_link_libraries(test_mode_reassert ws2_32)
    endif()
    add_test(NAME ModeReassert COMMAND test_mode_reassert)
endif()

# ── Examples ──────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build example programs" ON)

Building the changed targets before writing the issue drafts:

  Ran 2 shell commands

Clean build. Running the regression test:

  Ran 6 shell commands

Diff captured. Writing both issue drafts:

● Write(docs\UPSTREAM_ISSUE_HAMLIB_lock_mode.md)
Wrote 125 lines to docs\UPSTREAM_ISSUE_HAMLIB_lock_mode.md
# Upstream issue draft — Hamlib

> Copy everything below into a new issue at https://github.com/Hamlib/Hamlib/issues
> (drop this header). Attach `mockA_out.txt` / `mockB_out.txt` wire logs if requested.

---

**Title:** `rig_set_mode()` silently discards the mode command (returns RIG_OK) based on an uninitialized variable when the peer does not implement `\get_lock_mode`

## Summary
… +115 lines

● Write(docs\UPSTREAM_ISSUE_QUISK_lock_mode.md)
Wrote 117 lines to docs\UPSTREAM_ISSUE_QUISK_lock_mode.md
# Upstream issue draft — Quisk

> Copy everything below into an issue/mail for the Quisk maintainer
> (James Ahlstrom, N2ADR — https://github.com/jimahlstrom/quisk or the
> n2adr.com contact). Replace `[hamlib issue link]` once the Hamlib issue
> from `UPSTREAM_ISSUE_HAMLIB_lock_mode.md` is filed. Drop this header.

---

**Title:** Hamlib server: implement `get_lock_mode`/`set_lock_mode` — works around a Hamlib client bug that silently discards mode commands
… +107 lines

● Update(apps\test_hamlib_mock.cpp)
Added 13 lines, removed 5 lines
/**
 * test_hamlib_mock.cpp
 * Connects to the radio_mock via hamlib NET_RIGCTL (model 2) and replicates the
 * exact HamlibRadio usage (cache TTLs, freq-then-mode, repeated re-asserts).
 * Run alongside radio_mock --verbose and count the M commands on the wire:
 * hamlib 4.x elides a rig_set_mode whose target equals its cached mode value —
 * it returns RIG_OK without transmitting. That elision silently defeats every
 * re-assert of a mode an SDR front-end reverted behind our back.
 * exact HamlibRadio usage (freq-then-mode, repeated re-asserts).
 * Run alongside radio_mock --verbose and count the M commands on the wire.
 *
 * FINDING (2026-07-13): hamlib elides rig_set_mode calls — RIG_OK returned,
 * nothing transmitted — via its \get_lock_mode probe, NOT via the cache:
 * rig_set_mode() branches on an UNINITIALIZED `int locked_mode` that
 * netrigctl_get_lock_mode() fails to write when the server's reply carries no
 * parseable integer ("RPRT -4" from Quisk, bare "RPRT 0" from a catch-all).
 * A/B proof with this probe: radio_mock --lock-mode ok (rigctld-style "0")
 * -> 6/6 M commands transmitted; default (QUISK-style "RPRT -4") -> only 4/6,
 * elision pattern deterministic per preceding call path (stack garbage).
 * HamlibRadio::start() neutralizes the probe (caps->get_lock_mode = nullptr);
 * regression guard: tests/radio/unit/test_mode_reassert.cpp.
 */
#include <cstdio>
#include <cstring>

● Update(apps\test_hamlib_mock.cpp)
Added 3 lines, removed 3 lines
    }
    fprintf(stderr, "\n=== OPEN OK ===\n\n");

    // Candidate fix: disable the hamlib cache entirely over TCP. The set-side
    // elision (rig_set_mode skipped + RIG_OK when target == cached mode) must
    // die too, or re-asserting a reverted mode is impossible.
    // Cache fully live over TCP (mirrors HamlibRadio's HAMLIB_CACHE_MODE=0).
    // NOTE: proven NOT to influence the elision — that is the \get_lock_mode
    // probe (see file header); kept so the probe matches production reads.
    rig_set_cache_timeout_ms(rig, HAMLIB_CACHE_ALL, 0);

    ret = rig_set_freq(rig, RIG_VFO_CURR, 14109000.0);