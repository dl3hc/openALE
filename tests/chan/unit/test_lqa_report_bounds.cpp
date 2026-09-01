/**
 * @file test_lqa_report_bounds.cpp
 * @brief Regression guard for the Test-Channel "never finishes when Request LQA
 *        is enabled" bug and its underlying data-corruption twin.
 *
 * Root cause (both symptoms trace to LqaExchangeManager, include/LQA/lqa_exchange.h):
 *
 *   1. report_decoder_ (LQAReportDecoder) was never reset except by its own
 *      start()/feed()-completion cycle. An interrupted Block C5 report (dropped/
 *      corrupted DATA word -- exactly what happens on the marginal channels
 *      Test-Channel exists to sweep) left it stuck active, silently splicing
 *      every later unrelated DATA word (from ANY subsequent call) into the
 *      stale buffer and eventually decoding it as bogus LQA data.
 *
 *   2. apply_pending() queued a Block C5 report built from
 *      LQADatabase::get_entries_for_station(), which returns EVERY entry on
 *      file for a peer with no cap. Test-Channel's whole purpose is to
 *      populate many per-channel entries for one target across a sweep, so a
 *      long enough sweep (or channel plan) grows the report past the modem's
 *      MAX_TX_SEQUENCE_WORDS=64 transmit-queue budget -- an assert()/abort in
 *      Debug builds, or an impractically long response transmission in
 *      Release builds. Either way, the sweep never finishes.
 *
 * Fix: LqaExchangeManager::reset() (called on every new HANDSHAKE/CALLING
 * entry) discards an incomplete report_decoder_, and apply_pending() caps the
 * outgoing report to kMaxReportEntries regardless of how many entries the
 * peer has accumulated.
 */

#include "LQA/lqa_exchange.h"
#include "LQA/lqa_database.h"
#include "Word/ale_sequence.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace ale;

static int g_failures = 0;
static void check(bool cond, const char* msg)
{
    if (!cond) { std::cerr << "  FAIL: " << msg << std::endl; ++g_failures; }
    else       { std::cout << "  PASS: " << msg << std::endl; }
}

// ── Test 1: an incomplete report never survives reset() into a later call ──

static void test_reset_discards_incomplete_report()
{
    std::cout << "Test: reset() discards an incomplete Block C5 report decode..."
              << std::endl;

    LQADatabase db;
    std::vector<ALESequence> queued_reports;
    LqaExchangeManager mgr(
        db,
        [](uint32_t){},                                      // sm_queue_cmd_a (unused here)
        [&](ALESequence s){ queued_reports.push_back(std::move(s)); });

    // A peer announces a 10-entry report (10*36=360 bits -> ceil(360/21)=18 words).
    const uint32_t header = LQAReportEncoder::encode_report_cmd(10);
    mgr.on_report_cmd(header);

    // Only 3 of the 18 expected DATA words arrive before the link drops.
    for (int i = 0; i < 3; ++i)
        check(!mgr.on_report_data(0x000001u, "JOE",
                                   [](const std::string&){}),
              "partial feed does not complete the report");

    // New handshake/call starts (this is what ALEController now does on every
    // HANDSHAKE and CALLING entry) -- must discard the stale, incomplete decode.
    mgr.reset();

    // 20 arbitrary DATA words now arrive over the course of unrelated later
    // calls (ordinary address-extension / AMD content, not LQA report data at
    // all). Before the fix these would silently complete the stale 18-word
    // report and attribute garbage {freq,age,mp,sinad,ber} entries to whoever
    // "sender" happened to be at that later moment.
    bool any_bogus_completion = false;
    for (int i = 0; i < 20; ++i) {
        if (mgr.on_report_data(0x000002u, "BYSTANDER",
                                [](const std::string&){}))
            any_bogus_completion = true;
    }
    check(!any_bogus_completion,
          "post-reset unrelated DATA words never complete the stale report");
    check(db.get_entries_for_station("BYSTANDER", 999999.0f).empty(),
          "no phantom LQA entries were written for the unrelated sender");

    std::cout << "  PASS" << std::endl;
}

