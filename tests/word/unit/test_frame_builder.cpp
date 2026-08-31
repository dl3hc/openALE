/**
 * \file test_frame_builder.cpp
 * \brief OFS Phase 1: FrameBuilder catalog + grammar validator (FR-09).
 *
 * docs/FRAMING_STANDARD.md §7: every TX frame is built through a catalog
 * constructor, grammar-validated at build time (FrameValidator::validate_
 * frame — the spec's "INVALID ADDRESS SEQUENCE … ALERT OPERATOR OR
 * CONTROLLER" flowchart exit, A.5.2.5.1/3, collapsed to build time) and
 * tagged with its catalog FrameType.
 *
 * Three groups:
 *   A. Each illegal shape from the spec flowcharts is REJECTED.
 *   B. Accept-golden: every word list the current TX paths emit PASSES —
 *      the guard that the validator cannot change anything on air
 *      (behavior-preservation pin for the whole Phase 1 migration).
 *   C. Catalog constructors: tags, shapes, and build-time refusal.
 *
 * The existing Frame/ALECalling/AMD/DTM/DBM test suites pin the exact TX
 * byte streams and run unmodified in the same ctest pass — those are the
 * golden output pins for the migrated call sites (orderwire, sounding,
 * AllCall, LINKED-AMD).
 */

#include "Word/ale_sequence.h"
#include "Word/address_encoder.h"
#include "Protocol/Message/frame_validator.h"
#include "Protocol/Message/ale_frame_builder.h"
#include "Protocol/Message/ale_orderwire_protocols.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ale;

