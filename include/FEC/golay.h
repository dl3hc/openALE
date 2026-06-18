/**
 * \file golay.h
 * \brief Extended Golay (24,12) FEC encoder/decoder
 * 
 * Table-driven implementation of Extended Golay error-correcting code.
 * Corrects up to 3 bit errors per 24-bit codeword.
 * 
 * Specification: MIL-STD-188-141B
 *  - Code: Extended Golay (24,12)
 *  - Information bits: 12
 *  - Parity bits: 12
 *  - Error correction capability: 3 bits
 */

#pragma once

#include <cstdint>
#include <array>
#include <mutex>

namespace ale {

/**
 * Golay correction mode (MIL-STD-188-141B A.5.2.6.3).
 *
 * The (24,12) Extended Golay code has minimum distance 8.  The standard lets a
 * manufacturer trade correction against detection in one of four modes, written
 * "n/m" = "up to n errors detected AND corrected, up to m errors detected but
 * NOT correctable" (n + m = 7).  The enum value IS n (the correction power):
 *
 *   Mode3_4 (n=3): correct ≤3, detect ≤4   — maximum power (default)
 *   Mode2_5 (n=2): correct ≤2, detect ≤5
 *   Mode1_6 (n=1): correct ≤1, detect ≤6
 *   Mode0_7 (n=0): correct 0,  detect ≤7   — detection only
 *
 * An error whose weight is within the code's range but exceeds the selected
 * mode's correction power is reported as DECODE_DETECTED (not corrected).
 * Per A.5.2.6.3 the mode "should be chosen by the manufacturer to optimize
 * performance" and may be adjusted automatically under varying conditions.
 */
enum class GolayMode : uint8_t {
    Mode0_7 = 0,
    Mode1_6 = 1,
    Mode2_5 = 2,
    Mode3_4 = 3,
};

class Golay {
public:
    /** Generator polynomial of the extended Golay (24,12) code (MIL-STD-188-141B A.5.2.2.2).
     *  0xAE3 = x^11+x^9+x^7+x^6+x^5+x+1 (binary 101011100011).
     *  Parity for the MSB basis vector (info=0x800) equals this constant. */
    static constexpr uint16_t GENERATOR_POLYNOMIAL = 0xAE3;

    /** Size of the compile-time encoder lookup table (2^12 = 4096 12-bit info words). */
    static constexpr uint32_t ENCODE_TABLE_SIZE = 4096;

    /**
     * Encode 12-bit information word to 24-bit codeword
     * Codeword = [information (12 bits) | parity (12 bits)]
     *
     * \param info 12-bit information word
     * \return 24-bit encoded codeword
     */
    static uint32_t encode(uint16_t info);
    
    /**
     * Decode result flags (MIL-STD-188-141B A.5.2.2.2.2 / A.5.2.6)
     *
     * Returned by decode() to distinguish four outcomes:
     *   DECODE_OK            - No errors; codeword was valid.
     *   DECODE_CORRECTED     - 1-3 bit errors detected and corrected.
     *   DECODE_DETECTED      - Syndrome non-zero but error pattern weight > 3:
     *                          error detected but NOT correctable.
     *                          output contains the raw (uncorrected) info field.
     *   DECODE_UNCORRECTABLE - Legacy alias for DECODE_DETECTED (0xFF kept for
     *                          backward compatibility).
     *
     * The errors_corrected field is valid (0-3) only when flag == DECODE_CORRECTED.
     */
    struct DecodeResult {
        uint8_t flag;           ///< One of the DECODE_* constants below
        uint8_t errors_corrected; ///< Number of bits corrected (0 unless DECODE_CORRECTED)
    };

    static constexpr uint8_t DECODE_OK            = 0x00; ///< No errors
    static constexpr uint8_t DECODE_CORRECTED      = 0x01; ///< 1-3 errors corrected
    static constexpr uint8_t DECODE_DETECTED       = 0xFE; ///< Error detected, not correctable
    static constexpr uint8_t DECODE_UNCORRECTABLE  = 0xFF; ///< Alias (backward compat.)

    /**
     * Decode and correct 24-bit codeword (MIL-STD-188-141B A.5.2.2.2.2)
     *
     * Corrects up to 3 random bit errors.  When the syndrome is non-zero but
     * no matching error pattern exists (weight > 3), the error is *detected*
     * but not corrected and DECODE_DETECTED is returned — distinct from a
     * correctable error per the standard's flag semantics (A.5.2.6).
     *
     * \param codeword 24-bit received codeword
     * \param output   [out] 12-bit decoded information word
     *                 Corrected value on DECODE_CORRECTED;
     *                 raw (uncorrected) info field on DECODE_DETECTED.
     * \param mode     Correction power (A.5.2.6.3).  Errors heavier than the
     *                 mode's power are detected (DECODE_DETECTED) but not
     *                 corrected.  Defaults to Mode3_4 (full correction).
     * \return         DecodeResult with flag and errors_corrected count
     */
    static DecodeResult decode(uint32_t codeword, uint16_t& output,
                               GolayMode mode = GolayMode::Mode3_4);
    
    /**
     * Extract information bits from codeword (no error correction)
     * 
     * \param codeword 24-bit codeword
     * \return 12-bit information field
     */
    static uint16_t extract_info(uint32_t codeword);
    
    /**
     * Extract parity bits from codeword
     * 
     * \param codeword 24-bit codeword
     * \return 12-bit parity field
     */
    static uint16_t extract_parity(uint32_t codeword);
    
private:
    // Syndrome lookup table for error correction
    // Maps syndrome (12 bits) to error pattern (24 bits)
    // Computed statically at compile-time or load-time
    static constexpr uint32_t SYNDROME_TABLE_SIZE = 1 << 12;
    static std::array<uint32_t, SYNDROME_TABLE_SIZE> syndrome_table;
    
    /**
     * Compute syndrome for error detection
     * \param codeword 24-bit codeword
     * \return 12-bit syndrome
     */
    static uint16_t compute_syndrome(uint32_t codeword);
    
    /**
     * Compute parity of bit field (count set bits mod 2)
     */
    static uint8_t compute_parity(uint32_t value);
    
    /**
     * Initialize syndrome table (invoked once via std::call_once)
     */
    static void init_syndrome_table();

    static std::once_flag syndrome_init_flag;
    
};

} // namespace ale
