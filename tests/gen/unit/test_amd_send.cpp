/**
 * \file tests/gen/unit/test_amd_send.cpp
 * \brief Tests for ALEController::send_amd() dispatch + validation (A.5.7.2)
 *
 * send_amd(target, text) has two paths:
 *   - LINKED  → linked-orderwire frame to the active peer (target ignored)
 *   - not LINKED → queue AMD as pending message + initiate_call(target)
 *
 * These tests cover the not-LINKED dispatch and the validation guards, which
 * need no audio device and no established link. The LINKED-branch TX frame
 * shape is covered by test_amd_linked_orderwire.cpp at the SM level.
 *
 * Verifies:
 *   TEST 1  Empty text → ERROR
 *   TEST 2  Not LINKED + valid target → OK, SM enters CALLING (AMD queued)
 *   TEST 3  Not LINKED + empty/invalid target → ERROR (no call placed)
 *   TEST 4  active_peer() empty when not LINKED
 */

#include "App/ale_controller.h"
#include "Protocol/Control/ale_state_machine.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace ale;

static bool starts_with(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

// ── TEST 1 ───────────────────────────────────────────────────────────────────
void test_empty_text_error()
{
    std::cout << "[TEST 1] send_amd: empty text → ERROR\n";
    ALEController ctrl;
    ctrl.set_self_address("SAM");
    const std::string r = ctrl.send_amd("BOB", "");
    assert(starts_with(r, "ERROR:") && "empty text must ERROR");
    std::cout << "  '" << r << "'  PASSED\n\n";
}

// ── TEST 2 ───────────────────────────────────────────────────────────────────
void test_not_linked_dispatch_queues_and_calls()
{
    std::cout << "[TEST 2] send_amd not-LINKED → queue AMD + initiate_call\n";
    ALEController ctrl;
    ctrl.set_self_address("SAM");
    // IDLE default — no channels needed for a 1-word call to BOB.
    const std::string r = ctrl.send_amd("BOB", "HELLO");
    std::cout << "  reply: '" << r << "'\n";
    assert(starts_with(r, "OK:") && "valid not-LINKED send must return OK");
    assert(ctrl.state() == ALEState::CALLING && "SM must enter CALLING");
    std::cout << "  OK + state=CALLING  PASSED\n\n";
}

// ── TEST 3 ───────────────────────────────────────────────────────────────────
void test_not_linked_invalid_target_error()
{
    std::cout << "[TEST 3] send_amd not-LINKED + invalid target → ERROR (no call)\n";

    {
        ALEController ctrl;
        ctrl.set_self_address("SAM");
        const std::string r = ctrl.send_amd("", "HELLO");   // empty target
        assert(starts_with(r, "ERROR:") && "empty target must ERROR");
        assert(ctrl.state() == ALEState::IDLE && "no call placed on error");
    }
    {
        ALEController ctrl;
        ctrl.set_self_address("SAM");
        const std::string r = ctrl.send_amd("BOB!", "HELLO"); // non-Basic-38
        assert(starts_with(r, "ERROR:") && "invalid target must ERROR");
        assert(ctrl.state() == ALEState::IDLE && "no call placed on error");
    }
    std::cout << "  empty + invalid targets → ERROR, state stays IDLE  PASSED\n\n";
}

// ── TEST 4 ───────────────────────────────────────────────────────────────────
void test_active_peer_empty_when_not_linked()
{
    std::cout << "[TEST 4] active_peer() empty when not LINKED\n";
    ALEController ctrl;
    ctrl.set_self_address("SAM");
    assert(ctrl.active_peer().empty() && "active_peer must be empty in IDLE");
    std::cout << "  active_peer() == \"\"  PASSED\n\n";
}

// ── runner ────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "==============================================\n";
    std::cout << "ALEController::send_amd dispatch + validation\n";
    std::cout << "==============================================\n\n";

    test_empty_text_error();
    test_not_linked_dispatch_queues_and_calls();
    test_not_linked_invalid_target_error();
    test_active_peer_empty_when_not_linked();

    std::cout << "All tests PASSED.\n";
    return 0;
}