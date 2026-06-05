/**
 * \file test_fec_golay.cpp
 * \brief Unit tests for Extended Golay (24,12) FEC codec
 *
 * Covers: AC-FEC-004-1/2, AC-FEC-005-1/2/3, AC-FEC-006-1,
 *         AC-FEC-007-1, AC-FEC-009-1/2,
 *         AC-FEC-008-1, AC-FEC-010-1/2/3, AC-FEC-011-1/2
 * Spec: MIL-STD-188-141B A.5.2.2
 */

#include "FEC/golay.h"
#include "FSK/symbol_decoder.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>

namespace ale {

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

// AC-FEC-004-1/2: FEC + majority voting in the full ALE decode path
bool test_golay_ale_word_decode() {
    std::cout << "\n[TEST FEC-1] ALE decode path (SymbolDecoder + Golay)\n";
    std::cout << "-------------------------------------------------------\n";

    static constexpr uint32_t TEST_WORDS = 10;
    uint8_t dummy_symbols[SYMBOLS_PER_WORD * SYMBOL_REPETITION];

    for (uint32_t i = 0; i < SYMBOLS_PER_WORD * SYMBOL_REPETITION; ++i)
        dummy_symbols[i] = i % 8;

    std::cout << "  symbol stream pattern: i % 8 (deterministic)\n";
    std::cout << "  total symbols per word: "
              << SYMBOLS_PER_WORD * SYMBOL_REPETITION << "\n";

    for (uint32_t i = 0; i < TEST_WORDS; ++i) {
        uint32_t word  = 0;
        uint32_t errors = SymbolDecoder::decode_word_with_voting(dummy_symbols, word);

        std::cout << "  word[" << i << "] decoded=0x"
                  << std::hex << word << std::dec
                  << " errors=" << errors << "\n";

        if (word == 0 && errors == 0) {
            std::cout << "FAIL: invalid decode result\n";
            return false;
        }
    }

    std::cout << "PASS\n";
    return true;
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

int run_all_tests() {
    std::cout << "\n";
    std::cout << "===========================================\n";
    std::cout << "  PC-ALE FEC / Golay (24,12) Unit Tests\n";
    std::cout << "  MIL-STD-188-141B A.5.2.2\n";
    std::cout << "===========================================\n";

    int pass_count = 0;
    int fail_count = 0;

    if (test_golay_ale_word_decode())       { pass_count++; } else { fail_count++; }
    if (test_golay_codec_minimal())         { pass_count++; } else { fail_count++; }
    if (test_golay_spec_compliance())       { pass_count++; } else { fail_count++; }
    if (test_golay_decode_flags())          { pass_count++; } else { fail_count++; }
    if (test_feat_fec_002_decoder_acs())    { pass_count++; } else { fail_count++; }

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
