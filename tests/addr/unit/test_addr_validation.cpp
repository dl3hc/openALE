/**
 * \file test_addr_validation.cpp
 * \brief AC-ADDR-001-003 — App-boundary address validation in ALEController.
 *
 * Verifies that ALEController::set_self_address() and initiate_call() /
 * initiate_group_call() reject addresses that violate the Basic-38 character
 * set or the 3–15 character length limit (A.5.2.4.2 / REQ-ADDR-001/002/004).
 *
 * The AddressEncoder (DD-007) silently truncates >15-char strings as a
 * protocol-layer safety net — this test confirms the app layer surfaces
 * the error to the caller before the encoder ever runs.
 */

#include "App/ale_controller.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

namespace ale {

// ============================================================================
// Part 1 — set_self_address() length and character validation
// ============================================================================

bool test_ac_addr_001_003_self_address_validation()
{
    std::cout << "\n[AC-ADDR-001-003] set_self_address(): reject invalid, accept valid\n";

    struct Case {
        const char* addr;
        bool        expect;
        const char* label;
    };
    const Case cases[] = {
        // ── valid ────────────────────────────────────────────────────────────
        { "BOB",             true,  "3-char min"               },
        { "SAM",             true,  "3-char (another)"         },
        { "W1AWJ",           true,  "5-char alphanumeric"      },
        { "ABCDEFGHIJKLMNO", true,  "15-char max"              },
        { "0123456789012",   true,  "13 digits"                },
        { "@?@",             true,  "AllCall-pattern (@?@)"    },
        { "@@?",             true,  "AnyCall-pattern (@@?)"    },
        // ── too short ────────────────────────────────────────────────────────
        { "",                false, "empty"                    },
        { "A",               false, "1 char (< 3)"             },
        { "AB",              false, "2 chars (< 3)"            },
        // ── too long ─────────────────────────────────────────────────────────
        { "ABCDEFGHIJKLMNOP",  false, "16 chars (> 15)"        },
        { "ABCDEFGHIJKLMNOPQRS", false, "19 chars (> 15)"      },
        // ── non-Basic-38 chars ───────────────────────────────────────────────
        { "BOB!",            false, "! (non-Basic-38)"         },
        { "bo b",            false, "space + lowercase"        },
        { "SAM\t",           false, "tab (non-Basic-38)"       },
        { "ABC#DEF",         false, "# (non-Basic-38)"         },
    };

    bool all_ok = true;
    ALEController ctrl;

    for (const auto& c : cases) {
        const bool result = ctrl.set_self_address(c.addr);
        const bool ok     = (result == c.expect);
        all_ok &= ok;
        std::cout << "  set_self_address(\"" << c.addr << "\") [" << c.label << "]: "
                  << (ok ? "PASS" : "FAIL")
                  << " (got " << (result ? "true" : "false")
                  << ", exp " << (c.expect ? "true" : "false") << ")\n";
    }

    // After a rejected call the previous valid address must be unchanged.
    ctrl.set_self_address("SAM");
    ctrl.set_self_address("TOOLONGADDRESSOVER15");  // must be rejected
    const bool kept = (ctrl.self() == "SAM");
    all_ok &= kept;
    std::cout << "  previous address preserved after rejected set: "
              << (kept ? "PASS" : "FAIL")
              << " (got \"" << ctrl.self() << "\")\n";

    return all_ok;
}

// ============================================================================
// Part 2 — initiate_call() rejects invalid target addresses
// ============================================================================

bool test_ac_addr_001_003_initiate_call_validation()
{
    std::cout << "\n[AC-ADDR-001-003] initiate_call(): reject invalid target address\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");

    struct Case {
        const char* addr;
        bool        expect_ok;
        const char* label;
    };
    // Invalid targets must be rejected before the SM is ever consulted.
    // Valid target "BOB" in IDLE state must start the call (SM returns true).
    const Case cases[] = {
        { "",                   false, "empty target"        },
        { "AB",                 false, "too short (2 chars)" },
        { "ABCDEFGHIJKLMNOP",   false, "too long (16 chars)" },
        { "BOB!",               false, "! non-Basic-38"      },
        { "BOB",                true,  "valid 3-char target" },
    };

    bool all_ok = true;
    for (const auto& c : cases) {
        ALEController fresh;
        fresh.set_self_address("SAM");
        const bool result = fresh.initiate_call(c.addr);
        const bool ok     = (result == c.expect_ok);
        all_ok &= ok;
        std::cout << "  initiate_call(\"" << c.addr << "\") [" << c.label << "]: "
                  << (ok ? "PASS" : "FAIL")
                  << " (got " << (result ? "true" : "false")
                  << ", exp " << (c.expect_ok ? "true" : "false") << ")\n";
    }
    return all_ok;
}

// ============================================================================
// Part 3 — initiate_group_call() rejects if any member is invalid
// ============================================================================

bool test_ac_addr_001_003_group_call_validation()
{
    std::cout << "\n[AC-ADDR-001-003] initiate_group_call(): reject if any member invalid\n";

    bool all_ok = true;

    // All valid → must accept (SM in IDLE accepts group calls).
    {
        ALEController ctrl;
        ctrl.set_self_address("SAM");
        const bool ok = ctrl.initiate_group_call({"BOB", "JOE", "ANN"});
        all_ok &= ok;
        std::cout << "  all valid members accepted: " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // One member too long → must reject.
    {
        ALEController ctrl;
        ctrl.set_self_address("SAM");
        const bool rejected = !ctrl.initiate_group_call({"BOB", "ABCDEFGHIJKLMNOP", "ANN"});
        all_ok &= rejected;
        std::cout << "  member > 15 chars rejected: " << (rejected ? "PASS" : "FAIL") << "\n";
    }

    // One member with non-Basic-38 char → must reject.
    {
        ALEController ctrl;
        ctrl.set_self_address("SAM");
        const bool rejected = !ctrl.initiate_group_call({"BOB", "BAD!"});
        all_ok &= rejected;
        std::cout << "  member with ! rejected: " << (rejected ? "PASS" : "FAIL") << "\n";
    }

    return all_ok;
}

// ============================================================================
// Main
// ============================================================================

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  AC-ADDR-001-003 — App-Grenzvalidierung (ALEController)   ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    int pass = 0, fail = 0;
    auto run = [&](const char* name, bool result) {
        if (result) { ++pass; }
        else        { ++fail; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    run("set_self_address() length + Basic-38 validation",
        test_ac_addr_001_003_self_address_validation());
    run("initiate_call() rejects invalid target",
        test_ac_addr_001_003_initiate_call_validation());
    run("initiate_group_call() rejects invalid member",
        test_ac_addr_001_003_group_call_validation());

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "║  Passed: " << std::setw(2) << pass
              << "  Failed: " << std::setw(2) << fail
              << "                                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    return (fail == 0) ? 0 : 1;
}

} // namespace ale

int main() { return ale::run_all_tests(); }
