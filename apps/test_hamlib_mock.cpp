/**
 * test_hamlib_mock.cpp
 * Connects to the radio_mock via hamlib NET_RIGCTL (model 2) and replicates the
 * exact HamlibRadio usage (cache TTLs, freq-then-mode, repeated re-asserts).
 * Run alongside radio_mock --verbose and count the M commands on the wire:
 * hamlib 4.x elides a rig_set_mode whose target equals its cached mode value —
 * it returns RIG_OK without transmitting. That elision silently defeats every
 * re-assert of a mode an SDR front-end reverted behind our back.
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
    strncpy(rig->state.rigport.pathname, host, HAMLIB_FILPATHLEN - 1);

    int ret = rig_open(rig);
    if (ret != RIG_OK) {
        fprintf(stderr, "rig_open failed: %s\n", rigerror(ret));
        rig_cleanup(rig);
        return 1;
    }
    fprintf(stderr, "\n=== OPEN OK ===\n\n");

    // Candidate fix: disable the hamlib cache entirely over TCP. The set-side
    // elision (rig_set_mode skipped + RIG_OK when target == cached mode) must
    // die too, or re-asserting a reverted mode is impossible.
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

    rig_close(rig);
    rig_cleanup(rig);
    return 0;
}
