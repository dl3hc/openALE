/**
 * @file elecraft.h
 * @brief Elecraft CAT protocol encoder/decoder
 * 
 * Elecraft uses Kenwood-compatible ASCII commands.
 * Additional Elecraft-specific commands supported.
 * 
 * Supports K2, K3, K3S, KX2, KX3, etc.
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#pragma once

#include "pal/radios/kenwood.h"

namespace pal {

/**
 * @brief Elecraft CAT radio implementation
 * 
 * Elecraft radios are Kenwood-compatible with extensions.
 * This class inherits from Kenwood and adds Elecraft-specific features.
 */
class Elecraft : public Kenwood {
public:
    explicit Elecraft(ISerial* serial);
    
    ~Elecraft() override = default;
    
    // Override for Elecraft-specific behavior if needed
    std::string get_port_config() const override;
    
    // Elecraft-specific commands
    void set_power(int watts);
    void set_antenna(int ant);  // 1 or 2
    
private:
    void send_command(const std::string& cmd);
};

} // namespace pal
