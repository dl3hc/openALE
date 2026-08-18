/**
 * \file tests/gen/unit/test_amd_calling_frame.cpp
 * \brief AMD display decoupled from handshake outcome (A.5.7.2.2/A.5.7.2.3)
 *
 * Regression test for the reported bug: a station (e.g. DC7SU) embeds an AMD
 * message directly in the calling frame — TO[self] FROM[peer] CMD… DATA/REP…
 * TIS[peer] — the primary place A.5.7.1 puts AMD. Per A.5.7.2.2, "The
 * receiving station shall be capable of receiving an AMD message contained
 * in any ALE frame, including calls, responses, and acknowledgments," and
 * A.5.7.2.3 requires display "upon arrival" — independent of whether the
 * ensuing handshake actually completes.
 *
 * Before this fix, openALE only listened for AMD in the ACK frame
 * (HANDSHAKE/WAIT_ACK) and over an established link — never in the calling
 * frame or the response frame — so a calling-frame AMD was silently dropped
 * and the station just attempted (and, if the peer never sends a real ACK,
 * timed out) a link handshake.
 *
 * Verifies:
 *   TEST 1  Calling-frame AMD is displayed even when the handshake times out
 *           (no ACK ever arrives) — the exact reported scenario.
 *   TEST 2  Calling-frame AMD is displayed exactly once when the handshake
 *           goes on to link successfully (no duplicate/missing dispatch).
 *   TEST 3  Response-frame AMD (calling-station side, receiving the callee's
 *           response) is displayed correctly attributed, once the responder's
 *           own address is confirmed.
 */

#include "App/ale_controller.h"
#include "PAL/events.h"
#include "App/ale_event_data.h"
#include "Word/ale_word.h"
#include "Protocol/Control/ale_timing.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

namespace {

ALEWord make_word(PreambleType type, const char a3[3])
{
    const char ch[3] = { a3[0], a3[1], a3[2] };
    return WordParser::make_word(type, ch);
}

void rx_at(ALEController& ctrl, uint32_t t, PreambleType type, const char* a3)
{
    ctrl.update(t);
    ctrl.test_inject_rx_word(make_word(type, a3));
}

// Captures the most recent ALE_AMD_RECEIVED payload delivered via the PAL
// event bus (the same mechanism amd_dispatch() uses in production).
struct AmdCapture {
    int         count = 0;
    std::string self_addr, peer_addr, text;

    void reset() { count = 0; self_addr.clear(); peer_addr.clear(); text.clear(); }
};

AmdCapture g_cap;

void install_amd_capture()
{
    pal::get_event_handler()->on(pal::EventType::ALE_AMD_RECEIVED,
        [](const pal::Event& ev) {
            const auto* ad = static_cast<const AmdData*>(ev.data);
            ++g_cap.count;
            g_cap.self_addr = ad->self_addr;
            g_cap.peer_addr = ad->peer_addr;
            g_cap.text      = ad->text;
        });
}

} // namespace