namespace {

bool all_pass = true;

void check(bool cond, const char* label) {
    std::cout << "  " << label << ": " << (cond ? "PASS" : "FAIL") << "\n";
    all_pass = all_pass && cond;
}

// 3-char word helper (payload chars only — validity/payload bits are the
// word layer's business; the validator reads type + address field).
ALEWord w3(PreambleType t, char a, char b, char c) {
    const char chars[3] = {a, b, c};
    return WordParser::make_word(t, chars);
}

bool rejects(const char* label, FrameType type, std::vector<ALEWord> words) {
    const auto err = FrameValidator::validate_frame(type, words);
    const bool rej = err.has_value();
    std::cout << "  [" << (rej ? "rejected" : "ACCEPTED?!") << "] " << label;
    if (rej) std::cout << " — " << *err;
    std::cout << "\n";
    all_pass = all_pass && rej;
    return rej;
}

bool accepts(const char* label, FrameType type, const std::vector<ALEWord>& words) {
    const auto err = FrameValidator::validate_frame(type, words);
    const bool ok = !err.has_value();
    std::cout << "  [" << (ok ? "accepted" : "REJECTED?!") << "] " << label;
    if (!ok) std::cout << " — " << *err;
    std::cout << "\n";
    all_pass = all_pass && ok;
    return ok;
}

// ── Group A: illegal shapes rejected ─────────────────────────────────────────
void group_a_rejections() {
    std::cout << "\n[A] Illegal shapes must be rejected (spec flowcharts)\n";
    std::cout << "====================================================\n";

    // >15-char address: anchor + 5 extensions = 6 words.
    rejects("address run exceeds 5 words / 15 chars", FrameType::F_CALL,
        {w3(PreambleType::TO, 'A','B','C'),
         w3(PreambleType::DATA, 'D','E','F'),
         w3(PreambleType::REP,  'G','H','I'),
         w3(PreambleType::DATA, 'J','K','L'),
         w3(PreambleType::REP,  'M','N','O'),
         w3(PreambleType::DATA, 'P','Q','R')});

    // '@' stuffing not in the last extension word (A.5.2.4.3). A stuffed
    // extension followed by ANY further extension of the same run is illegal
    // — but a new anchor legitimately closes the run (doubled leading call).
    rejects("stuffing followed by a further extension", FrameType::F_CALL,
        {w3(PreambleType::TO,   'D','C','7'),
         w3(PreambleType::DATA, 'S','U','@'),
         w3(PreambleType::REP,  'X','Y','B')});
    rejects("two stuffed extension words", FrameType::F_CALL,
        {w3(PreambleType::TO,   'D','C','7'),
         w3(PreambleType::DATA, 'S','U','@'),
         w3(PreambleType::REP,  'X','Y','@')});

    // TIS and TWAS in one frame (A.5.2.5.3).
    rejects("TIS and TWAS in one frame", FrameType::F_ACK,
        {w3(PreambleType::TO,   'J','O','E'),
         w3(PreambleType::TO,   'J','O','E'),
         w3(PreambleType::TIS,  'S','A','M'),
         w3(PreambleType::TWAS, 'S','A','M')});

    // REP directly after TIS/TWAS — first extension must be DATA (A.5.2.5.3).
    rejects("REP directly after conclusion", FrameType::F_TERMINATION,
        {w3(PreambleType::TO,   'J','O','E'),
         w3(PreambleType::TO,   'J','O','E'),
         w3(PreambleType::TWAS, 'S','A','M'),
         w3(PreambleType::REP,  'U','E','L')});

    // Non-alternating extension (A.5.2.4.4.2): DATA directly after DATA.
    // (REP after REP is deliberately NOT rejected — legal group-lead grammar,
    // A.5.5.4.3.2; conclusions reject REP at the FIRST extension.)
    rejects("DATA directly after DATA extension", FrameType::F_CALL,
        {w3(PreambleType::TO,   'D','C','7'),
         w3(PreambleType::DATA, 'S','U','X'),
         w3(PreambleType::DATA, 'Y','Z','B')});

    // AMD block over Tm max (A.5.7.2.3): 90 chars of message text = the CMD's
    // first 3 chars + 29 DATA/REP words ("59 words counting CMD" = 29+30).
    {
        std::vector<ALEWord> over;
        over.push_back(w3(PreambleType::CMD, 'H','E','Y'));
        for (int i = 0; i < 30; ++i)
            over.push_back(w3((i % 2 == 0) ? PreambleType::DATA : PreambleType::REP,
                              'A','B','C'));
        rejects("AMD block over Tm max (30 data words)", FrameType::F_ORDERWIRE, over);

        std::vector<ALEWord> at_max = over;
        at_max.pop_back();   // exactly 29 data words — the 90-char limit
        accepts("AMD block at Tm max (29 data words / 90 chars)", FrameType::F_ORDERWIRE, at_max);
    }

    // Structural bottom cases.
    rejects("empty frame", FrameType::F_CALL, {});
    rejects("DATA/REP extension before any anchor", FrameType::F_CALL,
        {w3(PreambleType::DATA, 'S','U','X'),
         w3(PreambleType::TO,   'J','O','E')});
}

// ── Group B: accept-golden over the current TX repertoire ────────────────────
void group_b_accept_golden() {
    std::cout << "\n[B] Accept-golden: every current TX shape passes the validator\n";
    std::cout << "==============================================================\n";

    // Sections (validated in their frame context, not standalone).
    accepts("scanning_call C=1", FrameType::F_CALL,
            ALESequenceBuilder::scanning_call("BOB", 1).words());
    accepts("scanning_call C=3, multi-word dest", FrameType::F_CALL,
            ALESequenceBuilder::scanning_call("SL3ZXB", 3).words());
    accepts("scanning_call_group", FrameType::F_CALL,
            ALESequenceBuilder::scanning_call_group({"BOB", "EDGAR", "SAM"}, 3).words());
    accepts("leading_call 1-word", FrameType::F_CALL,
            ALESequenceBuilder::leading_call("JOE").words());
    accepts("leading_call 2-word", FrameType::F_CALL,
            ALESequenceBuilder::leading_call("SL3ZXB").words());
    accepts("leading_call 15-char (5 words, Ta max)", FrameType::F_CALL,
            ALESequenceBuilder::leading_call("ABCDEFGHIJKLMNO").words());
    accepts("leading_call_group", FrameType::F_CALL,
            ALESequenceBuilder::leading_call_group({"BOB", "EDGAR", "SAMUEL"}).words());
    accepts("conclusion TIS multi-word", FrameType::F_SOUND,
            ALESequenceBuilder::conclusion("DL3HC").words());
    accepts("conclusion TWAS multi-word", FrameType::F_SOUND,
            ALESequenceBuilder::conclusion("DL3HC", true).words());
    accepts("from_id multi-word", FrameType::F_CALL,
            ALESequenceBuilder::from_id("DL3HC").words());

    // Complete frames via the catalog constructors (validation runs inside).
    {
        const auto r = ALESequenceBuilder::response("SAMUEL", "JOE");
        check(!r.empty(), "response(accept, multi-word caller) builds");
        accepts("response frame words", FrameType::F_RESPONSE, r.words());
    }
    {
        const auto r = ALESequenceBuilder::response("SAM", "DL3HC", true);
        check(!r.empty() && r.words().size() == 2, "response(reject) = TWAS[self] + ext");
        accepts("response reject frame words", FrameType::F_RESPONSE, r.words());
    }
    {
        const auto a = ALESequenceBuilder::ack("DC7SU", "DL3HC");
        check(!a.empty(), "ack(link) builds");
        accepts("ack frame words", FrameType::F_ACK, a.words());
        const auto d = ALESequenceBuilder::ack("DC7SU", "DL3HC", true);
        check(!d.empty(), "ack(no_link) builds");
        accepts("ack no-link frame words", FrameType::F_ACK, d.words());
    }
    {
        const auto t = ALESequenceBuilder::termination("DC7SU", "DL3HC");
        check(!t.empty(), "termination(multi-word peer) builds");
        accepts("termination frame words", FrameType::F_TERMINATION, t.words());
        const auto deg = ALESequenceBuilder::termination("", "SAM");
        check(!deg.empty() && deg.words().size() == 1, "termination(empty peer) = TWAS[self]");
        accepts("termination degenerate words", FrameType::F_TERMINATION, deg.words());
    }

    // Message-section sequences in their carrying-frame context.
    {
        // Calling frame with the full message section: leading + FROM + CMD 'a'
        // + LQA report + AMD + TIS conclusion (enqueue_call_sequence_ order).
        std::vector<ALEWord> f = ALESequenceBuilder::leading_call("SL3ZXB").words();
        const auto from = ALESequenceBuilder::from_id("DL3HC").words();
        f.insert(f.end(), from.begin(), from.end());
        const auto lqa = ALESequenceBuilder::lqa_cmd(0x12345u).words();
        f.insert(f.end(), lqa.begin(), lqa.end());
        LQAReport r1{}, r2{};
        const auto rpt = ALESequenceBuilder::lqa_report({r1, r2}).words();
        f.insert(f.end(), rpt.begin(), rpt.end());
        const auto amd = encode_amd("QSL MSG 73");
        f.insert(f.end(), amd.begin(), amd.end());
        const auto conc = ALESequenceBuilder::conclusion("DL3HC").words();
        f.insert(f.end(), conc.begin(), conc.end());
        accepts("F_CALL with full message section (FROM + LQA + AMD)", FrameType::F_CALL, f);
    }
    {
        // 90-char AMD = the Tm limit (30 data words).
        std::vector<ALEWord> f = ALESequenceBuilder::leading_call("JOE").words();
        const auto amd = encode_amd(std::string(90, 'X'));
        check(amd.size() == 30, "encode_amd(90 chars) = CMD + 29 data words");
        f.insert(f.end(), amd.begin(), amd.end());
        const auto conc = ALESequenceBuilder::conclusion("DL3HC").words();
        f.insert(f.end(), conc.begin(), conc.end());
        accepts("F_CALL with 90-char AMD at the Tm limit", FrameType::F_CALL, f);
    }
    {
        // DTM block: 90 chars + CRC = 31 DATA/REP words — exempt from the AMD
        // cap (own size rules per FR-11 / §6.1 P-2).
        std::vector<ALEWord> f = ALESequenceBuilder::leading_call("JOE").words();
        const auto dtm = encode_dtm(std::string(90, 'X'), true);
        f.insert(f.end(), dtm.begin(), dtm.end());
        const auto conc = ALESequenceBuilder::conclusion("DL3HC").words();
        f.insert(f.end(), conc.begin(), conc.end());
        accepts("F_CALL with max-size DTM block (31 words incl. CRC)", FrameType::F_CALL, f);
    }
    {
        // DBM block likewise.
        std::vector<ALEWord> f = ALESequenceBuilder::leading_call("JOE").words();
        std::vector<uint8_t> payload(90, 0x41u);
        const auto dbm = encode_dbm(payload, true);
        f.insert(f.end(), dbm.begin(), dbm.end());
        const auto conc = ALESequenceBuilder::conclusion("DL3HC").words();
        f.insert(f.end(), conc.begin(), conc.end());
        accepts("F_CALL with DBM block", FrameType::F_CALL, f);
    }

    // Sound burst (C+2 conclusions of a multi-word self address).
    {
        const auto s = ALEFrameBuilder::sound("DL3HC", false, 12);
        check(!s.empty() && s.words().size() == 12u * 2u,
              "sound(DL3HC, reps=12) = 12 × 2-word conclusion");
        accepts("sound burst words", FrameType::F_SOUND, s.words());
        // CMD NOISE raw24 (noise_cmd() encoding: 'n'<<14 | max<<7 | mean).
        const uint32_t noise_raw = (0x6Eu << 14) | (80u << 7) | 60u;
        const auto sn = ALEFrameBuilder::sound("DL3HC", true, 2, noise_raw);
        check(!sn.empty() && sn.words().size() == 2u * 2u + 1u,
              "sound(TWAS, reps=2, +noise) appends the CMD 'n' fragment");
    }

    // AllCall broadcast frame.
    {
        const auto amd = encode_amd("POSITION REPORT");
        const auto f = ALEFrameBuilder::allcall_broadcast("DL3HC", amd, false, 3);
        check(!f.empty(), "allcall_broadcast(TWAS conclusion) builds");
        accepts("AllCall broadcast frame words", FrameType::F_ALLCALL, f.words());
    }

    // Orderwire bursts: single (AMD) and doubled (EFS).
    {
        auto content = ALESequenceBuilder::leading_call("SL3ZXB").words();
        const auto amd = encode_amd("HELLO LINKED WORLD");
        content.insert(content.end(), amd.begin(), amd.end());
        const auto single = ALEFrameBuilder::orderwire_burst(content, "DL3HC", false);
        check(!single.empty() && single.frame_type() == FrameType::F_ORDERWIRE,
              "orderwire_burst(single) builds + tagged");
        accepts("orderwire single-burst words", FrameType::F_ORDERWIRE, single.words());

        const auto dbl = ALEFrameBuilder::orderwire_burst(content, "DL3HC", true);
        check(!dbl.empty() && dbl.words().size() == 2u * single.words().size(),
              "orderwire_burst(double) = 2× the single burst");
        accepts("orderwire doubled-burst words", FrameType::F_ORDERWIRE, dbl.words());
    }

    // Complete group-call frame (AC-ADDR-001-003 "all valid members" path):
    // group scanning + group leading + conclusion, across the C range the
    // controller may set (0 = skip scanning section).
    for (uint32_t C = 0; C <= 10; ++C) {
        std::vector<ALEWord> f = ALESequenceBuilder::scanning_call_group(
            {"BOB", "JOE", "ANN"}, C).words();
        const auto lead = ALESequenceBuilder::leading_call_group({"BOB", "JOE", "ANN"}).words();
        f.insert(f.end(), lead.begin(), lead.end());
        const auto conc = ALESequenceBuilder::conclusion("SAM").words();
        f.insert(f.end(), conc.begin(), conc.end());
        char label[64];
        std::snprintf(label, sizeof(label), "F_CALL group frame (BOB/JOE/ANN), C=%u", C);
        accepts(label, FrameType::F_CALL, f);
    }
}

// ── Group C: catalog tags and build-time refusal ─────────────────────────────
void group_c_catalog() {
    std::cout << "\n[C] Catalog tags and constructor refusal\n";
    std::cout << "========================================\n";

    check(ALESequenceBuilder::response("SAM", "JOE").frame_type() == FrameType::F_RESPONSE,
          "response() tagged F_RESPONSE");
    check(ALESequenceBuilder::ack("SAM", "JOE").frame_type() == FrameType::F_ACK,
          "ack() tagged F_ACK");
    check(ALESequenceBuilder::termination("SAM", "JOE").frame_type() == FrameType::F_TERMINATION,
          "termination() tagged F_TERMINATION");
    check(ALEFrameBuilder::sound("SAM", false, 2).frame_type() == FrameType::F_SOUND,
          "sound() tagged F_SOUND");
    {
        const auto amd = encode_amd("HI");
        check(ALEFrameBuilder::allcall_broadcast("SAM", amd, false, 1).frame_type()
              == FrameType::F_ALLCALL, "allcall_broadcast() tagged F_ALLCALL");
    }
    check(ALEFrameBuilder::orderwire_burst({}, "SAM", false).frame_type()
          == FrameType::F_ORDERWIRE, "orderwire_burst() tagged F_ORDERWIRE");

    // Sections and fragments stay UNTAGGED (they are not frames).
    check(ALESequenceBuilder::leading_call("JOE").frame_type() == FrameType::UNTAGGED,
          "leading_call() section stays UNTAGGED");
    check(ALESequenceBuilder::conclusion("SAM").frame_type() == FrameType::UNTAGGED,
          "conclusion() section stays UNTAGGED");
    check(ALESequenceBuilder::lqa_cmd(0u).frame_type() == FrameType::UNTAGGED,
          "lqa_cmd() fragment stays UNTAGGED");

    // Build-time refusal: an illegal word list yields an EMPTY sequence —
    // nothing goes on air (FR-09 hard failure). The only reachable refusal is
    // caller-supplied content (orderwire words): builder-internal sections
    // cannot be illegal by construction — AddressEncoder caps every address
    // at 5 words / 15 chars, so e.g. sound()/response() with any self/peer
    // address always pass the gate.
    {
        // Content carrying both TIS and TWAS cannot be a legal frame.
        std::vector<ALEWord> bad = ALESequenceBuilder::leading_call("JOE").words();
        bad.push_back(w3(PreambleType::TIS,  'S','A','M'));
        bad.push_back(w3(PreambleType::TWAS, 'S','A','M'));
        check(ALEFrameBuilder::orderwire_burst(std::move(bad), "DL3HC", false).empty(),
              "orderwire_burst() refuses an illegal frame (empty return)");
    }
}

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "FrameBuilder catalog + grammar validator (OFS FR-09)\n";
    std::cout << "==========================================================\n";

    group_a_rejections();
    group_b_accept_golden();
    group_c_catalog();

    if (all_pass) {
        std::cout << "\nAll tests PASSED.\n";
        return 0;
    }
    std::cout << "\nTESTS FAILED.\n";
    return 1;
}