/**
 * \file tests/test_resampler_path.cpp
 * \brief Verifies the realtime audio chain offline:
 *        TX symbols → 8 kHz PCM → Resampler 8k→48k → Resampler 48k→8k
 *        → ALERxPipeline decode.
 *
 * This is exactly the signal path between two ale_cli instances over a
 * 48 kHz virtual cable (minus the cable itself).  The word boundary is
 * swept over all sub-symbol phase offsets to prove that decoding does not
 * depend on the alignment between the incoming signal and the local
 * decode-attempt grid.
 *
 * Pass criterion: all 5 Frame-c words decode at every tested offset.
 */

#include "Protocol/Control/ale_state_machine.h"
#include "Modem/ale2g_modem.h"
#include "App/resampler.h"
#include "FSK/tone_generator.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ale;

int main()
{
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Resampler-path test: 8k → 48k → 8k → ALERxPipeline\n");
    std::printf("═══════════════════════════════════════════════════════\n\n");

    // ── TX side: Frame c (TO:SAM DATA:UEL TO:SAM DATA:UEL TIS:JOE) ─────────
    ALEStateMachine sm;
    ALE2GModem::Modulator modem;
    ToneGenerator   gen;

    sm.set_transmit_callback([&](const ALEWord& w) { modem.enqueue_word(w); });
    sm.set_self_address("JOE");
    sm.set_target_scan_channels(0);
    sm.initiate_call("SAMUEL");

    std::vector<int16_t> pcm8;
    const uint32_t t_tx = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms;
    sm.update(ALETimingConstants::Twt_ms);
    sm.update(t_tx);
    uint8_t syms[SYMBOLS_PER_WORD];
    while (modem.pull_symbol_frame(syms)) {
        const size_t off = pcm8.size();
        pcm8.resize(off + SYMBOLS_PER_WORD * FFT_SIZE);
        gen.generate_symbols(syms, SYMBOLS_PER_WORD, pcm8.data() + off, TX_AMPLITUDE);
        sm.on_word_complete();
    }
    const size_t n_words = pcm8.size() / (SYMBOLS_PER_WORD * FFT_SIZE);
    std::printf("  TX words rendered: %zu (%zu samples @ 8 kHz)\n\n",
                n_words, pcm8.size());

    // ── Sweep sub-symbol phase offsets (in 8 kHz samples of leading silence)
    int offsets_pass = 0;
    const int test_offsets[] = { 0, 5, 13, 16, 23, 32, 41, 48, 57, 63 };

    for (int off8 : test_offsets) {
        // Leading + trailing silence around the signal, offset shifts phase.
        std::vector<int16_t> tx8(static_cast<size_t>(off8), 0);
        tx8.insert(tx8.end(), pcm8.begin(), pcm8.end());
        tx8.insert(tx8.end(), 8000, 0);

        // Device path: 8k → 48k (TX side) → 48k → 8k (RX side).
        Resampler up(8000, 48000), down(48000, 8000);
        std::vector<int16_t> dev48, rx8;
        up.process(tx8.data(), tx8.size(), dev48);
        down.process(dev48.data(), dev48.size(), rx8);

        // RX pipeline decode.
        ALE2GModem::Demodulator pipe;
        int got = 0;
        pipe.set_word_callback([&](const ALEWord& w) {
            (void)w;
            ++got;
        });
        pipe.push_samples(rx8.data(), static_cast<uint32_t>(rx8.size()));

        const bool ok = (got == static_cast<int>(n_words));
        if (ok) ++offsets_pass;
        std::printf("  offset %2d samples: decoded %d/%zu words  %s\n",
                    off8, got, n_words, ok ? "OK" : "FAIL");
    }

    const int total = static_cast<int>(sizeof(test_offsets) / sizeof(test_offsets[0]));
    std::printf("\n  %d/%d offsets fully decoded\n", offsets_pass, total);
    std::printf("═══════════════════════════════════════════════════════\n");
    return (offsets_pass == total) ? 0 : 1;
}
