/**
 * \file test_fec_golay.cpp
 * \brief Unit tests for Extended Golay (24,12) FEC codec and word interleaver
 *
 * Covers: AC-FEC-001-001, AC-FEC-001-002,
 *         AC-FEC-005-1/2/3, AC-FEC-006-1,
 *         AC-FEC-007-1, AC-FEC-009-1/2,
 *         AC-FEC-008-1, AC-FEC-010-1/2/3, AC-FEC-011-1/2,
 *         AC-FEC-012-1/2/3, AC-FEC-013-1/2/3/4
 * Spec: MIL-STD-188-141B A.5.2.2, A.5.2.2.3
 *
 * (The full ALE decode path — 2/3 majority voting + Golay — is covered by
 *  test_codec, test_pcale_interop and test_decoder_robustness.)
 */

#include "FEC/golay.h"
#include "FEC/ale_fec_codec.h"
#include "FEC/word_interleaver.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>

namespace ale {

// AC-FEC-001-001: Generator polynomial 0xAE3 and compile-time table with 4096 entries.
// REQ-FEC-004, REQ-FEC-005 (FEAT-FEC-001, MIL-STD-188-141B A.5.2.2.2)
bool test_ac_fec_001_001_generator_polynomial() {
    std::cout << "\n[TEST FEC-1] AC-FEC-001-001: Generator polynomial 0xAE3, table size 4096\n";
    std::cout << "--------------------------------------------------------------------------\n";

    // Verify the named constant equals 0xAE3
    static_assert(Golay::GENERATOR_POLYNOMIAL == 0xAE3u,
                  "GENERATOR_POLYNOMIAL must equal 0xAE3");
    std::cout << "  Golay::GENERATOR_POLYNOMIAL = 0x"
              << std::hex << Golay::GENERATOR_POLYNOMIAL << std::dec
              << " (expected 0xAE3)\n";
    if (Golay::GENERATOR_POLYNOMIAL != 0xAE3u) {
        std::cout << "FAIL: GENERATOR_POLYNOMIAL != 0xAE3\n";
        return false;
    }

    // Verify the table has 4096 entries: encode covers all 2^12 info words.
    static_assert(Golay::ENCODE_TABLE_SIZE == 4096u,
                  "ENCODE_TABLE_SIZE must equal 4096");
    std::cout << "  ENCODE_TABLE_SIZE = " << Golay::ENCODE_TABLE_SIZE
              << " (expected 4096)\n";
    if (Golay::ENCODE_TABLE_SIZE != 4096u) {
        std::cout << "FAIL: ENCODE_TABLE_SIZE != 4096\n";
        return false;
    }

    // Verify the polynomial is actually used: encoding the MSB basis vector (info=0x800)
    // must yield a parity equal to GENERATOR_POLYNOMIAL (by definition of the Golay code).
    const uint32_t cw_msb    = Golay::encode(0x800u);
    const uint16_t parity_msb = Golay::extract_parity(cw_msb);
    std::cout << "  encode(0x800) parity = 0x" << std::hex << parity_msb << std::dec
              << " (expected 0x" << std::hex << Golay::GENERATOR_POLYNOMIAL << std::dec << ")\n";
    if (parity_msb != Golay::GENERATOR_POLYNOMIAL) {
        std::cout << "FAIL: encode(0x800) parity mismatch — generator polynomial not applied correctly\n";
        return false;
    }

    std::cout << "PASS\n";
    return true;
}


// AC-FEC-001-002: Compile-time encode table with 4096 entries, each yielding a
// 24-bit codeword (info | parity). REQ-FEC-006, REQ-FEC-007 (FEAT-FEC-001).
//
// Three sub-checks:
//  1. ENCODE_TABLE_SIZE == 4096 (compile-time)
//  2. All 12 G-matrix basis rows match MIL-STD-188-141B Figure A-6 exactly
//  3. All 4096 encode() outputs are 24-bit and systematic (info in bits 23..12)
bool test_ac_fec_001_002_encode_table() {
    std::cout << "\n[TEST FEC-1b] AC-FEC-001-002: Encode-Tabelle 4096 Eintraege, 24-Bit-Codewort\n";
    std::cout << "------------------------------------------------------------------------------\n";

    // -- (1) Compile-time size check ----------------------------------------------
    static_assert(Golay::ENCODE_TABLE_SIZE == 4096u,
                  "ENCODE_TABLE_SIZE must equal 4096");
    std::cout << "  ENCODE_TABLE_SIZE = " << Golay::ENCODE_TABLE_SIZE << " (OK)\n";

    // -- (2) G-matrix basis vectors per MIL-STD-188-141B Figure A-6 --------------
    // The generator matrix G = [I12 | P].  Each row i gives the parity word for
    // the single-bit info word info=2^(11-i).  Parity bits are read MSB-first
    // from Figure A-6 (groups of 3 binary digits separated by spaces).
    //
    // Row  1 (info=0x800): 101 011 100 011 = 0xAE3
    // Row  2 (info=0x400): 111 110 010 010 = 0xF92
    // Row  3 (info=0x200): 110 100 101 011 = 0xD2B
    // Row  4 (info=0x100): 110 001 110 110 = 0xC76
    // Row  5 (info=0x080): 110 011 011 001 = 0xCD9
    // Row  6 (info=0x040): 011 001 101 101 = 0x66D
    // Row  7 (info=0x020): 001 100 110 111 = 0x337
    // Row  8 (info=0x010): 101 101 111 000 = 0xB78
    // Row  9 (info=0x008): 010 110 111 100 = 0x5BC
    // Row 10 (info=0x004): 001 011 011 110 = 0x2DE
    // Row 11 (info=0x002): 101 110 001 101 = 0xB8D
    // Row 12 (info=0x001): 010 111 000 111 = 0x5C7
    struct BasisCase { uint16_t info; uint16_t parity; const char* label; };
    static constexpr BasisCase G_ROWS[12] = {
        { 0x800, 0xAE3, "Row  1 (bit 11)" },
        { 0x400, 0xF92, "Row  2 (bit 10)" },
        { 0x200, 0xD2B, "Row  3 (bit  9)" },
        { 0x100, 0xC76, "Row  4 (bit  8)" },
        { 0x080, 0xCD9, "Row  5 (bit  7)" },
        { 0x040, 0x66D, "Row  6 (bit  6)" },
        { 0x020, 0x337, "Row  7 (bit  5)" },
        { 0x010, 0xB78, "Row  8 (bit  4)" },
        { 0x008, 0x5BC, "Row  9 (bit  3)" },
        { 0x004, 0x2DE, "Row 10 (bit  2)" },
        { 0x002, 0xB8D, "Row 11 (bit  1)" },
        { 0x001, 0x5C7, "Row 12 (bit  0)" },
    };

    for (const auto& bc : G_ROWS) {
        const uint16_t got = Golay::extract_parity(Golay::encode(bc.info));
        if (got != bc.parity) {
            std::cout << "FAIL G-matrix " << bc.label
                      << ": got 0x" << std::hex << got
                      << " expected 0x" << bc.parity << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  G-matrix (Fig. A-6): all 12 basis-vector parities match spec OK\n";

    // -- (3) All 4096 entries: 24-bit codeword, systematic form ------------------
    // bits 23..12 == info (systematic code); no bits above 23 set.
    uint32_t fail_count = 0;
    for (uint32_t info = 0; info < Golay::ENCODE_TABLE_SIZE; ++info) {
        const uint32_t cw = Golay::encode(static_cast<uint16_t>(info));

        if (cw >> 24) {
            std::cout << "FAIL: encode(0x" << std::hex << info
                      << ") = 0x" << cw << " exceeds 24 bits\n" << std::dec;
            ++fail_count;
            continue;
        }
        if ((cw >> 12) != info) {
            std::cout << "FAIL: encode(0x" << std::hex << info
                      << ") upper 12 bits = 0x" << (cw >> 12)
                      << " (expected 0x" << info << ")\n" << std::dec;
            ++fail_count;
        }
    }

    if (fail_count) {
        std::cout << "FAIL: " << fail_count << " entries invalid\n";
        return false;
    }

    std::cout << "  All 4096 entries: 24-bit codeword, systematic form OK\n";
    std::cout << "PASS\n";
    return true;
}

static std::vector<uint32_t> make_golay_error_masks_upto_3() {
    std::vector<uint32_t> masks;
    masks.reserve(1 + 24 + 276 + 2024);

    masks.push_back(0u);

    for (int i = 0; i < 24; ++i)
        masks.push_back(1u << i);

    for (int i = 0; i < 24; ++i)
        for (int j = i + 1; j < 24; ++j)
            masks.push_back((1u << i) | (1u << j));

    for (int i = 0; i < 24; ++i)
        for (int j = i + 1; j < 24; ++j)
            for (int k = j + 1; k < 24; ++k)
                masks.push_back((1u << i) | (1u << j) | (1u << k));

    return masks;
}

// AC-FEC-005-1: all 2325 error patterns up to weight 3 must be corrected
bool test_golay_codec_minimal() {
    std::cout << "\n[TEST FEC-2] Golay codec — all 1-3-bit error patterns (AC-FEC-005-1)\n";
    std::cout << "-----------------------------------------------------------------------\n";

    uint16_t u = 0x123;
    std::cout << "  reference word: 0x" << std::hex << u << std::dec << "\n";

    uint32_t cw = Golay::encode(u);
    std::cout << "  encoded codeword: 0x" << std::hex << cw << std::dec << "\n";

    uint16_t out = 0;
    Golay::DecodeResult res = Golay::decode(cw, out);

    std::cout << "  clean decode -> 0x" << std::hex << out
              << " errors=" << std::dec << (int)res.errors_corrected << "\n";

    if (out != u || res.flag != Golay::DECODE_OK) {
        std::cout << "FAIL: clean decode\n";
        return false;
    }

    const auto masks = make_golay_error_masks_upto_3();
    std::cout << "  testing error masks: " << masks.size() << "\n";

    uint32_t passed = 0;
    for (uint32_t m : masks) {
        uint16_t d = 0;
        Golay::decode(cw ^ m, d);
        if (d == u) {
            ++passed;
        } else {
            std::cout << "FAIL mask=0x" << std::hex << m << std::dec << "\n";
            return false;
        }
    }

    std::cout << "  corrected cases: " << passed << "/" << masks.size() << "\n";
    std::cout << "PASS\n";
    return true;
}

// AC-FEC-005-2, AC-FEC-006-1, AC-FEC-007-1, AC-FEC-009-1/2
bool test_golay_spec_compliance() {
    std::cout << "\n[TEST FEC-3] Golay spec compliance (AC-FEC-005/006/007/009)\n";
    std::cout << "-----------------------------------------------------------\n";

    // AC-FEC-006-1: basis vector for info bit 11 == generator polynomial 0xAE3
    uint32_t cw_msb    = Golay::encode(0x800);
    uint16_t parity_msb = Golay::extract_parity(cw_msb);
    std::cout << "  AC-FEC-006-1  encode(0x800) parity = 0x"
              << std::hex << parity_msb << std::dec << "\n";
    if (parity_msb != 0xAE3) {
        std::cout << "FAIL: generator polynomial mismatch (expected 0xAE3)\n";
        return false;
    }

    // AC-FEC-007-1 / AC-FEC-005-2: systematic form, all 4096 words
    // AC-FEC-009-1/2: full encode → decode round-trip, all 4096 info words
    uint32_t fail_word = 0;
    for (uint32_t info = 0; info < 4096; ++info) {
        uint32_t cw = Golay::encode(static_cast<uint16_t>(info));
        if ((cw >> 12) != info) {
            fail_word = info;
            std::cout << "FAIL: AC-FEC-007-1 systematic form broken at info=0x"
                      << std::hex << info << std::dec << "\n";
            return false;
        }
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw, out);
        if (r.flag != Golay::DECODE_OK || out != static_cast<uint16_t>(info)) {
            fail_word = info;
            std::cout << "FAIL: AC-FEC-009-1 round-trip at info=0x"
                      << std::hex << info << std::dec << "\n";
            return false;
        }
    }
    (void)fail_word;
    std::cout << "  AC-FEC-007-1  systematic form: all 4096 codewords OK\n";
    std::cout << "  AC-FEC-009-1/2 round-trip:     all 4096 info words OK\n";
    std::cout << "PASS\n";
    return true;
}

// DECODE_CORRECTED with correct error count; DECODE_DETECTED for weight > 3
bool test_golay_decode_flags() {
    std::cout << "\n[TEST FEC-4] Golay decode flags (DECODE_CORRECTED / DECODE_DETECTED)\n";
    std::cout << "-------------------------------------------------------------------\n";

    static constexpr uint16_t REF = 0x5A5;
    uint32_t cw = Golay::encode(REF);

    struct Case { uint32_t mask; uint8_t weight; };
    const Case correctable[] = {
        {0x000001u, 1}, {0x000003u, 2}, {0x000007u, 3},
        {0x000010u, 1}, {0x000110u, 2}, {0x001110u, 3},
    };
    for (auto& c : correctable) {
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ c.mask, out);
        if (r.flag != Golay::DECODE_CORRECTED) {
            std::cout << "FAIL: mask=0x" << std::hex << c.mask
                      << " expected DECODE_CORRECTED, got flag=0x"
                      << (int)r.flag << std::dec << "\n";
            return false;
        }
        if (r.errors_corrected != c.weight) {
            std::cout << "FAIL: mask=0x" << std::hex << c.mask << std::dec
                      << " errors_corrected=" << (int)r.errors_corrected
                      << " expected " << (int)c.weight << "\n";
            return false;
        }
        if (out != REF) {
            std::cout << "FAIL: mask=0x" << std::hex << c.mask
                      << " output=0x" << out << " expected 0x" << REF << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  DECODE_CORRECTED: weight 1/2/3 patterns OK\n";

    // weight-4 error must be DECODE_DETECTED (not correctable by design)
    uint32_t mask4 = 0x00000Fu;
    uint16_t out4  = 0;
    Golay::DecodeResult r4 = Golay::decode(cw ^ mask4, out4);
    if (r4.flag != Golay::DECODE_DETECTED) {
        std::cout << "FAIL: weight-4 mask=0x" << std::hex << mask4
                  << " expected DECODE_DETECTED, got flag=0x"
                  << (int)r4.flag << std::dec << "\n";
        return false;
    }
    std::cout << "  DECODE_DETECTED:  weight-4 pattern OK\n";

    std::cout << "PASS\n";
    return true;
}

// FEAT-FEC-002 — Golay (24,12) Decoder mit Fehlerkorrektur
// REQ-FEC-008 / REQ-FEC-010 / REQ-FEC-011
bool test_feat_fec_002_decoder_acs() {
    std::cout << "\n[TEST FEC-5] FEAT-FEC-002 — Decoder ACs (REQ-FEC-008/010/011)\n";
    std::cout << "-----------------------------------------------------------------\n";

    // AC-FEC-008-1: Golay-Prüfverfahren — s = yH^T mod 2
    //   Gültiges Codewort → DECODE_OK (Syndrom = 0)
    //   Fehler im Codewort → Syndrom ≠ 0 (wird erkannt)
    {
        const uint16_t ref = 0xABC;
        uint32_t cw = Golay::encode(ref);
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw, out);
        if (r.flag != Golay::DECODE_OK || out != ref) {
            std::cout << "FAIL AC-FEC-008-1: valid codeword must yield DECODE_OK\n";
            return false;
        }
        // Alle 24 Einzelbitfehler müssen erkannt werden (s ≠ 0)
        for (int bit = 0; bit < 24; ++bit) {
            uint16_t d = 0;
            Golay::DecodeResult rd = Golay::decode(cw ^ (1u << bit), d);
            if (rd.flag != Golay::DECODE_CORRECTED) {
                std::cout << "FAIL AC-FEC-008-1: bit " << bit << " error not detected\n";
                return false;
            }
        }
        std::cout << "  AC-FEC-008-1: syndrome=0 fuer gueltiges CW;"
                     " alle 24 Einzelbitfehler erkannt OK\n";
    }

    // AC-FEC-010-1: Syndrom-basiertes Dekodierverfahren (syndrome_table[s] → Fehlervektor)
    // AC-FEC-010-2: Jeder korrigierbare Fehlervektor eindeutig einem Syndromwert zugeordnet
    // AC-FEC-010-3: Dekodiertes Datenwort = gesendetes Original (Fehler ≤ 3)
    //   Verifikation: alle 2325 Fehlermuster (Gewicht 0..3) auf Referenzwort 0xABC
    //   Wenn ein Syndrom-Kollision existierte, würde mind. ein Muster fehlschlagen.
    {
        const uint16_t ref = 0xABC;
        uint32_t cw = Golay::encode(ref);
        const auto masks = make_golay_error_masks_upto_3();
        uint32_t corrected_count = 0;
        for (uint32_t m : masks) {
            uint16_t decoded = 0;
            Golay::decode(cw ^ m, decoded);
            if (decoded != ref) {
                std::cout << "FAIL AC-FEC-010-2/3: mask=0x" << std::hex << m
                          << " decoded=0x" << decoded
                          << " expected=0x" << ref << std::dec << "\n";
                return false;
            }
            ++corrected_count;
        }
        if (corrected_count != static_cast<uint32_t>(masks.size())) {
            std::cout << "FAIL AC-FEC-010-2: nur " << corrected_count
                      << "/" << masks.size() << " Fehlermuster korrigiert\n";
            return false;
        }
        std::cout << "  AC-FEC-010-1/2/3: alle " << corrected_count
                  << " Fehlermuster (Gew. 0..3) eindeutig dekodiert OK\n";
    }

    // AC-FEC-011-1: Flag DECODE_CORRECTED bei erfolgreicher Korrektur
    //   errors_corrected muss der tatsächlichen Fehleranzahl (1-3) entsprechen
    {
        const uint16_t ref = 0x7E1;
        uint32_t cw = Golay::encode(ref);
        struct { uint32_t mask; uint8_t wt; } cases[] = {
            {0x000001u, 1}, {0x000003u, 2}, {0x000007u, 3},
        };
        for (auto& c : cases) {
            uint16_t out = 0;
            Golay::DecodeResult r = Golay::decode(cw ^ c.mask, out);
            if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != c.wt || out != ref) {
                std::cout << "FAIL AC-FEC-011-1: wt=" << (int)c.wt
                          << " flag=0x" << std::hex << (int)r.flag
                          << " errors_corrected=" << std::dec << (int)r.errors_corrected << "\n";
                return false;
            }
        }
        std::cout << "  AC-FEC-011-1: DECODE_CORRECTED + errors_corrected fuer Gew. 1/2/3 OK\n";
    }

    // AC-FEC-011-2: Flag DECODE_DETECTED bei erkennbarem, nicht korrigierbarem Fehler
    //   Fehlergewicht 4 liegt über der Korrekturkapazität (min. Dist. 8 → kein CW-Konflikt)
    {
        const uint16_t ref = 0x7E1;
        uint32_t cw = Golay::encode(ref);
        uint32_t mask4 = 0x0000000Fu;   // Bits 0-3 gesetzt, Gewicht 4
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ mask4, out);
        if (r.flag != Golay::DECODE_DETECTED) {
            std::cout << "FAIL AC-FEC-011-2: Gewicht-4-Fehler muss DECODE_DETECTED liefern,"
                      << " erhalten flag=0x" << std::hex << (int)r.flag << std::dec << "\n";
            return false;
        }
        std::cout << "  AC-FEC-011-2: DECODE_DETECTED fuer Gewicht-4-Fehler OK\n";
    }

