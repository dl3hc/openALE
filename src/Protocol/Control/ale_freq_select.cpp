/**
 * \file ale_freq_select.cpp
 * \brief Enhanced Frequency-Select CMD 'f' encoding/decoding (A.5.6.3.2).
 */

#include "Protocol/Control/ale_freq_select.h"

namespace ale {

uint32_t encode_freq_data_word(uint32_t freq_hz) {
    // Convert Hz → kHz, then split into 5 BCD digits
    // [19:16]=10MHz BCD, [15:12]=1MHz BCD, [11:8]=100kHz BCD, [7:4]=10kHz BCD, [3:0]=1kHz BCD
    // Bit 20 (W4) stays 0 per spec.
    const uint32_t khz = freq_hz / 1000u;
    return ((khz / 10000u) % 10u) << 16
         | ((khz /  1000u) % 10u) << 12
         | ((khz /   100u) % 10u) <<  8
         | ((khz /    10u) % 10u) <<  4
         |  (khz           % 10u);
}

uint32_t decode_freq_data_word(uint32_t raw21) {
    const uint32_t khz = ((raw21 >> 16) & 0xFu) * 10000u
                       + ((raw21 >> 12) & 0xFu) *  1000u
                       + ((raw21 >>  8) & 0xFu) *   100u
                       + ((raw21 >>  4) & 0xFu) *    10u
                       +  (raw21        & 0xFu);
    return khz * 1000u;
}

uint32_t encode_freq_select_cmd_word() {
    // [20:14]='f'=0x66=1100110, [13:8]=control=000000, [7:0]=sub-Hz=0
    return (0x66u << 14);
}

std::vector<ALEWord> build_freq_select_sequence(uint32_t freq_hz) {
    ALEWord cmd{};
    cmd.type        = PreambleType::CMD;
    cmd.raw_payload = encode_freq_select_cmd_word();
    // address[] is informational; cmd_char_code() uses raw_payload for detection
    cmd.address[0]  = 'f'; cmd.address[1] = ' ';
    cmd.address[2]  = ' '; cmd.address[3]  = '\0';
    cmd.valid       = true;

    ALEWord data_word{};
    data_word.type        = PreambleType::DATA;
    data_word.raw_payload = encode_freq_data_word(freq_hz);
    data_word.valid       = true;

    return { cmd, data_word };
}

} // namespace ale
