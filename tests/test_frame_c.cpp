/**
 * \file test_frame_c.cpp
 * \brief Frame c: 1-channel nonscan, 2-word addressing, individual call
 *
 * MIL-STD-188-141B Figure A-14c:
 *   leading:    TO:SAM  DATA:UEL  TO:SAM  DATA:UEL
 *   conclusion: TIS:JOE
 *
 * Timing from ale_timing.h:
 *   LBT    Twt_ms  =  784 ms  (ALE-only listen-before-TX)
 *   Tuning Tt_ms   = 1045 ms  (blind tune)
 *   Per word       =  392 ms  (Trw = 3 × Tw)
 *   Total frame c  = 784 + 1045 + 5×392 = 3789 ms
 *
 * Build:
 *   g++ -std=c++17 -I include \
 *       src/FEC/word_interleaver.cpp \
 *       src/FEC/ale_fec_codec.cpp src/Word/ale_word.cpp src/Word/address_encoder.cpp \
 *       src/Word/ale_sequence.cpp src/FSK/tone_generator.cpp src/Modem/ale2g_modem.cpp \
 *       src/Protocol/Control/ale_state_machine.cpp \
 *       test_frame_c.cpp -o test_frame_c && ./test_frame_c
 *
 * Output:
 *   frame_c.wav  -> directly openable in VLC / Sorcerer / ION2G
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include "Modem/ale2g_modem.h"
#include "FSK/tone_generator.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace ale;

namespace {

void write_u16_le(std::ofstream& f, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF)
    };
    f.write(bytes, sizeof(bytes));
}

void write_u32_le(std::ofstream& f, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF)
    };
    f.write(bytes, sizeof(bytes));
}

bool write_wav_mono16(const char* path,
                      const std::vector<int16_t>& samples,
                      uint32_t sample_rate = 8000)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    constexpr uint16_t num_channels = 1;
    constexpr uint16_t bits_per_sample = 16;
    constexpr uint16_t audio_format = 1; // PCM

    const uint16_t block_align = num_channels * (bits_per_sample / 8);
    const uint32_t byte_rate = sample_rate * block_align;
    const uint32_t data_size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;

    // RIFF header
    f.write("RIFF", 4);
    write_u32_le(f, riff_size);
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    write_u32_le(f, 16);
    write_u16_le(f, audio_format);
    write_u16_le(f, num_channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, bits_per_sample);

    // data chunk
    f.write("data", 4);
    write_u32_le(f, data_size);

    if (!samples.empty()) {
        f.write(reinterpret_cast<const char*>(samples.data()), data_size);
    }

    return static_cast<bool>(f);
}

} // namespace

int main() {
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Frame c — 1-ch nonscan, 2-word addr, individual call\n");
    std::printf("  Calling: SAMUEL   Self: JOE\n");
    std::printf("  LBT:    %u ms\n", ALETimingConstants::Twt_ms);
    std::printf("  Tuning: %u ms\n", ALETimingConstants::Tt_ms);
    std::printf("  Per Trw:%u ms  (× 5 words = %u ms)\n",
        ALETimingConstants::Trw_ms,
        ALETimingConstants::Trw_ms * 5);
    std::printf("═══════════════════════════════════════════════════════\n\n");

    // ── PCM accumulator ──────────────────────────────────────────
    std::vector<int16_t> pcm;

    // ── Build system ─────────────────────────────────────────────
    ALEStateMachine sm;
    ALE2GModem::Modulator      modem;
    ToneGenerator   gen;

    sm.set_transmit_callback([&](const ALEWord& w) {
        std::printf("  TX  %-4s  %c%c%c\n",
            WordParser::word_type_name(w.type),
            w.address[0], w.address[1], w.address[2]);
        modem.enqueue_word(w);
    });

    // ── Configure ────────────────────────────────────────────────
    sm.set_self_address("JOE");
    sm.set_target_scan_channels(0);   // 0 = no scanning → Frame c (1-ch nonscan)

    // ── Trigger ──────────────────────────────────────────────────
    std::printf("  initiate_call(\"SAMUEL\")\n\n");
    sm.initiate_call("SAMUEL");

    // ── Drive ────────────────────────────────────────────────────
    // Tick at 1 ms — fine enough to not skip any timing threshold.
    //
    // Expected timeline (protocol time):
    //   t=0      SM enters CALLING → LBT starts
    //   t=784    LBT done (Twt_ms) → TUNING starts
    //   t=1829   Tuning done (Tt_ms) → LEADING_CALL; the COMPLETE TX
    //            sequence (4 leading + 1 conclusion words) is enqueued
    //            back-to-back.  The pull loop below renders all 5 symbol
    //            frames contiguously and fires on_word_complete() per
    //            frame → phases advance to LISTENING.
    //   On air each word occupies exactly 392 ms (49 symbols × 8 ms);
    //   the Trw grid is defined by the sample stream, not by wall time.

    const uint32_t tick_ms = 1;
    const uint32_t timeout_ms =
        ALETimingConstants::Twt_ms          // LBT
        + ALETimingConstants::Tt_ms         // Tuning
        + 5 * ALETimingConstants::Trw_ms    // 5 words
        + ALETimingConstants::Trw_ms;       // margin

    uint32_t t = 0;
    while (t < timeout_ms) {
        sm.update(t);
        uint8_t syms[SYMBOLS_PER_WORD];
        while (modem.pull_symbol_frame(syms)) {
            const size_t off = pcm.size();
            pcm.resize(off + SYMBOLS_PER_WORD * FFT_SIZE);
            gen.generate_symbols(syms, SYMBOLS_PER_WORD, pcm.data() + off, 0.7f);
            sm.on_word_complete();
        }
        t += tick_ms;
        if (sm.get_calling_phase() == CallingPhase::LISTENING)
            break;
    }

    // ── Results ──────────────────────────────────────────────────
    const bool ok = sm.get_calling_phase() == CallingPhase::LISTENING;
    std::printf("\n  Simulated time : %u ms\n", t);
    std::printf("  Phase reached  : %s\n", ok ? "LISTENING  ✓" : "NOT LISTENING  ✗");
    std::printf("  PCM samples    : %zu  (%.0f ms @ 8 kHz)\n",
        pcm.size(), pcm.size() / 8.0);

    if (pcm.empty()) {
        std::fprintf(stderr, "ERROR: No PCM samples generated by modem\n");
        return 1;
    }

    // ── Write WAV ────────────────────────────────────────────────
    const char* wav_path = "frame_c.wav";
    if (write_wav_mono16(wav_path, pcm, 8000)) {
        std::printf("  WAV written    : %s\n", wav_path);
    } else {
        std::fprintf(stderr, "ERROR: Failed to write %s\n", wav_path);
        return 1;
    }

    // Optional: raw PCM for debugging
    {
        const char* raw_path = "frame_c.raw";
        std::ofstream raw(raw_path, std::ios::binary);
        if (raw) {
            raw.write(reinterpret_cast<const char*>(pcm.data()),
                      pcm.size() * sizeof(int16_t));
            std::printf("  RAW written    : %s\n", raw_path);
        }
    }

    std::printf("\n  Next step:\n");
    std::printf("    open frame_c.wav in VLC, Sorcerer or ION2G\n");
    std::printf("    expected: TO:SAMUEL  TIS:JOE  INDIVIDUAL CALL\n");
    std::printf("═══════════════════════════════════════════════════════\n");

    return ok ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Separate FEC path verification (run before main test)
// Verifies the complete spec-conformant encoding chain for one word:
//   24-bit word → Golay(24,12) → interleave → 49-bit (48 data + S49=0)
// ─────────────────────────────────────────────────────────────────────────────