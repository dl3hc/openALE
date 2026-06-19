/**
 * \example usage.cpp
 * \brief Example usage of ALE 8-FSK modem core
 *
 * Demonstrates:
 *  1. Tone generation
 *  2. Golay encoding/decoding
 *  3. Live demodulation via ALE2GModem::Demodulator (Goertzel-based)
 *
 * Note: FFTDemodulator and SymbolDecoder have been removed.
 * Use ALE2GModem::Demodulator for demodulation.
 */

#include "FSK/ale_waveform.h"
#include "FSK/tone_generator.h"
#include "FEC/golay.h"

#include <iostream>
#include <vector>

using namespace ale;

// ============================================================================
// Example 1: Generate a single 8-FSK symbol
// ============================================================================

void example_1_simple_symbol() {
    std::cout << "\n=== Example 1: Generate Single Symbol ===\n\n";

    ToneGenerator gen;
    std::vector<int16_t> audio(64);

    gen.generate_tone(3, 64, audio.data());

    std::cout << "Generated 64 samples of symbol 3 (tone at "
              << SYMBOL_TO_FREQ[3] << " Hz)\n";
    std::cout << "(Demodulation: feed audio to ALE2GModem::Demodulator::push_samples())\n";
}

// ============================================================================
// Example 2: Modulate a sequence of symbols
// ============================================================================

void example_2_symbol_sequence() {
    std::cout << "\n=== Example 2: Modulate Symbol Sequence ===\n\n";

    uint8_t symbols[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    ToneGenerator gen;
    std::vector<int16_t> audio(8 * 64);

    uint32_t samples = gen.generate_symbols(symbols, 8, audio.data());

    std::cout << "Generated " << samples << " audio samples ";
    std::cout << "(8 symbols × 64 samples/symbol)\n";

    std::cout << "Tone mapping:\n";
    for (int i = 0; i < 8; ++i)
        std::cout << "  Symbol " << i << " → " << SYMBOL_TO_FREQ[i] << " Hz\n";
}

// ============================================================================
// Example 3: Golay FEC Encoding & Decoding
// ============================================================================

void example_3_golay_fec() {
    std::cout << "\n=== Example 3: Golay Error Correction ===\n\n";
    
    // Information word (12 bits)
    uint16_t info = 0xABC;  // 10101011110 in binary
    
    // Encode to 24-bit codeword
    uint32_t codeword = Golay::encode(info);
    std::cout << "Info word: 0x" << std::hex << info << std::dec << "\n";
    std::cout << "Codeword: 0x" << std::hex << codeword << std::dec << "\n";
    
    // Simulate single-bit error
    uint32_t corrupted = codeword ^ (1U << 5);
    std::cout << "\nCorrupted (1 bit flipped): 0x" << std::hex << corrupted << std::dec << "\n";
    
    // Decode and correct
    uint16_t decoded = 0;
    uint8_t errors = Golay::decode(corrupted, decoded);
    
    std::cout << "Decoded: 0x" << std::hex << decoded << std::dec << "\n";
    std::cout << "Errors corrected: " << (int)errors << "\n";
    
    if (decoded == info) {
        std::cout << "✓ Successfully recovered original data\n";
    } else {
        std::cout << "✗ Failed to recover data\n";
    }
}

// ============================================================================
// Example 4: Majority Voting (ALE-2G 3× redundancy)
// ============================================================================

void example_4_majority_voting() {
    std::cout << "\n=== Example 4: Majority Voting ===\n\n";

    // ALE-2G triple redundancy: each bit is sent 3 times (positions b, b+49, b+98)
    // The receiver takes the majority across the three copies.
    uint8_t bit_copies[3] = {1, 1, 0};  // 2 out of 3 are 1; one corrupted

    uint8_t sum = bit_copies[0] + bit_copies[1] + bit_copies[2];
    uint8_t corrected = (sum >= 2) ? 1 : 0;

    std::cout << "Three copies of bit: " << (int)bit_copies[0]
              << " " << (int)bit_copies[1]
              << " " << (int)bit_copies[2] << "\n";
    std::cout << "Majority vote result: " << (int)corrected << "\n";
    std::cout << "(Majority is 1 — correct bit recovered despite one error)\n";
}

// ============================================================================
// Example 5: Modulation + Golay FEC
// ============================================================================

void example_5_complete_pipeline() {
    std::cout << "\n=== Example 5: Modulation + Golay FEC ===\n\n";

    uint8_t message[5] = {2, 5, 0, 7, 3};

    std::cout << "Step 1: Message symbols: ";
    for (auto sym : message) std::cout << (int)sym << " ";
    std::cout << "\n";

    std::cout << "\nStep 2: Golay FEC Encoding\n";
    uint16_t data = (message[0] << 3) | (message[1] >> 1);
    uint32_t codeword = Golay::encode(data);
    std::cout << "  Data: 0x" << std::hex << data << std::dec << "\n";
    std::cout << "  Codeword: 0x" << std::hex << codeword << std::dec << "\n";

    std::cout << "\nStep 3: FSK Modulation\n";
    ToneGenerator gen;
    std::vector<int16_t> audio(5 * 64);
    uint32_t samples = gen.generate_symbols(message, 5, audio.data());
    std::cout << "  Generated " << samples << " audio samples\n";
    std::cout << "  (Feed to ALE2GModem::Demodulator::push_samples() for live decode)\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║    ALE 8-FSK Modem Core - Usage Examples                  ║\n";
    std::cout << "║    MIL-STD-188-141B Automatic Link Establishment          ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    example_1_simple_symbol();
    example_2_symbol_sequence();
    example_3_golay_fec();
    example_4_majority_voting();
    example_5_complete_pipeline();
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║    Examples complete!                                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    return 0;
}
