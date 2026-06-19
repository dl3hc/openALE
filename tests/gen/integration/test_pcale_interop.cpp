/**
 * \file tests/test_pcale_interop.cpp
 * \brief Bit-exact interoperability check against the PCALE / ALELite reference.
 *
 * Purpose
 * ───────
 * Two ale_cli instances link fine, and PCALE can decode ale_cli, but ale_cli
 * FAILS to decode PCALE's reply.  This test isolates the question:
 *
 *     "Is the incompatibility at the bit/codec level, or only physical
 *      (audio rate / timing / sync)?"
 *
 * It ports the reference encoder TxFEC() from ALELite/SourceALE/ALEDoc.cpp
 * VERBATIM (reusing our own Golay::encode, whose 4096-entry table is byte-for
 * byte identical to ALELite's enc[]), then:
 *
 *   1. Compares the reference's 49 symbols against ALEEncoder::encode() for the
 *      same 24-bit word  →  confirms encoder equivalence.
 *   2. Feeds the reference's 49 symbols into ALEDecoder::decode()
 *      →  confirms our decoder can recover words produced by PCALE.
 *
 * No audio, no modem, no timing: a pure, deterministic codec-layer check.
 */

#include "Codec/ale_encoder.h"
#include "Codec/ale_decoder.h"
#include "Word/ale_word.h"
#include "FEC/golay.h"
#include "FEC/ale_fec_codec.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

using namespace ale;

static int g_failures = 0;

static void check(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}

// ── Reference encoder, ported verbatim from ALELite TxFEC() ─────────────────────
//
// Original (ALEDoc.cpp):
//   #define encode(x) (((long) x << 12) | enc[x])
//   void Golay(long w, long G[]) {
//     G[0] = encode(w >> 12);
//     G[1] = encode(w & 07777) ^ 07777;   // invert B-word check bits
//   }
//   short *TxFEC(long ALE_WORD, short *Txw) { ... }
//
// Our Golay::encode(info) returns ((info<<12) | GOLAY_ENCODE_TABLE[info]), and
// GOLAY_ENCODE_TABLE is verified identical to ALELite's enc[] — so we reuse it
// for the Golay step and port only the interleave/redundancy bit-shuffle.

static void reference_TxFEC(uint32_t ALE_WORD, uint8_t Txw[49])
{
    long G[2];
    G[0] = static_cast<long>(Golay::encode(static_cast<uint16_t>((ALE_WORD >> 12) & 0xFFF)));
    G[1] = static_cast<long>(Golay::encode(static_cast<uint16_t>( ALE_WORD        & 0xFFF))) ^ 07777;

    long a, b;
    int  i;
    long T[49];   // mirror of the original `short Txw[49]` working buffer

    a = G[0] << 2;
    b = G[1] << 1;

    T[48] = (a & 4) | (b & 2);
    for (i = 46; i > 33; i -= 2) {
        T[i]    = (a & 040) | (b & 020);
        a >>= 1; b >>= 1;
        T[i]    = (T[i] | (a & 010)) >> 3;
        T[i + 1] = (b & 4);
        a >>= 1; b >>= 1;
        T[i + 1] |= (a & 2) | (b & 1);
        a >>= 1; b >>= 1;
    }
    a |= (G[1] << 5);
    b |= (G[0] << 5);   // stuff bit for free
    for (i = 32; i > 16; i -= 2) {
        T[i]    = (a & 040) | (b & 020);
        a >>= 1; b >>= 1;
        T[i]    = (T[i] | (a & 010)) >> 3;
        T[i + 1] = (b & 4);
        a >>= 1; b >>= 1;
        T[i + 1] |= (a & 2) | (b & 1);
        a >>= 1; b >>= 1;
    }
    a |= (G[0] << 6);
    b |= (G[1] << 5);   // stuff bit for free
    for (i = 16; i >= 0; i -= 2) {
        T[i]    = (a & 040) | (b & 020);
        a >>= 1; b >>= 1;
        T[i]    = (T[i] | (a & 010)) >> 3;
        T[i + 1] = (b & 4);
        a >>= 1; b >>= 1;
        T[i + 1] |= (a & 2) | (b & 1);
        a >>= 1; b >>= 1;
    }

    for (i = 0; i < 49; ++i)
        Txw[i] = static_cast<uint8_t>(T[i] & 0x7);
}

