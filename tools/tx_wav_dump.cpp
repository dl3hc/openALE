/**
 * \file tools/tx_wav_dump.cpp
 * \brief Diagnostic: dump TX audio to WAV so the tone generator can be inspected
 *        independently of the virtual audio cable and the waterfall app.
 *
 * Produces (in the current directory):
 *   tone2500_8k.wav        pure 2500 Hz, 8 kHz, straight from ToneGenerator
 *   tone2500_48k.wav       same tone after the REAL Resampler(8000→48000)
 *   sweep_2500_48k_a25.wav   pure 2500 Hz @48k, amplitude 0.25  (−12 dBFS)
 *   sweep_2500_48k_a125.wav  pure 2500 Hz @48k, amplitude 0.125 (−18 dBFS)
 *   sweep_2500_48k_a0625.wav pure 2500 Hz @48k, amplitude 0.0625(−24 dBFS)
 *   alepattern_48k.wav       all 8 tones cycling (mimics on-air waterfall look)
 *
 * How to use the results:
 *   1) Open tone2500_8k.wav / tone2500_48k.wav in Audacity → Analyze → Plot
 *      Spectrum (Hann window). A clean single peak at 2500 Hz with everything
 *      else far down (< −80 dB) PROVES the generator is clean — regardless of
 *      what any cable or waterfall shows.
 *   2) Play the three sweep_*.wav through the cable and watch the reference
 *      waterfall:
 *        - cable level identical for all three → cable/normalisation drives to
 *          full scale (NOT the generator);
 *        - level drops ~6 dB per step AND spur/tone ratio constant → spurs are
 *          the inherent ALE spectrum (no defect);
 *        - level drops AND spurs drop faster → clipping somewhere downstream.
 */

#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include "FSK/tx_bandpass.h"
#include "App/resampler.h"
#include "Protocol/Control/ale_state_machine.h"
#include "Protocol/Control/ale_timing.h"
#include "Modem/ale2g_modem.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace ale;

// ── Minimal 16-bit mono PCM WAV writer ───────────────────────────────────────
static bool write_wav_mono16(const std::string& path,
                             const std::vector<int16_t>& s, uint32_t sr) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "  cannot open %s\n", path.c_str()); return false; }

    const uint32_t data_bytes = static_cast<uint32_t>(s.size()) * 2u;
    const uint32_t byte_rate  = sr * 2u;
    auto w32 = [&](uint32_t v){ char b[4]={char(v),char(v>>8),char(v>>16),char(v>>24)}; f.write(b,4); };
    auto w16 = [&](uint16_t v){ char b[2]={char(v),char(v>>8)}; f.write(b,2); };

    f.write("RIFF",4); w32(36u + data_bytes); f.write("WAVE",4);
    f.write("fmt ",4); w32(16u); w16(1); w16(1);
    w32(sr); w32(byte_rate); w16(2); w16(16);
    f.write("data",4); w32(data_bytes);
    f.write(reinterpret_cast<const char*>(s.data()),
            static_cast<std::streamsize>(data_bytes));
    return true;
}

// ── Hann-windowed single-bin power over a length-L segment starting at `off`.
static double seg_power(const std::vector<int16_t>& x, size_t off, size_t L,
                        double f_hz, uint32_t sr) {
    const double PI = 3.14159265358979323846;
    const double w  = 2.0 * PI * f_hz / static_cast<double>(sr);
    const double cw = std::cos(w), sw = std::sin(w);
    double cn = 1.0, sn = 0.0, re = 0.0, im = 0.0;
    const double denom = (L > 1) ? static_cast<double>(L - 1) : 1.0;
    for (size_t n = 0; n < L; ++n) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * PI * n / denom);
        const double xn  = x[off + n] * win;
        re += xn * cn; im -= xn * sn;
        const double nc = cn * cw - sn * sw, ns = cn * sw + sn * cw;
        cn = nc; sn = ns;
    }
    return re * re + im * im;
}

// Welch-averaged power at f_hz (Hann, 50%% overlap) — matches Audacity Plot Spectrum.
static double welch_power(const std::vector<int16_t>& x, double f_hz, uint32_t sr) {
    constexpr size_t L = 4096;          // ~11.7 Hz/bin at 48 kHz
    if (x.size() < L) return seg_power(x, 0, x.size(), f_hz, sr);
    double acc = 0.0; size_t cnt = 0;
    for (size_t off = 0; off + L <= x.size(); off += L / 2) {
        acc += seg_power(x, off, L, f_hz, sr); ++cnt;
    }
    return cnt ? acc / cnt : 0.0;
}

