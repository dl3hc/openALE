/**
 * @file test_lqa_analyzer.cpp
 * @brief Unit tests for LQA Analyzer
 */

#include "LQA/lqa_analyzer.h"
#include "LQA/lqa_database.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>

using namespace ale;

void test_analyzer_creation() {
    std::cout << "Test: Analyzer creation..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    auto config = analyzer.get_config();
    assert(config.min_acceptable_score > 0.0f);
    
    std::cout << "  PASS" << std::endl;
}

void test_process_sounding() {
    std::cout << "Test: Process sounding..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Process sounding from remote station
    analyzer.process_sounding("REMOTE", 7073000, 22.0f, 0.001f);
    
    // Should update database
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(std::abs(entry->snr_db - 22.0f) < 0.5f);
    
    std::cout << "  PASS" << std::endl;
}

void test_process_sounding_extended() {
    std::cout << "Test: Process sounding extended..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    MetricsSample sample;
    sample.snr_db = 25.0f;
    sample.signal_power_dbm = -45.0f;
    sample.noise_power_dbm = -70.0f;
    sample.fec_errors_corrected = 1;
    sample.decode_success = true;
    sample.multipath_delay_ms = 2.0f;
    
    analyzer.process_sounding_extended("REMOTE", 7073000, sample);
    
    auto entry = db.get_entry(7073000, "REMOTE");
    assert(entry != nullptr);
    assert(std::abs(entry->snr_db - 25.0f) < 0.5f);
    assert(entry->multipath_score > 0.0f);
    
    std::cout << "  PASS" << std::endl;
}