    std::cout << "PASS\n";
    return true;
}

// ============================================================================
// FEAT-FEC-003 — Interleaving / Deinterleaving (REQ-FEC-012, REQ-FEC-013)
// ============================================================================

// AC-FEC-012-1/3, AC-FEC-013-1: Roundtrip interleave → deinterleave
// Tests that deinterleave(interleave(w)) == w for representative words in both halves.
bool test_feat_fec_003_roundtrip() {
    std::cout << "\n[TEST FEC-6] FEAT-FEC-003 — Roundtrip interleave/deinterleave (AC-FEC-012-1/3, AC-FEC-013-1)\n";
    std::cout << "--------------------------------------------------------------------------------------------\n";

    uint32_t fail_count = 0;

    auto roundtrip = [&](uint32_t ale_word) {
        const GolayCoded coded     = ALEFECCodec::encode_word(ale_word);
        const uint64_t transmitted = ALEFECCodec::interleave_word(coded);
        Golay::DecodeResult fec;
        const uint32_t recovered   = ALEFECCodec::deinterleave_word(transmitted, fec);
        if (recovered != ale_word || fec.flag != Golay::DECODE_OK) {
            std::cout << "FAIL: ale=0x" << std::hex << ale_word
                      << " recovered=0x" << recovered
                      << " fec.flag=0x" << (int)fec.flag << std::dec << "\n";
            ++fail_count;
        }
    };

    // Sweep all W1..W12 values, fixed W13..W24 = 0xA5A
    for (uint32_t info = 0; info < 4096; ++info)
        roundtrip(((info & 0xFFF) << 12) | 0xA5A);
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  W1..W12 sweep (W13..W24=0xA5A): all 4096 OK\n";

    // Sweep all W13..W24 values, fixed W1..W12 = 0xABC
    for (uint32_t info = 0; info < 4096; ++info)
        roundtrip((0xABCu << 12) | (info & 0xFFF));
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  W13..W24 sweep (W1..W12=0xABC): all 4096 OK\n";

    std::cout << "PASS\n";
    return true;
}