// Report 100/300/500 Hz levels relative to the strongest ALE tone (dB).
static void report_lowband(const char* name, const std::vector<int16_t>& s, uint32_t sr) {
    double ref = 0.0;
    for (uint32_t f : TONE_FREQS_HZ) {
        const double p = welch_power(s, static_cast<double>(f), sr);
        if (p > ref) ref = p;
    }
    auto dbc = [&](double f){
        const double p = welch_power(s, f, sr);
        return (ref > 0.0) ? 10.0 * std::log10(p / ref) : -200.0;
    };
    std::printf("  %-26s  vs strongest tone:  100Hz=%6.1f  300Hz=%6.1f  500Hz=%6.1f dB\n",
                name, dbc(100.0), dbc(300.0), dbc(500.0));
}

// ── peak / RMS report in dBFS ────────────────────────────────────────────────
static void report_level(const char* name, const std::vector<int16_t>& s) {
    int    peak = 0;
    double sumsq = 0.0;
    for (int16_t v : s) {
        const int a = std::abs(static_cast<int>(v));
        if (a > peak) peak = a;
        sumsq += static_cast<double>(v) * v;
    }
    const double rms     = std::sqrt(sumsq / (s.empty() ? 1 : s.size()));
    const double peak_db = (peak > 0) ? 20.0 * std::log10(peak / 32768.0) : -200.0;
    const double rms_db  = (rms  > 0) ? 20.0 * std::log10(rms  / 32768.0) : -200.0;
    std::printf("  %-26s  n=%-7zu  peak=%5d (%6.2f dBFS)  rms=%7.1f (%6.2f dBFS)\n",
                name, s.size(), peak, peak_db, rms, rms_db);
}

// ── helpers ──────────────────────────────────────────────────────────────────
static std::vector<int16_t> pure_tone_8k(double freq_hz, double seconds, float amp) {
    // SYMBOL_TO_FREQ maps a symbol value → frequency; find the symbol for freq_hz.
    uint8_t sym = 0;
    for (uint8_t s = 0; s < NUM_TONES; ++s)
        if (SYMBOL_TO_FREQ[s] == static_cast<uint32_t>(freq_hz)) { sym = s; break; }

    const uint32_t n = static_cast<uint32_t>(seconds * SAMPLE_RATE_HZ);
    std::vector<int16_t> out(n);
    ToneGenerator gen;
    gen.generate_tone(sym, n, out.data(), amp);
    return out;
}

static std::vector<int16_t> to_48k(const std::vector<int16_t>& in8k) {
    Resampler rs(SAMPLE_RATE_HZ, 48000);
    std::vector<int16_t> out;
    out.reserve(in8k.size() * 6 + 64);
    rs.process(in8k.data(), in8k.size(), out);
    return out;
}

// Apply the TX band-pass (8 kHz) — the exact filter used in the live TX path.
static std::vector<int16_t> bandpass_8k(const std::vector<int16_t>& in8k) {
    TxBandpass bp;
    std::vector<int16_t> out;
    out.reserve(in8k.size());
    bp.process(in8k.data(), in8k.size(), out);
    return out;
}

// Real Frame-c symbol stream (call SAMUEL from JOE) — same TX fixture as the
// roundtrip test: FEC-coded, interleaved, pseudo-random — unlike the periodic
// 0–7 cycle, so its spectrum matches genuine on-air ALE data.
static std::vector<uint8_t> framec_symbols() {
    std::vector<uint8_t> all;
    ALEStateMachine        sm;
    ALE2GModem::Modulator  modem;
    sm.set_transmit_callback([&](const ALEWord& w){ modem.enqueue_word(w); });
    sm.set_self_address("JOE");
    sm.set_target_scan_channels(0);
    sm.initiate_call("SAMUEL");

    const uint32_t timeout_ms = ALETimingConstants::Twt_ms + ALETimingConstants::Tt_ms
                              + 6u * ALETimingConstants::Trw_ms;
    uint8_t syms[SYMBOLS_PER_WORD];
    for (uint32_t t = 0; t < timeout_ms; ++t) {
        sm.update(t);
        while (modem.pull_symbol_frame(syms)) {
            all.insert(all.end(), syms, syms + SYMBOLS_PER_WORD);
            sm.on_word_complete();
        }
        if (sm.get_calling_phase() == CallingPhase::LISTENING) break;
    }
    return all;
}