// ── TEST 1 ───────────────────────────────────────────────────────────────────
// SAM calls JOE with AMD embedded in the calling frame, then never sends an
// ACK. JOE must still display the AMD text, even though the handshake times
// out back to a pre-link state.
void test_calling_frame_amd_survives_handshake_timeout()
{
    std::cout << "[TEST 1] Calling-frame AMD survives handshake timeout (no ACK)\n";
    g_cap.reset();

    ALEController ctrl;
    ctrl.add_self_address("JOE");   // default IDLE, no audio device (offline TX draining)

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const uint32_t t0   = 1000;

    // Wire format (A.5.5.3.1 leading call + A.5.7.2.2 AMD in the message
    // section): scanning TO×2, leading TO×2, CMD "HEL" + DATA "LO ", TIS SAM.
    uint32_t slot = 0;
    auto at = [&]() { return t0 + (slot++) * Trw; };
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::CMD,  "HEL");
    rx_at(ctrl, at(), PreambleType::DATA, "LO ");
    const uint32_t t_last = at();
    ctrl.update(t_last);
    ctrl.test_inject_rx_word(make_word(PreambleType::TIS, "SAM"));

    const bool amd_fired_early = g_cap.count > 0;   // must NOT fire before caller settles

    // Settle → SLOT_WAIT → CHANNEL_CHECK → SENDING_RESPONSE. Response words are
    // first buffered behind the PTT lead (config_.ptt_lead_ms, default 10 ms —
    // see ALEController::wire_callbacks()/tick_ptt_timing()); a later update()
    // flushes them to the modulator, and tick_offline_completion() (no audio
    // device attached) drains them synchronously, same as real headless/CLI use.
    ctrl.update(t_last + Tdrw + 1);
    ctrl.update(t_last + Tdrw + 2);
    const uint32_t lbt0 = t_last + Tdrw + 2;
    ctrl.update(lbt0 + Tdrw);         // CHANNEL_CHECK clear → SENDING_RESPONSE (words PTT-buffered)
    const uint32_t ack0 = lbt0 + Tdrw + 11;
    ctrl.update(ack0);                // PTT lead elapses → flush → drained → WAIT_ACK

    const bool in_wait_ack = ctrl.state() == ALEState::HANDSHAKE;
    const bool amd_still_pending = g_cap.count == 0;   // not dispatched yet — peer not yet "settled" as done

    // No ACK arrives. Advance past JOE's Twr window (same window the SM tests
    // use — Twr_slow + Tdrw + (Tdrw−Tlww)).
    // A generous margin past the nominal Twr window — this test isn't chasing
    // the exact boundary (that's covered elsewhere); it just needs the
    // handshake to genuinely time out.
    const uint32_t twr_window = ale::Twr_slow_int
                              + static_cast<uint32_t>(ale::Tdrw_ms)
                              + (ALETimingConstants::Tdrw_ms - ALETimingConstants::Tlww_ms);
    ctrl.update(ack0 + twr_window + 5000);

    const bool aborted   = ctrl.state() != ALEState::HANDSHAKE;
    const bool amd_fired = g_cap.count == 1;
    const bool peer_ok   = g_cap.peer_addr == "SAM";
    const bool self_ok   = g_cap.self_addr == "JOE";
    const bool text_ok   = g_cap.text == "HELLO";

    assert(!amd_fired_early && "AMD must not dispatch before the caller settles");
    assert(in_wait_ack && "JOE must have reached WAIT_ACK (response was sent)");
    assert(amd_still_pending && "AMD dispatch must wait for HANDSHAKE exit, not fire mid-handshake");
    assert(aborted && "handshake must have timed out (no ACK) — this is the reported scenario");
    assert(amd_fired && "AMD must be dispatched exactly once despite the timeout");
    assert(peer_ok && "AMD must be attributed to SAM");
    assert(self_ok && "AMD self_addr must be JOE");
    assert(text_ok && "AMD text must be the reassembled 'HELLO'");

    std::cout << "  response sent, reached WAIT_ACK: " << (in_wait_ack ? "PASS" : "FAIL") << "\n";
    std::cout << "  AMD not dispatched mid-handshake: " << (amd_still_pending ? "PASS" : "FAIL") << "\n";
    std::cout << "  handshake timed out (no ACK):     " << (aborted ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(ctrl.state()) << ")\n";
    std::cout << "  AMD dispatched exactly once:      " << (amd_fired ? "PASS" : "FAIL") << "\n";
    std::cout << "  self=\"" << g_cap.self_addr << "\" peer=\"" << g_cap.peer_addr
              << "\" text=\"" << g_cap.text << "\"  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
// Same calling-frame AMD, but this time SAM completes the ACK — JOE links.
// AMD must still fire exactly once (not lost, not duplicated).
void test_calling_frame_amd_fires_once_on_successful_link()
{
    std::cout << "[TEST 2] Calling-frame AMD fires once on successful link\n";
    g_cap.reset();

    ALEController ctrl;
    ctrl.add_self_address("JOE");

    const uint32_t Trw  = ALETimingConstants::Trw_ms;
    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    const uint32_t t0   = 1000;

    uint32_t slot = 0;
    auto at = [&]() { return t0 + (slot++) * Trw; };
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::TO,   "JOE");
    rx_at(ctrl, at(), PreambleType::CMD,  "SUC");
    const uint32_t t_last = at();
    ctrl.update(t_last);
    ctrl.test_inject_rx_word(make_word(PreambleType::TIS, "SAM"));

    ctrl.update(t_last + Tdrw + 1);
    ctrl.update(t_last + Tdrw + 2);
    const uint32_t lbt0 = t_last + Tdrw + 2;
    ctrl.update(lbt0 + Tdrw);           // CHANNEL_CHECK clear → SENDING_RESPONSE (words PTT-buffered)
    const uint32_t ack0 = lbt0 + Tdrw + 11;
    ctrl.update(ack0);                  // PTT lead elapses → flush → drained → WAIT_ACK

    // SAM's ACK arrives well within JOE's Twr window: "TO JOE" then "TIS SAM".
    ctrl.update(ack0 + 100);
    ctrl.test_inject_rx_word(make_word(PreambleType::TO, "JOE"));
    ctrl.update(ack0 + 100 + Trw);
    ctrl.test_inject_rx_word(make_word(PreambleType::TIS, "SAM"));
    ctrl.update(ack0 + 100 + Trw + Tdrw + 1);   // ACK conclusion settle → LINKED

    const bool linked    = ctrl.state() == ALEState::LINKED;
    const bool amd_fired = g_cap.count == 1;
    const bool peer_ok   = g_cap.peer_addr == "SAM";
    const bool text_ok   = g_cap.text == "SUC";

    assert(linked && "JOE must reach LINKED on a valid in-window ACK");
    assert(amd_fired && "AMD must fire exactly once on the success path too");
    assert(peer_ok && text_ok);

    std::cout << "  reached LINKED: " << (linked ? "PASS" : "FAIL")
              << " (state=" << ALEStateMachine::state_name(ctrl.state()) << ")\n";
    std::cout << "  AMD dispatched exactly once (count=" << g_cap.count << "), text=\""
              << g_cap.text << "\"  " << (amd_fired && text_ok ? "PASSED" : "FAILED") << "\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
// SAM (the calling station) receives JOE's response with AMD embedded in it.
// JOE's identity is only confirmed by its own TIS conclusion, processed by
// the SM after this accumulator sees the same word — so, like the
// calling-frame case, dispatch is deferred until CALLING is left.
void test_response_frame_amd_dispatches_immediately()
{
    std::cout << "[TEST 3] Response-frame AMD dispatched once CALLING settles\n";
    g_cap.reset();

    ALEController ctrl;
    ctrl.add_self_address("SAM");
    const bool ok = ctrl.initiate_call("JOE");
    assert(ok && "initiate_call must succeed");
    assert(ctrl.state() == ALEState::CALLING && "initiate_call must enter CALLING");

    const uint32_t Trw = ALETimingConstants::Trw_ms;

    // Drive SAM's own TX (leading call) via the offline auto-drain path, then
    // feed JOE's response: TO SAM, CMD "OK!", TIS JOE.
    uint32_t t = 0;
    ctrl.update(t);
    while (ctrl.get_calling_phase() != CallingPhase::LISTENING && t < 20000) {
        t += Trw;
        ctrl.update(t);
    }
    assert(ctrl.get_calling_phase() == CallingPhase::LISTENING && "SAM must reach LISTENING");

    const uint32_t Tdrw = ALETimingConstants::Tdrw_ms;
    uint32_t slot = 0;
    auto at = [&]() { return t + (slot++) * Trw; };
    rx_at(ctrl, at(), PreambleType::TO,  "SAM");
    rx_at(ctrl, at(), PreambleType::CMD, "OKY");
    rx_at(ctrl, at(), PreambleType::TIS, "JOE");
    const uint32_t t_last = at();

    // Let JOE's conclusion settle so CALLING is left (→ SENDING_ACK → LINKED),
    // which is where the buffered response-frame AMD gets dispatched.
    ctrl.update(t_last + Tdrw + 1);
    ctrl.update(t_last + 2 * Tdrw + 20);   // PTT-lead margin for the ACK burst to drain

    const bool amd_fired = g_cap.count == 1;
    const bool peer_ok   = g_cap.peer_addr == "JOE";
    const bool text_ok   = g_cap.text == "OKY";

    assert(amd_fired && "response-frame AMD must dispatch once CALLING settles");
    assert(peer_ok && text_ok);

    std::cout << "  AMD dispatched on TIS (count=" << g_cap.count << "), peer=\""
              << g_cap.peer_addr << "\" text=\"" << g_cap.text << "\"  "
              << (amd_fired && peer_ok && text_ok ? "PASSED" : "FAILED") << "\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==================================================\n";
    std::cout << "AMD display decoupled from handshake outcome\n";
    std::cout << "(A.5.7.2.2 / A.5.7.2.3)\n";
    std::cout << "==================================================\n\n";

    pal::set_event_handler(pal::create_event_handler());
    install_amd_capture();

    test_calling_frame_amd_survives_handshake_timeout();
    test_calling_frame_amd_fires_once_on_successful_link();
    test_response_frame_amd_dispatches_immediately();

    std::cout << "All tests PASSED.\n";
    return 0;
}