// Sanity counterpart: without an intervening reset(), the same 3+remaining
// word sequence DOES complete the decoder (proves the test actually exercises
// the vulnerable path, not a decoder that was already immune).
static void test_without_reset_stale_decoder_would_complete()
{
    std::cout << "Test: (sanity) without reset(), leftover words DO complete "
                 "the stale decoder..." << std::endl;

    LQADatabase db;
    LqaExchangeManager mgr(
        db,
        [](uint32_t){},
        [](ALESequence){});

    const uint32_t header = LQAReportEncoder::encode_report_cmd(10);  // 18 words
    mgr.on_report_cmd(header);
    for (int i = 0; i < 3; ++i)
        mgr.on_report_data(0x000001u, "JOE", [](const std::string&){});

    // No reset() here -- feed the remaining 15 words from an "unrelated" sender.
    bool completed = false;
    for (int i = 0; i < 15; ++i)
        if (mgr.on_report_data(0x000002u, "BYSTANDER", [](const std::string&){}))
            completed = true;

    check(completed, "stale decoder completes on leftover words when never reset");
    check(!db.get_entries_for_station("BYSTANDER", 999999.0f).empty(),
          "...and writes a bogus entry for the unrelated sender (confirms the bug existed)");

    std::cout << "  PASS" << std::endl;
}

// ── Test 2: outgoing report is capped regardless of peer history size ──

static void test_outgoing_report_is_capped()
{
    std::cout << "Test: apply_pending() caps the outgoing report size..."
              << std::endl;

    LQADatabase db;
    // Seed 40 distinct-channel bilateral entries for JOE -- far more than fit
    // in MAX_TX_SEQUENCE_WORDS=64 alongside the CMD 'a'/CMD 'r' header and the
    // TO/TIS frame overhead. This is exactly what a long Test-Channel sweep
    // (or a large channel plan) legitimately accumulates for one target.
    // get_entries_for_station() age-filters against wall-clock "now", so the
    // seed timestamps must be recent, not an arbitrary fixed value.
    const uint32_t now0 = db.get_current_time_ms();
    for (uint32_t i = 0; i < 40; ++i)
        db.update_bilateral(3000000u + i * 100000u, "JOE",
                             /*sinad=*/20, /*ber=*/2, /*mp=*/1, /*ts=*/now0);

    ALESequence queued_report;
    bool        report_queued = false;
    LqaExchangeManager mgr(
        db,
        [](uint32_t){},
        [&](ALESequence s){ queued_report = std::move(s); report_queued = true; });

    // JOE (peer) sent CMD 'a' with KA1=1 -- SAM must reply with a Block C5 report.
    LQACmdPayload p;
    p.ka1 = true;
    mgr.on_word_lqa_cmd(encode_lqa_cmd(p), 3000000u);

    check(mgr.apply_pending("JOE", /*can_queue_c5=*/true,
                             [](const std::string&){}),
          "apply_pending() reports a bilateral payload was present");
    check(report_queued, "a Block C5 report was queued");

    // 40 entries uncapped would need ceil(40*36/21)=69 DATA words + 1 header =
    // 70 words, already over MAX_TX_SEQUENCE_WORDS=64 on its own (before even
    // adding CMD 'a'/TO/TIS overhead). The cap must keep this well under budget.
    std::cout << "    queued report: " << queued_report.size() << " words" << std::endl;
    check(queued_report.size() < 64,
          "queued report stays under the modem's MAX_TX_SEQUENCE_WORDS budget");
    check(queued_report.size() <= 36,
          "queued report matches the expected 20-entry cap (1 header + <=35 data words)");

    std::cout << "  PASS" << std::endl;
}

int main()
{
    test_without_reset_stale_decoder_would_complete();
    test_reset_discards_incomplete_report();
    test_outgoing_report_is_capped();

    if (g_failures == 0) {
        std::cout << "\nAll LQA report bounds tests PASSED." << std::endl;
        return 0;
    }
    std::cerr << g_failures << " failure(s)." << std::endl;
    return 1;
}
