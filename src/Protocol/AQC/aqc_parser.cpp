/**
 * \file aqc_parser.cpp
 * \brief Implementation of AQC-ALE parser and data element extraction
 */

#include "Protocol/AQC/aqc_protocol.h"
#include <cstring>
#include <algorithm>

namespace ale {
namespace aqc {

static const char* TRAFFIC_CLASS_NAMES[] = {
    "CLEAR_VOICE",
    "DIGITAL_VOICE",
    "HFD_VOICE",
    "RESERVED_3",
    "SECURE_DIGITAL_VOICE",
    "RESERVED_5",
    "RESERVED_6",
    "RESERVED_7",
    "ALE_MSG",
    "PSK_MSG",
    "TONE_39_MSG",
    "HF_EMAIL",
    "KY100_ACTIVE",
    "RESERVED_13",
    "RESERVED_14",
    "RESERVED_15"
};

static const char* TRANSACTION_CODE_NAMES[] = {
    "RESERVED_0",
    "MS_141A",
    "ACK_LAST",
    "NAK_LAST",
    "TERMINATE",
    "OP_ACKNAK",
    "AQC_CMD",
    "RESERVED_7"
};

AQCParser::AQCParser() {}

bool AQCParser::is_aqc_format(const ALEWord& word) {
    // AQC fixed bit = bit15 of the 16-bit AQC word = bit15 of the 21-bit
    // Base-ALE payload. 0 = address word (looks like plain Base-ALE to older
    // rx); 1 = control word (carries the AQC fixed bit).
    return AQCProtocol::payload_has_aqc_fixed_bit(word.raw_payload);
}

bool AQCParser::extract_data_elements(uint32_t payload, DataElements& de) {
    // 21-bit payload, bit mapping per MIL-STD-188-141B AQC spec. NOTE: exact
    // mapping depends on AQC word type/message structure; this is a general
    // layout from common AQC DE field placements in MARS-ALE reference code.
    de.de2 = (payload >> 0) & 0x07;         // Bits 0-2: Slot (0-7)
    de.de3 = static_cast<DE3_TrafficClass>((payload >> 3) & 0x0F);  // Bits 3-6: Traffic class
    de.de4 = (payload >> 7) & 0x1F;         // Bits 7-11: LQA (0-31)
    de.de9 = static_cast<DE9_TransactionCode>((payload >> 12) & 0x07); // Bits 12-14: Transaction
    de.de1 = (payload >> 15) & 0x07;        // Bits 15-17: Reserved
    de.de8 = (payload >> 18) & 0x07;        // Bits 18-20: Orderwire count

    // DE5-7: in additional words / other message types
    de.de5 = 0;
    de.de6 = 0;
    de.de7 = 0;
    
    return true;
}

bool AQCParser::parse_call_probe(const ALEWord* words, size_t count, AQCCallProbe& probe) {
    if (count < 2) {
        return false;  // Need at least TO + TERM words
    }

    // Word 0: TO address (may be AQC-enhanced). Word 1: terminator (FROM).
    if (words[0].type == PreambleType::TO || words[0].type == PreambleType::TWAS) {
        probe.to_address = words[0].address;

        if (is_aqc_format(words[0])) {
            extract_data_elements(words[0].raw_payload, probe.de);
        }
    } else {
        return false;
    }

    if (words[1].type == PreambleType::FROM || words[1].type == PreambleType::TIS) {
        probe.term_address = words[1].address;
    } else {
        return false;
    }

    probe.timestamp_ms = words[0].timestamp_ms;
    
    return true;
}

bool AQCParser::parse_call_handshake(const ALEWord* words, size_t count, AQCCallHandshake& handshake) {
    if (count < 2) {
        return false;
    }

    // Word 0: TO (original caller). Word 1: FROM (responding station).
    // Optional word 2: CMD with DE fields.
    if (words[0].type == PreambleType::TO) {
        handshake.to_address = words[0].address;
    } else {
        return false;
    }

    if (words[1].type == PreambleType::FROM || words[1].type == PreambleType::TIS) {
        handshake.from_address = words[1].address;

        if (is_aqc_format(words[1])) {
            extract_data_elements(words[1].raw_payload, handshake.de);
            handshake.slot_position = handshake.de.de2;
            handshake.ack_this_flag = (handshake.de.de9 == DE9_TransactionCode::ACK_LAST);
        }
    } else {
        return false;
    }

    // CRC validation not yet implemented
    if (count >= 3 && words[2].type == PreambleType::CMD) {
        handshake.crc_status = CRCStatus::NOT_APPLICABLE;  // Placeholder
    }
    
    handshake.timestamp_ms = words[0].timestamp_ms;
    
    return true;
}

bool AQCParser::parse_inlink(const ALEWord* words, size_t count, AQCInlink& inlink) {
    if (count < 2) {
        return false;
    }

    // Word 0: TO address. Word 1: terminator. Optional: further AQC control words.
    if (words[0].type == PreambleType::TO || words[0].type == PreambleType::TWAS) {
        inlink.to_address = words[0].address;
        inlink.net_address_flag = (words[0].type == PreambleType::TWAS);

        if (is_aqc_format(words[0])) {
            extract_data_elements(words[0].raw_payload, inlink.de);
            inlink.slot_position = inlink.de.de2;
            inlink.ack_this_flag = (inlink.de.de9 == DE9_TransactionCode::ACK_LAST);
        }
    } else {
        return false;
    }

    if (words[1].type == PreambleType::FROM || words[1].type == PreambleType::TIS) {
        inlink.term_address = words[1].address;
    } else {
        return false;
    }

    if (count >= 3 && words[2].type == PreambleType::CMD) {
        inlink.crc_status = CRCStatus::NOT_APPLICABLE;  // Placeholder for CRC validation
    }
    
    inlink.timestamp_ms = words[0].timestamp_ms;
    
    return true;
}

bool AQCParser::parse_orderwire(const ALEWord* words, size_t count, AQCOrderwire& orderwire) {
    if (count < 1) {
        return false;
    }
    
    // Orderwire (AMD) messages use DATA words; concatenate text across words.
    std::string message;

    for (size_t i = 0; i < count; i++) {
        if (words[i].type == PreambleType::DATA) {
            message.append(words[i].address, 3);  // 3 chars per word
        } else if (words[i].type == PreambleType::CMD) {
            // CMD may carry CRC; actual validation against accumulated
            // message deferred to AQCCRC class.
            uint16_t crc = words[i].raw_payload & 0xFFFF;
            orderwire.calculated_crc = crc;
            orderwire.crc_status = CRCStatus::NOT_APPLICABLE;  // Placeholder
        }
    }
    
    // Trim trailing spaces/nulls
    while (!message.empty() && (message.back() == ' ' || message.back() == '\0')) {
        message.pop_back();
    }
    
    orderwire.message = message;
    orderwire.timestamp_ms = words[0].timestamp_ms;
    
    return !message.empty();
}

const char* AQCParser::traffic_class_name(DE3_TrafficClass tc) {
    uint8_t index = static_cast<uint8_t>(tc);
    if (index > 15) index = 15;
    return TRAFFIC_CLASS_NAMES[index];
}

const char* AQCParser::transaction_code_name(DE9_TransactionCode code) {
    uint8_t index = static_cast<uint8_t>(code);
    if (index > 7) index = 7;
    return TRANSACTION_CODE_NAMES[index];
}

// ============================================================================
// CRC Implementation
// ============================================================================

uint8_t AQCCRC::calculate_crc8(const uint8_t* data, size_t length) {
    // CRC-8 polynomial: 0x07 (x^8 + x^2 + x + 1)
    uint8_t crc = 0x00;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    return crc;
}

uint16_t AQCCRC::calculate_crc16(const uint8_t* data, size_t length) {
    // CRC-16 CCITT polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    return crc;
}

bool AQCCRC::validate_crc8(const uint8_t* data, size_t length) {
    if (length < 1) {
        return false;
    }
    
    // Last byte is CRC
    uint8_t received_crc = data[length - 1];
    uint8_t calculated_crc = calculate_crc8(data, length - 1);
    
    return (received_crc == calculated_crc);
}

bool AQCCRC::validate_crc16(const uint8_t* data, size_t length) {
    if (length < 2) {
        return false;
    }
    
    // Last 2 bytes are CRC (big-endian)
    uint16_t received_crc = (static_cast<uint16_t>(data[length - 2]) << 8) | data[length - 1];
    uint16_t calculated_crc = calculate_crc16(data, length - 2);
    
    return (received_crc == calculated_crc);
}

// ============================================================================
// Slot Manager Implementation
// ============================================================================

SlotManager::SlotManager() {}

uint32_t SlotManager::calculate_slot_time(uint8_t slot_number, uint32_t base_time_ms) {
    return calculate_slot_time(slot_number, base_time_ms, SLOT_DURATION_MS);
}

uint32_t SlotManager::calculate_slot_time(uint8_t slot_number, uint32_t base_time_ms,
                                           uint32_t dwell_ms) {
    if (slot_number >= NUM_SLOTS) {
        slot_number = NUM_SLOTS - 1;
    }
    // Guard: never divide by zero if caller passes 0; fall back to default.
    const uint32_t effective_dwell = (dwell_ms > 0) ? dwell_ms : SLOT_DURATION_MS;
    return base_time_ms + (slot_number * effective_dwell);
}

bool SlotManager::is_valid_dwell_rate(uint32_t dwell_ms) {
    // MIL-STD-188-141B Annex B §"Programmable timing" defines two scan rates:
    //   TD5 = 200 ms (5 ch/s, basic)   — mandatory baseline
    //   TD2 = 500 ms (2 ch/s, minimum) — supported for backward compatibility
    return (dwell_ms == DWELL_TD5_MS) || (dwell_ms == DWELL_TD2_MS);
}

uint8_t SlotManager::assign_slot(const std::string& address) {
    // Simple hash: sum ASCII values mod 8
    uint32_t hash = 0;
    
    for (char ch : address) {
        hash += static_cast<uint32_t>(ch);
    }
    
    return hash % NUM_SLOTS;
}

uint32_t SlotManager::get_slot_duration_ms() {
    return SLOT_DURATION_MS;
}

} // namespace aqc
} // namespace ale
