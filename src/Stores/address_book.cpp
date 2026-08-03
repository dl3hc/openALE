/**
 * \file address_book.cpp
 * \brief ALE address book implementation
 */

#include "Stores/address_book.h"
#include "Word/ale_word.h"
#include <algorithm>

namespace ale {

AddressBook::AddressBook() {}

bool AddressBook::set_self_address(const std::string& address)
{
    // Addresses are always Basic 38 per A.5.2.4.2
    if (address.length() < 3 || address.length() > 15)
        return false;
    for (char ch : address) {
        if (!WordParser::is_valid_basic38_char(ch))
            return false;
    }
    self_address = address;
    return true;
}

void AddressBook::add_station(const std::string& address, const std::string& name)
{
    for (const auto& s : stations) {
        if (s.first == address) return;
    }
    stations.push_back({address, name});
}

void AddressBook::update_station(const std::string& address, const std::string& name)
{
    for (auto& s : stations) {
        if (s.first == address) { s.second = name; return; }
    }
    stations.push_back({address, name});
}

void AddressBook::remove_station(const std::string& address)
{
    stations.erase(std::remove_if(stations.begin(), stations.end(),
                                  [&](const auto& p) { return p.first == address; }),
                   stations.end());
}

void AddressBook::clear_stations()
{
    stations.clear();
}

void AddressBook::add_net(const std::string& net_address,
                           const std::string& description)
{
    for (const auto& n : nets) {
        if (n.first == net_address) return;
    }
    nets.push_back({net_address, description});
}

bool AddressBook::is_self(const std::string& address) const
{
    return address == self_address;
}

bool AddressBook::is_known_station(const std::string& address) const
{
    for (const auto& s : stations) {
        if (s.first == address) return true;
    }
    return false;
}

bool AddressBook::is_known_net(const std::string& address) const
{
    for (const auto& n : nets) {
        if (n.first == address) return true;
    }
    return false;
}

bool AddressBook::match_wildcard(const std::string& pattern,
                                  const std::string& address)
{
    // Per A.5.2.4.9 '?' is the wildcard, substituting for any of the 36
    // alphanumeric characters (A-Z, 0-9).  Pattern and address must have
    // equal length; each '?' covers exactly one address character position.
    if (pattern.length() != address.length())
        return false;

    for (size_t i = 0; i < pattern.length(); ++i) {
        if (pattern[i] == '?') {
            // '?' matches A-Z or 0-9 only (the 36-character alphanumeric subset).
            // '@' and '?' in the address are not matched by a wildcard.
            char c = address[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return false;
        } else if (pattern[i] != address[i]) {
            return false;
        }
    }
    return true;
}

} // namespace ale
