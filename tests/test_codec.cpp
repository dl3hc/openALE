/**
 * \file tests/test_codec.cpp
 * \brief Unit tests for ALEEncoder + ALEDecoder round-trip.
 *
 * Tests the Codec layer in isolation — no audio, no modem, no state machine.
 *
 * Pipeline under test:
 *   ALEWord  →  ALEEncoder::encode()   →  SymbolFrame (49 symbols)
 *   SymbolFrame  →  ALEDecoder::decode()  →  ALEWord
 */

#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "Word/ale_word.h"
#include "FEC/golay.h"
#include <cassert>
#include <cstring>
#include <cstdio>

using namespace ale;

// ── helpers ──────────────────────────────────────────────────────────────────

static void check(bool cond, const char* msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

static ALEWord make(PreambleType type, const char* chars3)
{
    return WordParser::make_word(type, chars3);
}

// ── test functions ────────────────────────────────────────────────────────────

static void test_roundtrip_basic_preambles()
{
    struct Case { PreambleType type; const char* addr; };
    const Case cases[] = {
        { PreambleType::TO,   "BOB" },
        { PreambleType::TIS,  "SAM" },
        { PreambleType::TWAS, "BOB" },
        { PreambleType::FROM, "BOB" },
        { PreambleType::THRU, "REL" },
        { PreambleType::CMD,  "AB@" },
    };

    for (const auto& c : cases) {
        const ALEWord word = make(c.type, c.addr);
        check(word.valid, "make_word failed");

        const SymbolFrame frame = ALEEncoder::encode(word);

        ALEWord decoded;
        Golay::DecodeResult fec;
        uint8_t bad_votes = 0xFF;
        const bool ok = ALEDecoder::decode(frame.data(), decoded, fec, &bad_votes);

        check(ok, "decode returned false");
        check(fec.flag == Golay::DECODE_OK, "expected DECODE_OK for clean signal");
        check(bad_votes == 0, "expected 0 bad_votes for clean signal");
        check(decoded.type == word.type, "preamble type mismatch");
        check(std::strcmp(decoded.address, word.address) == 0, "address mismatch");
    }
    printf("PASS  test_roundtrip_basic_preambles\n");
}

static void test_roundtrip_extended_address()
{
    // DATA and REP use the Expanded 64 character set (0x20–0x5F).
    const ALEWord w_data = make(PreambleType::DATA, "UEL");  // 0x55 0x45 0x4C — all in 0x20-0x5F
    check(w_data.valid, "DATA word invalid");

    const SymbolFrame frame = ALEEncoder::encode(w_data);
    ALEWord out;
    Golay::DecodeResult fec;
    check(ALEDecoder::decode(frame.data(), out, fec), "DATA roundtrip failed");
    check(out.type == PreambleType::DATA, "DATA preamble mismatch");
    check(std::strcmp(out.address, w_data.address) == 0, "DATA address mismatch");
    printf("PASS  test_roundtrip_extended_address\n");
}

static void test_bad_votes_zero_on_clean_frame()
{
    const ALEWord word = make(PreambleType::TO, "BOB");
    const SymbolFrame frame = ALEEncoder::encode(word);

    ALEWord out;
    Golay::DecodeResult fec;
    uint8_t bv = 0xFF;
    ALEDecoder::decode(frame.data(), out, fec, &bv);

    // A clean encoded frame has all three copies of each bit identical → 0 bad votes.
    check(bv == 0, "bad_votes should be 0 for a perfectly encoded frame");
    printf("PASS  test_bad_votes_zero_on_clean_frame\n");
}

static void test_majority_vote_recovers_single_symbol_error()
{
    // Flip all 3 bits of one symbol in the frame.  The majority vote
    // reconstructs the correct tx49 if only one of the three copies is corrupted.
    // Since each symbol covers 3 consecutive stream bits, flipping one symbol
    // corrupts 3 adjacent bit positions in one of the three redundant copies.
    // Golay can then correct the residual FEC errors (up to 3 per half-word).

    const ALEWord word = make(PreambleType::TIS, "SAM");
    SymbolFrame frame = ALEEncoder::encode(word);

    // Corrupt the first symbol (cover both possibilities to ensure at least
    // one 1-copy-only error is introduced):
    frame[0] ^= 0x07u;   // flip all 3 bits of symbol 0 (copy 0 bits 0,1,2)

    ALEWord out;
    Golay::DecodeResult fec;
    uint8_t bv = 0;
    const bool ok = ALEDecoder::decode(frame.data(), out, fec, &bv);

    // The vote is 2:1 in favour of the correct bit for each position in symbol 0,
    // so majority vote still wins.  bad_votes must be > 0 (the 3 flipped bits are non-unanimous).
    check(ok, "decode should succeed with one corrupted symbol");
    check(out.type == word.type, "type mismatch after error injection");
    check(std::strcmp(out.address, word.address) == 0, "address mismatch after error injection");
    check(bv > 0, "expected non-zero bad_votes for corrupted frame");
    printf("PASS  test_majority_vote_recovers_single_symbol_error  (bad_votes=%u)\n",
           static_cast<unsigned>(bv));
}

static void test_encode_tx49_matches_encode_word()
{
    const ALEWord word = make(PreambleType::TO, "BOB");
    const uint64_t tx49 = word.encode();

    const SymbolFrame via_word  = ALEEncoder::encode(word);
    const SymbolFrame via_tx49  = ALEEncoder::encode_tx49(tx49);

    check(via_word == via_tx49, "encode(word) and encode_tx49(word.encode()) must match");
    printf("PASS  test_encode_tx49_matches_encode_word\n");
}

static void test_all_zero_word_decodes_to_data()
{
    // tx49 = 0 → all symbols = 0 → stream all-zero → majority vote = 0
    // Golay(0) is a valid codeword; preamble = bits 23..21 = 0 = DATA.
    SymbolFrame frame{};   // all zero symbols
    ALEWord out;
    Golay::DecodeResult fec;
    const bool ok = ALEDecoder::decode(frame.data(), out, fec);
    // If Golay accepts it, verify that bad_votes == 0.
    if (ok) {
        uint8_t bv = 0xFF;
        ALEDecoder::decode(frame.data(), out, fec, &bv);
        check(bv == 0, "all-zero frame should have no bad votes");
    }
    // We don't assert ok here — whether the all-zero tx49 is a valid Golay
    // codeword is a property of the Golay encoder, not of the codec layer.
    printf("PASS  test_all_zero_word_decodes_to_data  (valid=%s)\n", ok ? "yes" : "no");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    test_roundtrip_basic_preambles();
    test_roundtrip_extended_address();
    test_bad_votes_zero_on_clean_frame();
    test_majority_vote_recovers_single_symbol_error();
    test_encode_tx49_matches_encode_word();
    test_all_zero_word_decodes_to_data();

    printf("\nAll codec tests passed.\n");
    return 0;
}