void test_best_channel_for_station() {
    std::cout << "Test: Best channel for station..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Add soundings on different channels
    analyzer.process_sounding("REMOTE", 7073000, 22.0f, 0.001f);   // Good
    analyzer.process_sounding("REMOTE", 10142000, 18.0f, 0.01f);   // Fair
    analyzer.process_sounding("REMOTE", 14107000, 28.0f, 0.0005f); // Excellent
    
    // Get best channel
    auto best = analyzer.get_best_channel_for_station("REMOTE");
    
    assert(best != nullptr);
    assert(best->frequency_hz == 14107000);  // Should select highest SNR
    assert(best->score > 20.0f);
    
    std::cout << "  Best channel: " << best->frequency_hz << " Hz, Score: " 
              << best->score << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_best_channel_overall() {
    std::cout << "Test: Best overall channel..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Add soundings from different stations
    analyzer.process_sounding("ALFA", 7073000, 20.0f, 0.001f);
    analyzer.process_sounding("BRAVO", 10142000, 25.0f, 0.0005f);
    analyzer.process_sounding("CHARLIE", 14107000, 18.0f, 0.01f);
    
    auto best = analyzer.get_best_channel();
    
    assert(best != nullptr);
    assert(best->score > 15.0f);
    
    std::cout << "  Best channel: " << best->frequency_hz << " Hz, Score: " 
              << best->score << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_rank_all_channels() {
    std::cout << "Test: Rank all channels..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Add data for multiple channels
    analyzer.process_sounding("REMOTE", 7073000, 22.0f, 0.001f);
    analyzer.process_sounding("REMOTE", 10142000, 18.0f, 0.01f);
    analyzer.process_sounding("REMOTE", 14107000, 25.0f, 0.0005f);
    
    auto ranked = analyzer.rank_all_channels();
    
    assert(ranked.size() == 3);
    
    // Should be sorted by score (highest first)
    assert(ranked[0].score >= ranked[1].score);
    assert(ranked[1].score >= ranked[2].score);
    
    std::cout << "  Ranked channels:" << std::endl;
    for (const auto& rank : ranked) {
        std::cout << "    " << rank.frequency_hz << " Hz: " 
                  << rank.score << std::endl;
    }
    std::cout << "  PASS" << std::endl;
}

void test_rank_channels_for_station() {
    std::cout << "Test: Rank channels for specific station..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Add data for REMOTE on multiple channels
    analyzer.process_sounding("REMOTE", 7073000, 22.0f, 0.001f);
    analyzer.process_sounding("REMOTE", 10142000, 18.0f, 0.01f);
    
    // Add data for OTHER on same channels (should not affect REMOTE ranking)
    analyzer.process_sounding("OTHER", 7073000, 15.0f, 0.1f);
    
    auto ranked = analyzer.rank_channels_for_station("REMOTE");
    
    assert(ranked.size() == 2);  // Only REMOTE's channels
    assert(ranked[0].best_station == "REMOTE");
    
    std::cout << "  PASS" << std::endl;
}

void test_sounding_due() {
    std::cout << "Test: Sounding due detection..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    AnalyzerConfig config;
    config.sounding_interval_ms = 100;  // Very short for testing
    analyzer.set_config(config);
    
    // Channel with no data - sounding should be due
    assert(analyzer.is_sounding_due(7073000));
    
    // Add recent sounding
    analyzer.process_sounding("REMOTE", 7073000, 20.0f, 0.01f);
    
    // Sounding should not be due immediately
    assert(!analyzer.is_sounding_due(7073000));
    
    // Wait for interval
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Now sounding should be due
    assert(analyzer.is_sounding_due(7073000));
    
    std::cout << "  PASS" << std::endl;
}

void test_channels_needing_sounding() {
    std::cout << "Test: Channels needing sounding..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    AnalyzerConfig config;
    config.sounding_interval_ms = 100;
    analyzer.set_config(config);
    
    // Add soundings
    analyzer.process_sounding("REMOTE", 7073000, 20.0f, 0.01f);
    analyzer.process_sounding("REMOTE", 10142000, 22.0f, 0.005f);
    
    // Wait for interval
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    auto channels = analyzer.get_channels_needing_sounding();
    
    assert(channels.size() == 2);
    
    std::cout << "  Channels needing sounding: " << channels.size() << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_quality_summary() {
    std::cout << "Test: Quality summary..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    // Add data
    analyzer.process_sounding("REMOTE", 7073000, 25.0f, 0.001f);
    
    std::string summary = analyzer.get_channel_quality_summary(7073000);
    
    assert(!summary.empty());
    assert(summary.find("dB") != std::string::npos);
    
    std::cout << "  Summary: " << summary << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_station_quality_summary() {
    std::cout << "Test: Station quality summary..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    analyzer.process_sounding("REMOTE", 7073000, 22.0f, 0.001f);
    
    std::string summary = analyzer.get_station_quality_summary("REMOTE", 7073000);
    
    assert(!summary.empty());
    assert(summary.find("SNR") != std::string::npos);
    assert(summary.find("BER") != std::string::npos);
    
    std::cout << "  Summary: " << summary << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_min_acceptable_score() {
    std::cout << "Test: Minimum acceptable score threshold..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    AnalyzerConfig config;
    config.min_acceptable_score = 20.0f;  // High threshold
    analyzer.set_config(config);
    
    // Add low quality channel. Quality is BER-led (A.5.4.1.1): ber is the
    // non-unanimous 2/3-vote count (0–48), so a genuinely poor channel needs a
    // HIGH count (near 48), not a small "rate". ber=40 → ber_q=(1-40/48)*30=5;
    // with sinad=8 → from_q=0.7*5+0.3*8=5.9 → score ≈ 11 (< 20 threshold).
    analyzer.process_sounding("REMOTE", 7073000, 10.0f, 40.0f, 8.0f);  // Poor quality

    // Should return nullptr (below threshold)
    auto best = analyzer.get_best_channel_for_station("REMOTE");
    assert(best == nullptr);

    // Add high quality channel: near-zero BER count + high SINAD → score ≈ 30.
    analyzer.process_sounding("REMOTE", 10142000, 28.0f, 0.5f, 28.0f);  // Excellent
    
    // Should return the good channel
    best = analyzer.get_best_channel_for_station("REMOTE");
    assert(best != nullptr);
    assert(best->frequency_hz == 10142000);
    
    std::cout << "  PASS" << std::endl;
}

void test_sounding_callback() {
    std::cout << "Test: Sounding callback..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    AnalyzerConfig config;
    config.enable_automatic_sounding = true;
    config.sounding_interval_ms = 50;
    analyzer.set_config(config);
    
    // Track sounding requests
    std::vector<uint32_t> sounding_requests;
    analyzer.set_sounding_callback([&](uint32_t freq) {
        sounding_requests.push_back(freq);
    });
    
    // Add old sounding
    analyzer.process_sounding("REMOTE", 7073000, 20.0f, 0.01f);
    
    // Wait for interval
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Update should trigger callback
    analyzer.update();
    
    assert(!sounding_requests.empty());
    
    std::cout << "  Sounding requests: " << sounding_requests.size() << std::endl;
    std::cout << "  PASS" << std::endl;
}

void test_configuration() {
    std::cout << "Test: Configuration..." << std::endl;
    
    LQADatabase db;
    LQAAnalyzer analyzer(&db);
    
    AnalyzerConfig config;
    config.min_acceptable_score = 15.0f;
    config.sounding_interval_ms = 600000;
    config.prefer_recent_contacts = false;
    
    analyzer.set_config(config);
    
    auto retrieved = analyzer.get_config();
    assert(std::abs(retrieved.min_acceptable_score - 15.0f) < 0.1f);
    assert(retrieved.sounding_interval_ms == 600000);
    assert(retrieved.prefer_recent_contacts == false);
    
    std::cout << "  PASS" << std::endl;
}

// ── Bilateral channel ranking tests (AC-CHAN-005-001/002, A.5.4.5) ────────────

// Without bilateral data: existing behavior unchanged (fallback to composite score).
void test_bilateral_ranking_fallback_no_bilateral_data() {
    std::cout << "Test: bilateral_channel_score() falls back to composite score when no bilateral data..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // No SINAD here (sinad_db defaults to 0), so quality is driven by the BER
    // (2/3-vote count, A.5.4.1.1) — snr_db does not feed the score. Give the two
    // channels clearly different BER counts so the ranking is unambiguous.
    analyzer.process_sounding("REMOTE", 7073000,  22.0f, 10.0f);  // higher BER count
    analyzer.process_sounding("REMOTE", 10142000, 28.0f,  2.0f);  // lower BER count → better

    auto ranked = analyzer.rank_channels_for_station("REMOTE");
    assert(ranked.size() == 2);
    // Lower BER count → higher composite score → ranks first (A.5.4.1.1)
    assert(ranked[0].frequency_hz == 10142000);

    std::cout << "  PASS" << std::endl;
}

// Bilateral average (A.5.4.5.1): bilateral_channel_score = (FROM + TO) / 2.
// Both FROM and TO use BER-led blend: q = 0.7·ber_q + 0.3·sinad_q (A.5.4.1.1
// primary, A.5.4.1.2 secondary) so a perfect decode is not penalised by a low
// SINAD proxy floor.
void test_bilateral_average_formula() {
    std::cout << "Test: bilateral_channel_score() = (FROM + TO) / 2 (A.5.4.5.1)..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // FROM quality is BER-led: from_q = 0.7·ber_q + 0.3·sinad_q.
    // With ber≈0 (clean) ber_q≈30, so from_q ≈ 0.7·30 + 0.3·20 = 27 for SINAD=20.
    // TO quality uses same blend on bilateral codes: to_q = 0.7·(30−ber) + 0.3·sinad.
    //
    // C1: FROM→27; bilateral SINAD=10,BER=5 → to=0.7·25+0.3·10=20.5
    //     bilateral_score = (27 + 20.5) / 2 = 23.75
    db.update_entry_extended(7073000, "REMOTE", 20.0f, 0.001f,
                             20.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(7073000, "REMOTE", 10u, 5u, 0u);

    // C2: FROM→27; bilateral SINAD=20,BER=0 → to=0.7·30+0.3·20=27
    //     bilateral_score = (27 + 27) / 2 = 27.0
    db.update_entry_extended(10142000, "REMOTE", 20.0f, 0.001f,
                             20.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(10142000, "REMOTE", 20u, 0u, 0u);

    auto ranked = analyzer.rank_channels_for_station("REMOTE");
    assert(ranked.size() == 2);
    // C2 (avg=27.0) must rank above C1 (avg=23.75)
    assert(ranked[0].frequency_hz == 10142000);
    assert(ranked[1].frequency_hz == 7073000);
    assert(ranked[0].score > ranked[1].score);
    // Verify the average formula explicitly
    assert(std::abs(ranked[0].score - 27.0f) < 0.1f);
    assert(std::abs(ranked[1].score - 23.75f) < 0.1f);

    std::cout << "  ranked[0]=" << ranked[0].frequency_hz
              << " score=" << ranked[0].score
              << "  ranked[1]=" << ranked[1].frequency_hz
              << " score=" << ranked[1].score << std::endl;
    std::cout << "  PASS" << std::endl;
}

// Balance tiebreaker (A.5.4.5.1): when two channels have the same average
// bilateral score, the more balanced channel (lower FROM–TO imbalance) ranks
// first.
void test_bilateral_balance_tiebreaker() {
    std::cout << "Test: bilateral balance tiebreaker — balanced path preferred on tie (A.5.4.5.1)..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // FROM is BER-led: with ber≈0, from_q ≈ 0.7·30 + 0.3·sinad = 21 + 0.3·sinad.
    // TO is also BER-led: to_q = 0.7·(30−bilateral_ber) + 0.3·bilateral_sinad.
    // Choose inputs so both channels produce the same bilateral average (=27):
    //
    // C1: balanced — local SINAD=20 → FROM≈27, bilateral_sinad=20,BER=0 → TO=27
    //     avg=(27+27)/2=27, imbalance=|27−27|=0
    db.update_entry_extended(7073000, "REMOTE", 20.0f, 0.001f,
                             20.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(7073000, "REMOTE", 20u, 0u, 0u);  // to=0.7·30+0.3·20=27

    // C2: lopsided — local SINAD=30 → FROM≈30, bilateral_sinad=10,BER=0 → TO=24
    //     avg=(30+24)/2=27, imbalance=|30−24|=6
    db.update_entry_extended(10142000, "REMOTE", 30.0f, 0.001f,
                             30.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(10142000, "REMOTE", 10u, 0u, 0u);  // to=0.7·30+0.3·10=24

    auto ranked = analyzer.rank_channels_for_station("REMOTE");
    assert(ranked.size() == 2);
    // Both have the same average score (≈27)
    assert(std::abs(ranked[0].score - ranked[1].score) < 0.01f);
    // C1 (balanced, imbalance=0) must rank above C2 (lopsided, imbalance=20)
    assert(ranked[0].frequency_hz == 7073000);
    assert(ranked[1].frequency_hz == 10142000);

    std::cout << "  ranked[0]=" << ranked[0].frequency_hz
              << " score=" << ranked[0].score
              << "  ranked[1]=" << ranked[1].frequency_hz
              << " score=" << ranked[1].score << std::endl;
    std::cout << "  PASS" << std::endl;
}

// A.5.4.5.1: recently-failed handshake → deprioritise channel even if score is high.
void test_handshake_fail_penalty() {
    std::cout << "Test: handshake_fail_penalty — recently-failed channel ranked last (A.5.4.5.1)..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // C1: good quality, but recently failed handshake
    db.update_entry_extended(7073000, "REMOTE", 25.0f, 0.001f,
                             25.0f, 0.0f, -100.0f, 0, 10);
    db.record_handshake_fail(7073000, "REMOTE");  // just now

    // C2: slightly lower quality, no failure
    db.update_entry_extended(10142000, "REMOTE", 22.0f, 0.002f,
                             22.0f, 0.0f, -100.0f, 0, 10);

    auto ranked = analyzer.rank_channels_for_station("REMOTE");
    assert(ranked.size() == 2);
    // C2 must rank above C1 despite lower raw score, because C1 is penalised
    assert(ranked[0].frequency_hz == 10142000);
    assert(ranked[1].frequency_hz == 7073000);

    std::cout << "  ranked[0]=" << ranked[0].frequency_hz
              << " score=" << ranked[0].score
              << "  ranked[1]=" << ranked[1].frequency_hz
              << " score=" << ranked[1].score << std::endl;
    std::cout << "  PASS" << std::endl;
}

// A.5.4.6: rank_all_channels(BROADCAST) uses TO-direction; LISTENING uses FROM-direction.
void test_rank_all_channels_mode() {
    std::cout << "Test: rank_all_channels() mode selection (A.5.4.6)..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // C1: strong FROM (local SINAD=25), weak TO (bilateral_sinad=5)
    db.update_entry_extended(7073000, "REMOTE", 25.0f, 0.001f,
                             25.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(7073000, "REMOTE", 5u, 0u, 7u);   // TO poor

    // C2: weak FROM (local SINAD=5), strong TO (bilateral_sinad=25)
    db.update_entry_extended(10142000, "REMOTE", 5.0f, 0.1f,
                             5.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(10142000, "REMOTE", 25u, 0u, 7u);  // TO strong

    // BROADCAST → TO-priority: C2 (TO=25) must rank above C1 (TO=5)
    auto broadcast = analyzer.rank_all_channels(SelectionMode::BROADCAST);
    assert(broadcast.size() == 2);
    assert(broadcast[0].frequency_hz == 10142000);

    // LISTENING → FROM-priority: C1 (FROM=25) must rank above C2 (FROM=5)
    auto listening = analyzer.rank_all_channels(SelectionMode::LISTENING);
    assert(listening.size() == 2);
    assert(listening[0].frequency_hz == 7073000);

    std::cout << "  BROADCAST rank[0]=" << broadcast[0].frequency_hz
              << "  LISTENING rank[0]=" << listening[0].frequency_hz << std::endl;
    std::cout << "  PASS" << std::endl;
}

// A.5.4.6: min_path_score filter removes channels where no station meets the threshold.
void test_rank_all_channels_min_path_score() {
    std::cout << "Test: rank_all_channels() min_path_score filter (A.5.4.6)..." << std::endl;

    LQADatabase db;
    LQAAnalyzer analyzer(&db);

    // C1: good quality
    db.update_entry_extended(7073000, "REMOTE", 22.0f, 0.001f,
                             22.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(7073000, "REMOTE", 22u, 0u, 7u);

    // C2: poor quality (score will be low)
    db.update_entry_extended(10142000, "REMOTE", 4.0f, 0.3f,
                             4.0f, 0.0f, -100.0f, 0, 10);
    db.update_bilateral(10142000, "REMOTE", 4u, 25u, 7u);  // bilateral_ber=25 → very poor peer BER

    // Without filter: both channels returned
    auto all = analyzer.rank_all_channels();
    assert(all.size() == 2);

    // With min_path_score=15: only C1 qualifies
    // C2 bilateral: from_q≈22, to_q=0.7·(30−25)+0.3·4=4.7 → avg≈13.4 < 15
    auto filtered = analyzer.rank_all_channels(
        SelectionMode::LINK_ESTABLISHMENT, {}, 15.0f);
    assert(filtered.size() == 1);
    assert(filtered[0].frequency_hz == 7073000);

    std::cout << "  unfiltered=" << all.size() << " filtered=" << filtered.size() << std::endl;
    std::cout << "  PASS" << std::endl;
}

int main() {
    std::cout << "=== LQA Analyzer Tests ===" << std::endl;

    test_analyzer_creation();
    test_process_sounding();
    test_process_sounding_extended();
    test_best_channel_for_station();
    test_best_channel_overall();
    test_rank_all_channels();
    test_rank_channels_for_station();
    test_sounding_due();
    test_channels_needing_sounding();
    test_quality_summary();
    test_station_quality_summary();
    test_min_acceptable_score();
    test_sounding_callback();
    test_configuration();
    test_bilateral_ranking_fallback_no_bilateral_data();
    test_bilateral_average_formula();
    test_bilateral_balance_tiebreaker();
    test_handshake_fail_penalty();
    test_rank_all_channels_mode();
    test_rank_all_channels_min_path_score();

    std::cout << "\n=== All LQA Analyzer Tests Passed ===" << std::endl;
    return 0;
}