static std::vector<int16_t> symbols_to_8k(const std::vector<uint8_t>& syms) {
    std::vector<int16_t> out(syms.size() * SAMPLES_PER_SYMBOL);
    ToneGenerator gen;
    gen.generate_symbols(syms.data(), static_cast<uint32_t>(syms.size()),
                         out.data(), TX_AMPLITUDE);
    return out;
}

int main() {
    std::printf("TX WAV dump — generator/resampler output for offline inspection\n");
    std::printf("================================================================\n");

    // 1) pure 2500 Hz tone, generator direct @8k and through real resampler @48k
    auto t8 = pure_tone_8k(2500.0, 2.0, TX_AMPLITUDE);
    auto t48 = to_48k(t8);
    write_wav_mono16("tone2500_8k.wav",  t8,  SAMPLE_RATE_HZ);
    write_wav_mono16("tone2500_48k.wav", t48, 48000);
    report_level("tone2500_8k.wav",  t8);
    report_level("tone2500_48k.wav", t48);
    report_lowband("tone2500_48k.wav (pure)", t48, 48000);

    // 2) level sweep @48k: same tone at 0.25 / 0.125 / 0.0625
    struct { const char* name; float amp; } sweep[] = {
        { "sweep_2500_48k_a25.wav",   0.25f   },
        { "sweep_2500_48k_a125.wav",  0.125f  },
        { "sweep_2500_48k_a0625.wav", 0.0625f },
    };
    for (auto& sw : sweep) {
        auto w = to_48k(pure_tone_8k(2500.0, 2.0, sw.amp));
        write_wav_mono16(sw.name, w, 48000);
        report_level(sw.name, w);
    }

    // 3) all 8 tones cycling (mimics the on-air waterfall) @48k
    {
        std::vector<uint8_t> syms(8 * 50);
        for (size_t i = 0; i < syms.size(); ++i)
            syms[i] = static_cast<uint8_t>(i % NUM_TONES);
        std::vector<int16_t> p8(syms.size() * SAMPLES_PER_SYMBOL);
        ToneGenerator gen;
        gen.generate_symbols(syms.data(), static_cast<uint32_t>(syms.size()),
                             p8.data(), TX_AMPLITUDE);
        auto p48 = to_48k(p8);
        write_wav_mono16("alepattern_48k.wav", p48, 48000);
        report_level("alepattern_48k.wav", p48);
        report_lowband("alepattern_48k.wav (keyed)", p48, 48000);

        // Same pattern AFTER the TX band-pass (before/after comparison).
        auto p48_bp = to_48k(bandpass_8k(p8));
        write_wav_mono16("alepattern_bp_48k.wav", p48_bp, 48000);
        report_level("alepattern_bp_48k.wav", p48_bp);
        report_lowband("alepattern_bp_48k.wav (BP)", p48_bp, 48000);
    }

    // 4) realistic Frame-c (real FEC-coded symbols), raw vs band-passed @48k
    {
        auto syms = framec_symbols();
        std::printf("  (Frame-c: %zu symbols)\n", syms.size());
        if (!syms.empty()) {
            auto f8 = symbols_to_8k(syms);
            auto f48    = to_48k(f8);
            auto f48_bp = to_48k(bandpass_8k(f8));
            write_wav_mono16("framec_48k.wav",    f48,    48000);
            write_wav_mono16("framec_bp_48k.wav", f48_bp, 48000);
            report_lowband("framec_48k.wav (raw)", f48,    48000);
            report_lowband("framec_bp_48k.wav (BP)", f48_bp, 48000);
        }
    }

    std::printf("\nDone. Inspect tone2500_*.wav in Audacity (Plot Spectrum, Hann).\n");
    std::printf("Compare framec_48k.wav vs framec_bp_48k.wav (raw vs band-passed).\n");
    std::printf("Play sweep_*.wav through the cable to test level/clipping.\n");
    return 0;
}
