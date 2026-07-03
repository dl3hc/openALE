/**
 * \file address_book.h
 * \brief ALE address book: self address, known stations and nets
 *
 * All addresses are constrained to the Basic 38 ASCII subset and
 * 3-15 characters per MIL-STD-188-141B A.5.2.4.2.
 */

#pragma once

#include <string>
#include <vector>

namespace ale {

/**
 * \class AddressBook
 * Manage ALE addresses (self, other stations, nets).
 *
 * All addresses are constrained to the Basic 38 ASCII subset and
 * 3-15 characters per MIL-STD-188-141B A.5.2.4.2.
 */
class AddressBook {
public:
    AddressBook();

    /**
     * Set self address (this station's call sign).
     * Must be 3-15 Basic 38 characters.
     * \return true if valid and set
     */
    bool set_self_address(const std::string& address);

    /** Get self address. */
    std::string get_self_address() const { return self_address; }

    /**
     * Add a known station address. No-op if address already present.
     * \param address Station address (Basic 38, 3-15 chars)
     * \param name    Optional friendly name
     */
    void add_station(const std::string& address, const std::string& name = "");

    /**
     * Add or update (in-place) a known station address.
     * If address already exists its name is updated; otherwise the entry is appended.
     */
    void update_station(const std::string& address, const std::string& name = "");

    /** Remove a known station by address. No-op if not present. */
    void remove_station(const std::string& address);

    /** Clear all known stations. */
    void clear_stations();

    /** Read-only view of the known-station list (address, name pairs). */
    const std::vector<std::pair<std::string, std::string>>& all_stations() const
        { return stations; }

    /**
     * Add a net (group) address.
     * \param net_address Net address (Basic 38, 3-15 chars)
     * \param description Optional description
     */
    void add_net(const std::string& net_address,
                 const std::string& description = "");

    /** Return true if address matches the self address exactly. */
    bool is_self(const std::string& address) const;

    /** Return true if address is in the known-station list. */
    bool is_known_station(const std::string& address) const;

    /** Return true if address is a known net. */
    bool is_known_net(const std::string& address) const;

    /**
     * Match an address against a pattern that may contain wildcard characters.
     *
     * Per MIL-STD-188-141B A.5.2.4.9:
     *  '?' (0111111) is the wildcard; it substitutes for any single alphanumeric
     *  character (A-Z, 0-9 — exactly 36 values) at the same position.
     *  '@' is the utility/stuffing symbol and is NOT a wildcard.
     *
     * Pattern and address must have equal length (one wildcard covers exactly one
     * address character position).
     *
     * \param pattern Pattern string (may contain '?' wildcards)
     * \param address Address to match against (Basic 38)
     * \return true if pattern matches address
     */
    static bool match_wildcard(const std::string& pattern,
                                const std::string& address);

private:
    std::string self_address;
    std::vector<std::pair<std::string, std::string>> stations; ///< address, name
    std::vector<std::pair<std::string, std::string>> nets;     ///< net, description
};

} // namespace ale