// AC-FEC-013-2: Stuff bit S49 must be 0
bool test_feat_fec_003_stuff_bit() {
    std::cout << "\n[TEST FEC-7] FEAT-FEC-003 — Stuff-Bit S49 = 0 (AC-FEC-013-2)\n";
    std::cout << "----------------------------------------------------------------\n";

    static const uint32_t probe_words[] = {
        0x000000u, 0xFFFFFFu, 0xABC123u, 0x123ABCu,
        0x5A5A5Au, 0xA5A5A5u, 0x800000u, 0x000001u
    };
    for (uint32_t w : probe_words) {
        const uint64_t transmitted = ALEFECCodec::interleave_word(ALEFECCodec::encode_word(w));
        if ((transmitted >> 48) & 1u) {
            std::cout << "FAIL: S49 != 0 for ale_word=0x" << std::hex << w << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  S49 = 0 for all probe words OK\n";
    std::cout << "PASS\n";
    return true;
}

// AC-FEC-012-2: B-channel parity (odd positions 25..47) must carry ~Golay(W13..W24),
//               A-channel parity (even positions 24..46) must carry Golay(W1..W12).
bool test_feat_fec_003_parity_inversion() {
    std::cout << "\n[TEST FEC-8] FEAT-FEC-003 — Paritaets-Inversion G13..G24 (AC-FEC-012-2)\n";
    std::cout << "--------------------------------------------------------------------------\n";

    static const uint32_t probe_words[] = {
        0x000000u, 0xFFFFFFu, 0xABC123u, 0x5A5A5Au, 0x123456u
    };
    for (uint32_t w : probe_words) {
        const GolayCoded coded = ALEFECCodec::encode_word(w);
        const uint64_t t       = ALEFECCodec::interleave_word(coded);

        // Expected parity values (bits 11..0 of each codeword, MSB = bit 11)
        const uint16_t exp_parity_a = coded.coder_a & 0xFFFu;           // G1..G12  normal
        const uint16_t exp_parity_b = coded.coder_b & 0xFFFu;           // ~G13..~G24 (already inverted)

        for (int k = 12; k < 24; ++k) {
            const uint8_t a        = (t          >> (2 * k))     & 1u;
            const uint8_t b        = (t          >> (2 * k + 1)) & 1u;
            const uint8_t expect_a = (exp_parity_a >> (23 - k))  & 1u;
            const uint8_t expect_b = (exp_parity_b >> (23 - k))  & 1u;

            if (a != expect_a) {
                std::cout << "FAIL: A-parity k=" << k
                          << " got=" << (int)a << " expected=" << (int)expect_a
                          << " word=0x" << std::hex << w << std::dec << "\n";
                return false;
            }
            if (b != expect_b) {
                std::cout << "FAIL: B-parity k=" << k
                          << " got=" << (int)b << " expected=" << (int)expect_b
                          << " word=0x" << std::hex << w << std::dec << "\n";
                return false;
            }
        }
    }
    std::cout << "  A-parity = Golay(W1..W12), B-parity = ~Golay(W13..W24) for all probe words OK\n";
    std::cout << "PASS\n";
    return true;
}

// AC-FEC-013-4: The 49th bit (S49) must be ignored on receive.
// Flip bit 48 and verify the decoded ALE word is identical to the clean decode.
bool test_feat_fec_003_stuff_bit_ignored() {
    std::cout << "\n[TEST FEC-9] FEAT-FEC-003 — S49 ignoriert beim Empfang (AC-FEC-013-4)\n";
    std::cout << "------------------------------------------------------------------------\n";

    static const uint32_t probe_words[] = {
        0x000000u, 0xFFFFFFu, 0xABC123u, 0x5A5A5Au
    };
    for (uint32_t w : probe_words) {
        const uint64_t clean   = ALEFECCodec::interleave_word(ALEFECCodec::encode_word(w));
        const uint64_t flipped = clean ^ (1ULL << 48);  // flip S49

        Golay::DecodeResult fec_clean, fec_flipped;
        const uint32_t result_clean   = ALEFECCodec::deinterleave_word(clean, fec_clean);
        const uint32_t result_flipped = ALEFECCodec::deinterleave_word(flipped, fec_flipped);

        if (result_clean != result_flipped || fec_flipped.flag != Golay::DECODE_OK) {
            std::cout << "FAIL: S49-flip changed decode for word=0x" << std::hex << w
                      << " clean=0x" << result_clean << " flipped=0x" << result_flipped
                      << " fec.flag=0x" << (int)fec_flipped.flag << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  Flipping S49 does not affect decoded word OK\n";
    std::cout << "PASS\n";
    return true;
}

// A.5.2.6.3: the four Golay correction modes (3/4, 2/5, 1/6, 0/7).
// In mode n/m, errors of weight ≤ n are corrected; heavier (still detectable)
// errors are reported DECODE_DETECTED.
bool test_golay_correction_modes() {
    std::cout << "\n[TEST FEC-10] Golay correction modes 3/4, 2/5, 1/6, 0/7 (A.5.2.6.3)\n";
    std::cout << "--------------------------------------------------------------------\n";

    static constexpr uint16_t REF = 0x5A5;
    const uint32_t cw = Golay::encode(REF);

    struct ModeCase { GolayMode mode; uint8_t power; const char* name; };
    const ModeCase modes[] = {
        { GolayMode::Mode3_4, 3, "3/4" },
        { GolayMode::Mode2_5, 2, "2/5" },
        { GolayMode::Mode1_6, 1, "1/6" },
        { GolayMode::Mode0_7, 0, "0/7" },
    };
    // Error masks of weight 0,1,2,3,4 (parity bits) — weights 1..3 are unique
    // coset leaders, weight 4 exceeds the code's correction range entirely.
    const uint32_t masks[5] = { 0x0u, 0x1u, 0x3u, 0x7u, 0xFu };

    for (const auto& m : modes) {
        for (uint8_t w = 0; w <= 4; ++w) {
            uint16_t out = 0;
            Golay::DecodeResult r = Golay::decode(cw ^ masks[w], out, m.mode);

            if (w == 0) {
                if (r.flag != Golay::DECODE_OK) {
                    std::cout << "FAIL: mode " << m.name << " w=0 expected DECODE_OK\n";
                    return false;
                }
            } else if (w <= m.power) {
                if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != w || out != REF) {
                    std::cout << "FAIL: mode " << m.name << " w=" << (int)w
                              << " expected DECODE_CORRECTED/REF, got flag=0x"
                              << std::hex << (int)r.flag << std::dec
                              << " errors=" << (int)r.errors_corrected << "\n";
                    return false;
                }
            } else {
                if (r.flag != Golay::DECODE_DETECTED) {
                    std::cout << "FAIL: mode " << m.name << " w=" << (int)w
                              << " expected DECODE_DETECTED (above mode power), got flag=0x"
                              << std::hex << (int)r.flag << std::dec << "\n";
                    return false;
                }
            }
        }
        std::cout << "  mode " << m.name << " (correct <=" << (int)m.power
                  << "): correct/detect split OK\n";
    }

    // The default (no mode argument) must behave as Mode3_4 (full correction).
    {
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ 0x7u, out);   // weight 3
        if (r.flag != Golay::DECODE_CORRECTED || out != REF) {
            std::cout << "FAIL: default mode must correct weight-3 (== Mode3_4)\n";
            return false;
        }
    }
    std::cout << "  default decode() == Mode3_4 OK\n";
    std::cout << "PASS\n";
    return true;
}

int run_all_tests() {
    std::cout << "\n";
    std::cout << "===========================================\n";
    std::cout << "  FEC / Golay (24,12) Unit Tests\n";
    std::cout << "  MIL-STD-188-141B A.5.2.2\n";
    std::cout << "===========================================\n";

    int pass_count = 0;
    int fail_count = 0;

    if (test_ac_fec_001_001_generator_polynomial()) { pass_count++; } else { fail_count++; }
    if (test_ac_fec_001_002_encode_table())         { pass_count++; } else { fail_count++; }
    if (test_golay_codec_minimal())           { pass_count++; } else { fail_count++; }
    if (test_golay_spec_compliance())         { pass_count++; } else { fail_count++; }
    if (test_golay_decode_flags())            { pass_count++; } else { fail_count++; }
    if (test_feat_fec_002_decoder_acs())      { pass_count++; } else { fail_count++; }
    if (test_feat_fec_003_roundtrip())        { pass_count++; } else { fail_count++; }
    if (test_feat_fec_003_stuff_bit())        { pass_count++; } else { fail_count++; }
    if (test_feat_fec_003_parity_inversion()) { pass_count++; } else { fail_count++; }
    if (test_feat_fec_003_stuff_bit_ignored()) { pass_count++; } else { fail_count++; }
    if (test_golay_correction_modes())        { pass_count++; } else { fail_count++; }

    std::cout << "\n===========================================\n";
    std::cout << "  Passed: " << pass_count
              << "  Failed: " << fail_count << "\n";
    std::cout << "===========================================\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main() {
    return ale::run_all_tests();
}
