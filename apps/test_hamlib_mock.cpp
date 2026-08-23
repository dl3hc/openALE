/**
 * test_hamlib_mock.cpp
 * Connects to the radio_mock via hamlib NET_RIGCTL (model 2) and replicates the
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
#include <hamlib/rig.h>

static void set_mode_n(RIG* rig, rmode_t mode, const char* tag)
{
    const int ret = rig_set_mode(rig, RIG_VFO_CURR, mode, RIG_PASSBAND_NORMAL);
    fprintf(stderr, "TEST %s: rig_set_mode(%s) -> %s\n",
            tag, rig_strrmode(mode), rigerror(ret));
}

int main(int argc, char* argv[])
{
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1:18769";

    rig_set_debug(RIG_DEBUG_TRACE);   // show hamlib's elision decisions
    rig_load_all_backends();

    RIG* rig = rig_init(2);  // 2 = NET_RIGCTL
    if (!rig) { fprintf(stderr, "rig_init failed\n"); return 1; }

    rig->state.rigport.type.rig = RIG_PORT_NETWORK;
#ifdef _MSC_VER
    strncpy_s(rig->state.rigport.pathname, HAMLIB_FILPATHLEN, host, HAMLIB_FILPATHLEN - 1);
#else
    strncpy(rig->state.rigport.pathname, host, HAMLIB_FILPATHLEN - 1);
#endif

    int ret = rig_open(rig);
    if (ret != RIG_OK) {
        fprintf(stderr, "rig_open failed: %s\n", rigerror(ret));
        rig_cleanup(rig);
        return 1;
    }
    fprintf(stderr, "\n=== OPEN OK ===\n\n");

    // Cache fully live over TCP (mirrors HamlibRadio's HAMLIB_CACHE_MODE=0).
    // NOTE: proven NOT to influence the elision — that is the \get_lock_mode
    // probe (see file header); kept so the probe matches production reads.
    rig_set_cache_timeout_ms(rig, HAMLIB_CACHE_ALL, 0);

    ret = rig_set_freq(rig, RIG_VFO_CURR, 14109000.0);
    fprintf(stderr, "TEST F: rig_set_freq(14109000) -> %s\n", rigerror(ret));

    // The critical sequence: one mode change, then identical re-asserts.
    // If the mock's wire log shows only ONE "M PKTUSB", hamlib elided the rest.
    set_mode_n(rig, RIG_MODE_PKTUSB, "M1 (change LSB->PKTUSB baseline)");
    set_mode_n(rig, RIG_MODE_PKTUSB, "M2 (identical re-assert)");
    set_mode_n(rig, RIG_MODE_PKTUSB, "M3 (identical re-assert)");

    // Live readback between re-asserts (mirrors sync_from_radio / assert_mode).
    rmode_t   got_mode; pbwidth_t got_bw;
    ret = rig_get_mode(rig, RIG_VFO_CURR, &got_mode, &got_bw);
    fprintf(stderr, "TEST m: rig_get_mode -> %s mode=%s bw=%ld\n",
            rigerror(ret), rig_strrmode(got_mode), (long)got_bw);

    set_mode_n(rig, RIG_MODE_PKTUSB, "M4 (re-assert after live readback)");

    // Different mode then back — both should always hit the wire.
    set_mode_n(rig, RIG_MODE_PKTLSB, "M5 (change PKTUSB->PKTLSB)");
    set_mode_n(rig, RIG_MODE_PKTUSB, "M6 (change PKTLSB->PKTUSB)");

    // Same-freq repeat — does rig_set_freq get elided too?
    ret = rig_set_freq(rig, RIG_VFO_CURR, 14109000.0);
    fprintf(stderr, "TEST F2: rig_set_freq(14109000 repeat) -> %s\n", rigerror(ret));

    // RFPOWER round-trip (P2-02 regression guard). radio_mock advertises
    // RIG_LEVEL_RFPOWER in its dump_state (0x1000 bit) — confirm hamlib's
    // capability probe picks that up, then set/get and confirm the value
    // survives the wire.
    const bool has_set = rig_has_set_level(rig, RIG_LEVEL_RFPOWER) != 0;
    const bool has_get = rig_has_get_level(rig, RIG_LEVEL_RFPOWER) != 0;
    fprintf(stderr, "TEST L0: rig_has_set_level(RFPOWER)=%d rig_has_get_level(RFPOWER)=%d\n",
            has_set, has_get);

    value_t val{};
    val.f = 0.5f;
    ret = rig_set_level(rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, val);
    fprintf(stderr, "TEST L1: rig_set_level(RFPOWER, 0.5) -> %s\n", rigerror(ret));

    value_t got{};
    ret = rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &got);
    fprintf(stderr, "TEST L2: rig_get_level(RFPOWER) -> %s val=%.3f\n",
            rigerror(ret), got.f);

    rig_close(rig);
    rig_cleanup(rig);
    return 0;
}