// ── Helpers ─────────────────────────────────────────────────────────────────────

static uint32_t raw24_of(const ALEWord& w)
{
    return (static_cast<uint32_t>(w.type) << 21) | (w.raw_payload & 0x1F'FFFFu);
}

static void dump_symbols(const char* label, const uint8_t* s)
{
    std::fprintf(stderr, "  %s:", label);
    for (int i = 0; i < 49; ++i) std::fprintf(stderr, " %u", s[i]);
    std::fprintf(stderr, "\n");
}

// ── Test 1: encoder equivalence (our encoder vs PCALE TxFEC) ────────────────────

static void test_encoder_matches_reference()
{
    struct Case { PreambleType type; const char* addr; };
    const Case cases[] = {
        { PreambleType::TO,   "BOB" },
        { PreambleType::TIS,  "SAM" },
        { PreambleType::TWAS, "BOB" },
        { PreambleType::TO,   "JOE" },
        { PreambleType::TIS,  "ABC" },
        { PreambleType::DATA, "UEL" },
        { PreambleType::REP,  "XYZ" },
        { PreambleType::CMD,  "AB@" },
    };

    int mismatches = 0;
    for (const auto& c : cases) {
        const ALEWord w = WordParser::make_word(c.type, c.addr);
        check(w.valid, "make_word failed");

        const SymbolFrame ours = ALEEncoder::encode(w);

        uint8_t ref[49];
        reference_TxFEC(raw24_of(w), ref);

        if (std::memcmp(ours.data(), ref, 49) != 0) {
            ++mismatches;
            std::fprintf(stderr, "ENCODER MISMATCH for %s '%s' (raw24=0x%06X):\n",
                         WordParser::word_type_name(c.type), c.addr, raw24_of(w));
            dump_symbols("ours", ours.data());
            dump_symbols("pcale", ref);
        }
    }
    check(mismatches == 0, "our encoder output differs from PCALE TxFEC");
    if (mismatches == 0)
        std::printf("PASS  test_encoder_matches_reference  (%zu words bit-identical)\n",
                    sizeof(cases) / sizeof(cases[0]));
}

// ── Test 2: our decoder recovers PCALE-encoded words ────────────────────────────

static void test_decoder_accepts_reference()
{
    struct Case { PreambleType type; const char* addr; };
    const Case cases[] = {
        { PreambleType::TO,   "BOB" },
        { PreambleType::TIS,  "SAM" },
        { PreambleType::TWAS, "BOB" },
        { PreambleType::TO,   "JOE" },
        { PreambleType::TIS,  "ABC" },
        { PreambleType::DATA, "UEL" },
        { PreambleType::REP,  "XYZ" },
    };

    int failures = 0;
    for (const auto& c : cases) {
        const ALEWord w = WordParser::make_word(c.type, c.addr);

        uint8_t ref[49];
        reference_TxFEC(raw24_of(w), ref);

        ALEWord decoded;
        Golay::DecodeResult fec;
        uint8_t unanimous = 0xFF;
        const bool ok = ALEDecoder::decode(ref, decoded, fec, &unanimous);

        const bool good = ok
                       && decoded.type == w.type
                       && std::strcmp(decoded.address, w.address) == 0;
        if (!good) {
            ++failures;
            std::fprintf(stderr,
                "DECODE FAIL for %s '%s': ok=%d fec.flag=%d unanimous=%u "
                "got type=%s addr='%s'\n",
                WordParser::word_type_name(c.type), c.addr, ok, fec.flag, unanimous,
                WordParser::word_type_name(decoded.type), decoded.address);
            dump_symbols("pcale", ref);
        } else {
            check(fec.flag == Golay::DECODE_OK, "expected DECODE_OK for clean PCALE word");
            check(unanimous == SYMBOLS_PER_WORD - 1u, "expected all-unanimous votes for clean PCALE word");
        }
    }
    check(failures == 0, "our decoder failed on PCALE-encoded words");
    if (failures == 0)
        std::printf("PASS  test_decoder_accepts_reference  (all PCALE words decoded)\n");
}

// ── Test 3: exhaustive sweep over all 24-bit words ──────────────────────────────
//
// Walk every preamble (0-7) × a broad payload sample.  This catches any
// data-dependent bit-position bug that a handful of fixed addresses might miss.

static void test_exhaustive_sweep()
{
    uint32_t enc_mismatch = 0;
    uint32_t dec_mismatch = 0;
    uint32_t tested = 0;

    for (uint32_t pre = 0; pre < 8; ++pre) {
        // Sample the 21-bit payload space on a stride that still exercises every
        // bit position without enumerating all 2 million combinations.
        for (uint32_t payload = 0; payload < 0x200000u; payload += 0x2F1u) {
            const uint32_t raw24 = (pre << 21) | payload;
            ++tested;

            // Reference symbols.
            uint8_t ref[49];
            reference_TxFEC(raw24, ref);

            // Our symbols via the FEC + interleave + pack pipeline.
            const GolayCoded coded = ALEFECCodec::encode_word(raw24);
            const uint64_t   tx49  = ALEFECCodec::interleave_word(coded);
            const SymbolFrame ours = ALEEncoder::encode_tx49(tx49);

            if (std::memcmp(ours.data(), ref, 49) != 0) {
                if (enc_mismatch < 3) {
                    std::fprintf(stderr, "SWEEP ENC MISMATCH raw24=0x%06X\n", raw24);
                    dump_symbols("ours", ours.data());
                    dump_symbols("pcale", ref);
                }
                ++enc_mismatch;
            }

            // Decode the reference symbols and compare the recovered 24-bit word.
            uint64_t stream_tx49 = 0;
            uint8_t stream[49 * 3];
            for (uint32_t k = 0; k < 49; ++k) {
                stream[3*k+0] = (ref[k] >> 2) & 1u;
                stream[3*k+1] = (ref[k] >> 1) & 1u;
                stream[3*k+2] =  ref[k]       & 1u;
            }
            for (uint32_t i = 0; i < 49; ++i) {
                const uint32_t sum = stream[i] + stream[i+49] + stream[i+98];
                if (sum >= 2u) stream_tx49 |= (1ULL << i);
            }
            Golay::DecodeResult fec;
            const uint32_t word24 = ALEFECCodec::deinterleave_word(stream_tx49, fec);
            if (word24 != raw24 || fec.flag != Golay::DECODE_OK) {
                if (dec_mismatch < 3)
                    std::fprintf(stderr,
                        "SWEEP DEC MISMATCH raw24=0x%06X got=0x%06X fec=%d\n",
                        raw24, word24, fec.flag);
                ++dec_mismatch;
            }
        }
    }

    std::fprintf(stderr, "  swept %u words: enc_mismatch=%u dec_mismatch=%u\n",
                 tested, enc_mismatch, dec_mismatch);
    check(enc_mismatch == 0, "encoder diverges from PCALE on some 24-bit words");
    check(dec_mismatch == 0, "decoder fails to recover some PCALE-encoded words");
    if (enc_mismatch == 0 && dec_mismatch == 0)
        std::printf("PASS  test_exhaustive_sweep  (%u words, codec is bit-compatible)\n", tested);
}

int main()
{
    test_encoder_matches_reference();
    test_decoder_accepts_reference();
    test_exhaustive_sweep();

    if (g_failures == 0) {
        std::printf("\nAll PCALE interop tests passed — codec is bit-compatible.\n");
        std::printf("=> Any PCALE->ale_cli decode failure is PHYSICAL "
                    "(audio rate / timing / sync), not bit-level.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d PCALE interop check(s) FAILED — bit-level incompatibility found.\n",
                 g_failures);
    return 1;
}
