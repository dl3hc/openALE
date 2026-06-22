/**
 * \file test_fec_golay.cpp
 * \brief Unit tests for Extended Golay (24,12) FEC codec and word interleaver
 *
 * Covers: AC-FEC-001-001, AC-FEC-001-002,
 *         AC-FEC-002-001, AC-FEC-002-002,
 *         AC-FEC-003-001, AC-FEC-003-002,
 *         AC-FEC-004-001, AC-FEC-004-002,
 *         AC-FEC-005-001,
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
#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"

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

    // Verify the table has 4096 entries: encode covers all 2^12 info words.
    static_assert(Golay::ENCODE_TABLE_SIZE == 4096u,
                  "ENCODE_TABLE_SIZE must equal 4096");
    std::cout << "  ENCODE_TABLE_SIZE = " << Golay::ENCODE_TABLE_SIZE
              << " (expected 4096)\n";

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

// AC-FEC-002-001: Decoder reliably corrects 1, 2, or 3 bit errors; 4+ → DETECTED.
// REQ-FEC-010 (FEAT-FEC-002, MIL-STD-188-141B A.5.2.2.2.2)
//
// Verification hint: all C(24,3) = 2024 three-bit error patterns tested.
bool test_ac_fec_002_001_three_bit_correction() {
    std::cout << "\n[TEST FEC-11] AC-FEC-002-001: 1/2/3-Bit-Fehler korrigierbar, 4+ DETECTED\n";
    std::cout << "--------------------------------------------------------------------------\n";

    static constexpr uint16_t REF = 0xA5A;
    const uint32_t cw = Golay::encode(REF);
    uint32_t fail_count = 0;

    // Weight-1: all 24 single-bit patterns → DECODE_CORRECTED, errors_corrected == 1
    for (int i = 0; i < 24; ++i) {
        uint32_t mask = 1u << i;
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ mask, out);
        if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != 1 || out != REF) {
            std::cout << "FAIL weight-1 mask=0x" << std::hex << mask
                      << " flag=0x" << (int)r.flag
                      << " ec=" << std::dec << (int)r.errors_corrected << "\n";
            ++fail_count;
        }
    }
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  weight-1 (24 patterns): all DECODE_CORRECTED, errors_corrected=1 OK\n";

    // Weight-2: all C(24,2) = 276 patterns → DECODE_CORRECTED, errors_corrected == 2
    for (int i = 0; i < 24; ++i) {
        for (int j = i + 1; j < 24; ++j) {
            uint32_t mask = (1u << i) | (1u << j);
            uint16_t out = 0;
            Golay::DecodeResult r = Golay::decode(cw ^ mask, out);
            if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != 2 || out != REF) {
                std::cout << "FAIL weight-2 mask=0x" << std::hex << mask << std::dec << "\n";
                ++fail_count;
            }
        }
    }
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  weight-2 (276 patterns): all DECODE_CORRECTED, errors_corrected=2 OK\n";

    // Weight-3: all C(24,3) = 2024 patterns → DECODE_CORRECTED, errors_corrected == 3
    for (int i = 0; i < 24; ++i) {
        for (int j = i + 1; j < 24; ++j) {
            for (int k = j + 1; k < 24; ++k) {
                uint32_t mask = (1u << i) | (1u << j) | (1u << k);
                uint16_t out = 0;
                Golay::DecodeResult r = Golay::decode(cw ^ mask, out);
                if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != 3 || out != REF) {
                    std::cout << "FAIL weight-3 mask=0x" << std::hex << mask << std::dec << "\n";
                    ++fail_count;
                }
            }
        }
    }
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  weight-3 (2024 patterns): all DECODE_CORRECTED, errors_corrected=3 OK\n";

    // Weight-4 samples → DECODE_DETECTED (not correctable)
    static const uint32_t w4_masks[] = { 0x0000000Fu, 0x000000F0u, 0x00000F00u, 0x0000F000u };
    for (uint32_t m : w4_masks) {
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ m, out);
        if (r.flag != Golay::DECODE_DETECTED) {
            std::cout << "FAIL weight-4 mask=0x" << std::hex << m
                      << " expected DECODE_DETECTED got flag=0x" << (int)r.flag << std::dec << "\n";
            ++fail_count;
        }
    }
    if (fail_count) { std::cout << "FAIL\n"; return false; }
    std::cout << "  weight-4 (4 samples): all DECODE_DETECTED OK\n";

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

// AC-FEC-002-002: Decoder returns DECODE_OK, DECODE_CORRECTED, DECODE_DETECTED.
// REQ-FEC-011 (FEAT-FEC-002, MIL-STD-188-141B A.5.2.2.2.2 / A.5.2.6)
//
// Verifies that all three flag constants are defined in golay.h and that the
// decoder assigns each flag to the correct situation:
//   DECODE_OK        — codeword has no errors (syndrome == 0)
//   DECODE_CORRECTED — 1, 2, or 3 bit errors detected and corrected
//   DECODE_DETECTED  — 4+ bit errors detected but not correctable
bool test_ac_fec_002_002_decode_flags() {
    std::cout << "\n[TEST FEC-12] AC-FEC-002-002: DECODE_OK / DECODE_CORRECTED / DECODE_DETECTED\n";
    std::cout << "-------------------------------------------------------------------------------\n";

    static constexpr uint16_t REF = 0x6C3;
    const uint32_t cw = Golay::encode(REF);

    // DECODE_OK: error-free codeword
    {
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw, out);
        if (r.flag != Golay::DECODE_OK || r.errors_corrected != 0 || out != REF) {
            std::cout << "FAIL: clean codeword must return DECODE_OK, errors_corrected=0\n";
            return false;
        }
        std::cout << "  DECODE_OK: syndrome=0 for valid codeword OK\n";
    }

    // DECODE_CORRECTED: weight 1, 2, 3
    {
        struct { uint32_t mask; uint8_t wt; } cases[] = {
            { 0x000001u, 1 },
            { 0x000003u, 2 },
            { 0x000007u, 3 },
        };
        for (auto& c : cases) {
            uint16_t out = 0;
            Golay::DecodeResult r = Golay::decode(cw ^ c.mask, out);
            if (r.flag != Golay::DECODE_CORRECTED || r.errors_corrected != c.wt || out != REF) {
                std::cout << "FAIL: weight-" << (int)c.wt
                          << " error must return DECODE_CORRECTED, errors_corrected="
                          << (int)c.wt << "; got flag=0x" << std::hex << (int)r.flag
                          << " ec=" << std::dec << (int)r.errors_corrected << "\n";
                return false;
            }
        }
        std::cout << "  DECODE_CORRECTED: weight 1/2/3 errors corrected, flag and count correct OK\n";
    }

    // DECODE_DETECTED: weight 4 (exceeds correction capacity)
    {
        uint16_t out = 0;
        Golay::DecodeResult r = Golay::decode(cw ^ 0x0000000Fu, out);
        if (r.flag != Golay::DECODE_DETECTED) {
            std::cout << "FAIL: weight-4 error must return DECODE_DETECTED; got flag=0x"
                      << std::hex << (int)r.flag << std::dec << "\n";
            return false;
        }
        std::cout << "  DECODE_DETECTED: weight-4 error not corrected, flag correct OK\n";
    }

    std::cout << "PASS\n";
    return true;
}

// AC-FEC-003-001: Interleave pattern A₁B₁A₂B₂…A₂₄B₂₄S₄₉
// REQ-FEC-012 (FEAT-FEC-003, MIL-STD-188-141B A.5.2.2.3)
//
// Verifies the exact bit-position mapping:
//   out[2k]   = Coder-A-Bit k  (bit 23-k of sequence_a)  for k = 0..23
//   out[2k+1] = Coder-B-Bit k  (bit 23-k of sequence_b)  for k = 0..23
//   out[48]   = 0  (Stuff-Bit S49)
bool test_ac_fec_003_001_interleave_pattern() {
    std::cout << "\n[TEST FEC-13] AC-FEC-003-001: Interleave-Muster A1B1A2B2...A24B24S49\n";
    std::cout << "-----------------------------------------------------------------------\n";

    // (1) Single A-bit: Coder-A-Bit k → output position 2k, everything else 0
    for (int k = 0; k < 24; ++k) {
        const uint32_t seq_a    = 1u << (23 - k);
        const uint64_t out      = WordInterleaver::interleave(seq_a, 0u);
        const uint64_t expected = 1ULL << (2 * k);
        if (out != expected) {
            std::cout << "FAIL: A-bit k=" << k
                      << " seq_a=0x" << std::hex << seq_a
                      << " got=0x"      << out
                      << " expected=0x" << expected << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  A-bits: each Coder-A-Bit k at output position 2k (k=0..23) OK\n";

    // (2) Single B-bit: Coder-B-Bit k → output position 2k+1, everything else 0
    for (int k = 0; k < 24; ++k) {
        const uint32_t seq_b    = 1u << (23 - k);
        const uint64_t out      = WordInterleaver::interleave(0u, seq_b);
        const uint64_t expected = 1ULL << (2 * k + 1);
        if (out != expected) {
            std::cout << "FAIL: B-bit k=" << k
                      << " seq_b=0x" << std::hex << seq_b
                      << " got=0x"      << out
                      << " expected=0x" << expected << std::dec << "\n";
            return false;
        }
    }
    std::cout << "  B-bits: each Coder-B-Bit k at output position 2k+1 (k=0..23) OK\n";

    // (3) All-ones: bits 0..47 all set, S49 = 0
    {
        const uint64_t out          = WordInterleaver::interleave(0x00FFFFFFu, 0x00FFFFFFu);
        const uint64_t expected_low = (1ULL << 48) - 1ULL;
        if ((out & expected_low) != expected_low) {
            std::cout << "FAIL: all-ones: not all 48 bits 0..47 set: got=0x"
                      << std::hex << out << std::dec << "\n";
            return false;
        }
        if ((out >> 48) & 1ULL) {
            std::cout << "FAIL: all-ones: S49 (bit 48) != 0\n";
            return false;
        }
    }
    std::cout << "  All-ones: bits 0..47 set, S49=0 OK\n";

    // (4) A-only: even positions 0,2,...,46 set; odd positions 1,3,...,47 = 0; S49 = 0
    {
        const uint64_t out = WordInterleaver::interleave(0x00FFFFFFu, 0u);
        for (int k = 0; k < 24; ++k) {
            if (!((out >> (2 * k)) & 1ULL)) {
                std::cout << "FAIL: A-only: position " << (2 * k) << " not set\n";
                return false;
            }
            if ((out >> (2 * k + 1)) & 1ULL) {
                std::cout << "FAIL: A-only: position " << (2 * k + 1) << " unexpectedly set\n";
                return false;
            }
        }
        if ((out >> 48) & 1ULL) { std::cout << "FAIL: A-only: S49 != 0\n"; return false; }
    }
    std::cout << "  A-only: even positions set, odd positions 0, S49=0 OK\n";

    // (5) B-only: odd positions 1,3,...,47 set; even positions 0,2,...,46 = 0; S49 = 0
    {
        const uint64_t out = WordInterleaver::interleave(0u, 0x00FFFFFFu);
        for (int k = 0; k < 24; ++k) {
            if ((out >> (2 * k)) & 1ULL) {
                std::cout << "FAIL: B-only: position " << (2 * k) << " unexpectedly set\n";
                return false;
            }
            if (!((out >> (2 * k + 1)) & 1ULL)) {
                std::cout << "FAIL: B-only: position " << (2 * k + 1) << " not set\n";
                return false;
            }
        }
        if ((out >> 48) & 1ULL) { std::cout << "FAIL: B-only: S49 != 0\n"; return false; }
    }
    std::cout << "  B-only: odd positions set, even positions 0, S49=0 OK\n";

    std::cout << "PASS\n";
    return true;
}

// AC-FEC-003-002: Coder-B-Paritaet ist invertiert
// REQ-FEC-013 (FEAT-FEC-003, MIL-STD-188-141B A.5.2.2.3)
//
// Three checks:
//  (1) encode_word(): coder_b[11:0] == ~Golay(W13..W24)[11:0]
//  (2) deinterleave_word() undoes the inversion, recovering the ALE word with DECODE_OK
//  (3) Negative: decoding coder_b without un-inversion yields DECODE_DETECTED
//      (12 inverted parity bits → distance 12 from nearest valid codeword > correction cap. of 3)
bool test_ac_fec_003_002_coder_b_parity_inversion() {
    std::cout << "\n[TEST FEC-14] AC-FEC-003-002: Coder-B Paritaets-Inversion (~G13..~G24)\n";
    std::cout << "------------------------------------------------------------------------\n";

    static const uint32_t probe_words[] = {
        0xABC123u, 0x5A5A5Au, 0x123456u, 0xFFFFFFu, 0x800001u
    };

    for (uint32_t w : probe_words) {
        const uint16_t w_lower = static_cast<uint16_t>(w & 0xFFFu);

        // (1) Encode: coder_b low 12 bits must equal bitwise NOT of raw Golay(w_lower) parity
        const uint16_t raw_parity  = Golay::encode(w_lower) & 0xFFFu;
        const uint16_t inv_parity  = (~raw_parity) & 0xFFFu;

        const GolayCoded coded      = ALEFECCodec::encode_word(w);
        const uint16_t coded_parity = coded.coder_b & 0xFFFu;

        if (coded_parity != inv_parity) {
            std::cout << "FAIL (1) encode w=0x" << std::hex << w
                      << ": coder_b parity=0x" << coded_parity
                      << " expected ~Golay(w_lower)=0x" << inv_parity << std::dec << "\n";
            return false;
        }

        // (2) Decode: deinterleave_word must undo inversion and recover the ALE word
        const uint64_t tx = ALEFECCodec::interleave_word(coded);
        Golay::DecodeResult fec;
        const uint32_t recovered = ALEFECCodec::deinterleave_word(tx, fec);

        if (recovered != w || fec.flag != Golay::DECODE_OK) {
            std::cout << "FAIL (2) decode w=0x" << std::hex << w
                      << ": recovered=0x" << recovered
                      << " fec.flag=0x" << (int)fec.flag << std::dec << "\n";
            return false;
        }

        // (3) Negative: decoding coded.coder_b WITHOUT un-inversion.
        // coded.coder_b = valid_codeword(w_lower) XOR 0x000FFF (12 parity bits flipped).
        // 0x000FFF is not a valid codeword, and minimum Golay distance is 8, so
        // coded.coder_b is at distance >= 4 from every valid codeword → DECODE_DETECTED.
        uint16_t out_raw = 0;
        Golay::DecodeResult r_raw = Golay::decode(coded.coder_b, out_raw);
        if (r_raw.flag != Golay::DECODE_DETECTED) {
            std::cout << "FAIL (3) negative w=0x" << std::hex << w
                      << ": decoding coder_b without un-inversion returned flag=0x"
                      << (int)r_raw.flag << " (expected DECODE_DETECTED)\n" << std::dec;
            return false;
        }
    }

    std::cout << "  (1) coder_b[11:0] == ~Golay(W13..W24)[11:0] for all probe words OK\n";
    std::cout << "  (2) deinterleave_word restores ALE word with DECODE_OK OK\n";
    std::cout << "  (3) negative: coder_b without un-inversion -> DECODE_DETECTED OK\n";
    std::cout << "PASS\n";
    return true;
}

// AC-FEC-004-001: 3×-Redundanz — Jedes 49-Bit-Wort dreifach gesendet
// REQ-FEC-014, REQ-FEC-015 (FEAT-FEC-004, MIL-STD-188-141B A.5.2.2.4)
//
// ALEEncoder::encode_tx49 maps a 49-bit word to exactly 49 8-FSK symbols.
// On-air stream: 147 bits = tx49 concatenated three times → stream[j] = tx49[j%49].
// Each input bit i appears at stream positions i, i+49, i+98 (Stride-49 invariant).
bool test_ac_fec_004_001_triple_redundancy() {
    std::cout << "\n[TEST FEC-16] AC-FEC-004-001: 3×-Redundanz — 49-Bit-Wort dreifach gesendet\n";
    std::cout << "-------------------------------------------------------------------------------\n";

    static_assert(SYMBOLS_PER_WORD == 49, "3×49 bits / 3 bits-per-symbol = 49-symbol TX burst");
    static_assert(BITS_PER_SYMBOL  ==  3, "8-FSK: 3 bits per symbol");

    // Exhaustive single-bit walk: for each bit i set in tx49, verify the
    // 147-bit stream reconstructed from the SymbolFrame satisfies stream[j] = tx49[j%49].
    bool all_ok = true;
    for (uint32_t i = 0; i < SYMBOLS_PER_WORD; ++i) {
        const uint64_t    tx49  = 1ULL << i;
        const SymbolFrame frame = ALEEncoder::encode_tx49(tx49);

        // All 49 symbols must be 3-bit values (0–7).
        for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k) {
            if (frame[k] > 7u) {
                std::cout << "FAIL: bit i=" << i << " sym[" << k << "]="
                          << (int)frame[k] << " out of range [0,7]\n";
                all_ok = false;
            }
        }

        // Reconstruct 147-bit stream (MSB-first within each 3-bit symbol).
        uint8_t stream[3 * SYMBOLS_PER_WORD] = {};
        for (uint32_t k = 0; k < SYMBOLS_PER_WORD; ++k)
            for (uint32_t b = 0; b < BITS_PER_SYMBOL; ++b)
                stream[k * BITS_PER_SYMBOL + b] =
                    (frame[k] >> (BITS_PER_SYMBOL - 1u - b)) & 1u;

        // Verify stream[j] == (tx49 >> (j%49)) & 1 for all j = 0..146.
        for (uint32_t j = 0; j < 3 * SYMBOLS_PER_WORD; ++j) {
            const uint8_t expected = (tx49 >> (j % SYMBOLS_PER_WORD)) & 1u;
            if (stream[j] != expected) {
                std::cout << "FAIL: bit i=" << i
                          << " stream[" << j << "]=" << (int)stream[j]
                          << " expected=" << (int)expected << "\n";
                all_ok = false;
            }
        }

        // Explicit Stride-49 check: bit i must appear at exactly positions i, i+49, i+98.
        if (stream[i] != 1 || stream[i + 49] != 1 || stream[i + 98] != 1) {
            std::cout << "FAIL: bit i=" << i
                      << " not found at all three stride-49 positions\n";
            all_ok = false;
        }
    }

    if (!all_ok) return false;

    std::cout << "  TX burst size = " << SYMBOLS_PER_WORD << " symbols (49×3 = 147 bits) OK\n";
    std::cout << "  All 49 symbols in range [0,7] for all probe words OK\n";
    std::cout << "  stream[j] == tx49[j%49] for j=0..146: Stride-49 invariant holds OK\n";
    std::cout << "  Each input bit i at positions i, i+49, i+98 OK\n";
    std::cout << "PASS\n";
    return true;
}

// Stride-49 majority vote: recovers tx49 from a SymbolFrame.
// Mirrors ALEEncoder::encode_tx49() inverse: sym[k] = {tx49[3k%49], tx49[(3k+1)%49], tx49[(3k+2)%49]}
// For each bit b, copies sit at stream positions b, b+49, b+98 → majority vote.
static uint64_t decode_word_with_voting(const SymbolFrame& frame) {
    uint64_t result = 0;
    for (uint32_t b = 0; b < SYMBOLS_PER_WORD - 1u; ++b) {
        auto bit_at = [&](uint32_t s) -> uint8_t {
            return (frame[s / 3] >> (2u - (s % 3u))) & 1u;
        };
        uint8_t v0 = bit_at(b), v1 = bit_at(b + 49), v2 = bit_at(b + 98);
        if ((v0 + v1 + v2) >= 2) result |= (1ULL << b);
    }
    return result;
}

// AC-FEC-004-002: Majority-Vote auf Stride-49-Basis
// REQ-FEC-016, REQ-FEC-017 (FEAT-FEC-004, MIL-STD-188-141B A.5.2.2.4)
//
// decode_word_with_voting() must:
//  (1) Round-trip: encode_tx49(tx49) -> decode_word_with_voting -> recover tx49 exactly.
//  (2) Single-copy error: corrupt one symbol in copy 0, 1, or 2 independently;
//      majority vote must still recover the correct tx49.
//  (3) S49 invariant: bit 48 of the output is always 0, regardless of symbol content.
//  (4) Stride-49: mod-49 indexing is used (not linear), verified via deliberate
//      corruption at linear positions that would corrupt stride-49 vote incorrectly.
bool test_ac_fec_004_002_majority_vote_stride49() {
    std::cout << "\n[TEST FEC-17] AC-FEC-004-002: Majority-Vote auf Stride-49-Basis\n";
    std::cout << "------------------------------------------------------------------\n";

    // (1) Round-trip: clean channel — for several tx49 words, encode then decode.
    {
        const uint64_t probe_words[] = {
            0x000000000000ULL, 0xFFFFFFFFFFFFULL,  // all zeros / all ones (48 bits)
            0xABCDEF123456ULL, 0x5A5A5A5A5A5AULL,
            0x800000000001ULL, 0x000000000001ULL,
        };
        for (uint64_t tx49 : probe_words) {
            const uint64_t tx49_48 = tx49 & ((1ULL << 48) - 1ULL);  // mask to 48 bits
            const SymbolFrame frame = ALEEncoder::encode_tx49(tx49_48);
            const uint64_t recovered = decode_word_with_voting(frame);
            if (recovered != tx49_48) {
                std::cout << "FAIL (1) round-trip tx49=0x" << std::hex << tx49_48
                          << " recovered=0x" << recovered << std::dec << "\n";
                return false;
            }
        }
        std::cout << "  (1) Round-trip clean channel: all probe words recovered OK\n";
    }

    // (2) Single-copy error: corrupt one symbol in exactly one of the three copies;
    //     the other two copies must carry the correct value → 2-of-3 majority wins.
    {
        const uint64_t tx49 = 0x123456789ABCULL & ((1ULL << 48) - 1ULL);
        const SymbolFrame clean = ALEEncoder::encode_tx49(tx49);

        // Each of the 49 symbols is shared across all three copies via mod-49 indexing,
        // so "copy k" lives at symbol offsets 0..48 * copy k (all same symbol — because
        // the 49-symbol frame itself encodes 3 copies bitwise interleaved in 147 bits).
        // To corrupt exactly copy k (k=0,1,2), we corrupt bit i in stream positions
        // i+k*49.  Bit i occupies stream position i, so symbol[i/3] bit (2-i%3).
        // Flipping symbol[i/3] bit is easiest as XOR with (1 << (2 - i%3)) & 7.
        //
        // We test corruption at bit positions 0,7,16,31,47 (representative spread).
        static constexpr uint32_t test_bits[] = { 0, 7, 16, 31, 47 };
        uint32_t fail_count = 0;

        for (uint32_t copy_idx = 0; copy_idx < 3; ++copy_idx) {
            for (uint32_t bit_i : test_bits) {
                SymbolFrame corrupted = clean;
                // Bit i in copy copy_idx sits at stream position (bit_i + copy_idx*49).
                // That stream position maps to symbol s = (bit_i + copy_idx*49) / 3
                // and bit within symbol b = 2 - (bit_i + copy_idx*49) % 3.
                const uint32_t stream_pos = bit_i + copy_idx * 49;
                const uint32_t sym_idx    = stream_pos / 3;
                const uint32_t bit_in_sym = 2u - (stream_pos % 3u);
                corrupted[sym_idx] ^= static_cast<uint8_t>(1u << bit_in_sym);
                corrupted[sym_idx] &= 0x07u;

                const uint64_t recovered = decode_word_with_voting(corrupted);
                if (recovered != tx49) {
                    std::cout << "FAIL (2) single-copy error: copy=" << copy_idx
                              << " bit=" << bit_i
                              << " sym=" << sym_idx
                              << " recovered=0x" << std::hex << recovered
                              << " expected=0x" << tx49 << std::dec << "\n";
                    ++fail_count;
                }
            }
        }
        if (fail_count) {
            std::cout << "FAIL\n";
            return false;
        }
        std::cout << "  (2) Single-copy error (all 3 copies × 5 bit positions): 2-of-3 majority wins OK\n";
    }

    // (3) S49 invariant: bit 48 of returned tx49 is always 0.
    {
        // Feed all-ones symbols (symbol value 7 = 0b111): all 147 stream bits = 1.
        // Without the S49 guard, bit 48 would be voted 1. It must be 0.
        const SymbolFrame all_ones = []() {
            SymbolFrame f;
            f.fill(7u);
            return f;
        }();
        const uint64_t result = decode_word_with_voting(all_ones);
        if ((result >> 48) & 1ULL) {
            std::cout << "FAIL (3) S49 invariant: bit 48 != 0 for all-ones input\n";
            return false;
        }
        std::cout << "  (3) S49 invariant: bit 48 = 0 even for all-ones input OK\n";
    }

    // (4) Stride-49 index correctness: exhaustive single-bit walk.
    //     For each bit i (0..47), set tx49 = 1<<i, encode, decode — must recover 1<<i.
    //     This verifies the mod-49 stride is used (not a naive 1-of-3 linear vote).
    {
        uint32_t fail_count = 0;
        for (uint32_t i = 0; i < SYMBOLS_PER_WORD - 1u; ++i) {
            const uint64_t tx49  = 1ULL << i;
            const SymbolFrame fr = ALEEncoder::encode_tx49(tx49);
            const uint64_t got   = decode_word_with_voting(fr);
            if (got != tx49) {
                std::cout << "FAIL (4) single-bit i=" << i
                          << " got=0x" << std::hex << got
                          << " expected=0x" << tx49 << std::dec << "\n";
                ++fail_count;
            }
        }
        if (fail_count) {
            std::cout << "FAIL\n";
            return false;
        }
        std::cout << "  (4) Exhaustive single-bit walk (48 bits): Stride-49 indexing correct OK\n";
    }

    std::cout << "PASS\n";
    return true;
}

bool test_ac_fec_005_001_unanimous_vote_counter() {
    std::cout << "\n[TEST FEC-18] AC-FEC-005-001: unanimous_votes in ALEWord (exkl. S49)\n";
    std::cout << "----------------------------------------------------------------------\n";

    // (1) Default-constructed ALEWord: unanimous_votes must be 0.
    {
        ALEWord w;
        static_assert(sizeof(w.unanimous_votes) == 1, "unanimous_votes must be uint8_t");
        if (w.unanimous_votes != 0) {
            std::cout << "FAIL (1) default unanimous_votes != 0\n";
            return false;
        }
        std::cout << "  (1) unanimous_votes field exists, default = 0 OK\n";
    }

    // Build a clean, fully encoded ALE word for the decode tests.
    const char chars[3] = { 'A', 'B', 'C' };
    const ALEWord ref_word = WordParser::make_word(PreambleType::TO, chars);
    if (!ref_word.valid) {
        std::cout << "FAIL: make_word returned invalid word\n";
        return false;
    }
    const SymbolFrame clean_frame = ALEEncoder::encode(ref_word);

    // (2) Clean channel: all 48 data bits are unanimous; S49 must NOT be counted.
    //     Expected: unanimous_votes == 48, not 49.
    {
        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0xFF;
        const bool ok = ALEDecoder::decode(clean_frame.data(), out, fec, &uv);
        if (!ok) {
            std::cout << "FAIL (2) clean frame decode failed\n";
            return false;
        }
        if (uv != 48u) {
            std::cout << "FAIL (2) unanimous_votes_out=" << (int)uv
                      << " expected 48\n";
            return false;
        }
        if (out.unanimous_votes != 48u) {
            std::cout << "FAIL (2) ALEWord.unanimous_votes=" << (int)out.unanimous_votes
                      << " expected 48\n";
            return false;
        }
        std::cout << "  (2) Clean channel: unanimous_votes=48 (S49 excluded) OK\n";
    }

    // (3) One copy of bit 0 corrupted → that bit becomes non-unanimous → count = 47.
    //     Bit 0, copy 1 → stream position 0+1*49=49 → symbol 49/3=16, sub-bit 2-49%3=1.
    {
        SymbolFrame corrupted = clean_frame;
        constexpr uint32_t stream_pos = 49u;
        constexpr uint32_t sym_idx    = stream_pos / 3u;        // 16
        constexpr uint32_t bit_in_sym = 2u - (stream_pos % 3u); // 1
        corrupted[sym_idx] ^= static_cast<uint8_t>(1u << bit_in_sym);
        corrupted[sym_idx] &= 0x07u;

        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0xFF;
        ALEDecoder::decode(corrupted.data(), out, fec, &uv);
        if (uv != 47u) {
            std::cout << "FAIL (3) after single-copy corruption: unanimous_votes="
                      << (int)uv << " expected 47\n";
            return false;
        }
        std::cout << "  (3) Single-copy error at bit 0: unanimous_votes=47 OK\n";
    }

    // (4) All-ones symbol stream: bits 0..47 are unanimous (1/1/1) AND bit 48 (S49)
    //     is also "unanimous" among its copies — but must NOT be counted.
    //     Expected: unanimous_votes == 48, not 49.
    {
        SymbolFrame all_ones;
        all_ones.fill(7u);  // 0b111 → every stream bit = 1

        ALEWord out;
        Golay::DecodeResult fec;
        uint8_t uv = 0xFF;
        ALEDecoder::decode(all_ones.data(), out, fec, &uv);
        if (uv != 48u) {
            std::cout << "FAIL (4) all-ones stream: unanimous_votes="
                      << (int)uv << " expected 48 (S49 not counted)\n";
            return false;
        }
        std::cout << "  (4) All-ones stream: unanimous_votes=48 (S49 not counted) OK\n";
    }

    std::cout << "PASS\n";
    return true;
}

int run_all_tests() {

    int pass_count = 0;
    int fail_count = 0;

    if (test_ac_fec_001_001_generator_polynomial()) { pass_count++; } else { fail_count++; }
    if (test_ac_fec_001_002_encode_table())         { pass_count++; } else { fail_count++; }
    if (test_ac_fec_002_001_three_bit_correction()) { pass_count++; } else { fail_count++; }
    if (test_ac_fec_002_002_decode_flags())         { pass_count++; } else { fail_count++; }
    if (test_ac_fec_003_001_interleave_pattern())   { pass_count++; } else { fail_count++; }
    if (test_ac_fec_003_002_coder_b_parity_inversion()) { pass_count++; } else { fail_count++; }
    if (test_ac_fec_004_001_triple_redundancy())    { pass_count++; } else { fail_count++; }
    if (test_ac_fec_004_002_majority_vote_stride49()) { pass_count++; } else { fail_count++; }
    if (test_ac_fec_005_001_unanimous_vote_counter()) { pass_count++; } else { fail_count++; }
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
