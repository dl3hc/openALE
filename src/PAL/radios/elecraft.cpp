/**
 * @file elecraft.cpp
 * @brief Elecraft CAT protocol implementation
 * 
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#include "pal/radios/elecraft.h"
#include <cstdio>

namespace pal {

Elecraft::Elecraft(ISerial* serial)
    : Kenwood(serial)
{
}

std::string Elecraft::get_port_config() const {
    // Elecraft default: 38400 baud
    return "38400,n,8,1";
}

void Elecraft::set_power(int watts) {
    // Elecraft power command: PC###; (3 digits, watts)
    char buf[8];
    std::snprintf(buf, sizeof(buf), "PC%03d;", watts);
    Kenwood::send_command(std::string(buf));
}

void Elecraft::set_antenna(int ant) {
    // Elecraft antenna command: AN#; (1 or 2)
    char buf[8];
    std::snprintf(buf, sizeof(buf), "AN%d;", ant);
    Kenwood::send_command(std::string(buf));
}

} // namespace pal
