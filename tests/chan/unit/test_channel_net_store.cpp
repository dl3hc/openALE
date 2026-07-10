/**
 * \file test_channel_net_store.cpp
 * \brief Tests for NetStore/ContactStore/SelfAddressStore (Stores/ale_data_store.h),
 *        Channel-ID auto-assignment, .ale persistence of IDs/nets, and the
 *        target_scan_channels auto-derivation wired into ALEController::initiate_call().
 *
 * Background: Tsc = C x 2 x Trw (MIL-STD-188-141B Annex B), where C is the number
 * of scan/sounding-enabled channels of the net a call target belongs to. These
 * tests verify the data model that produces C and the wiring that applies it.
 */

#include "Stores/ale_data_store.h"
#include "App/ale_controller.h"
#include "App/ale_event_data.h"
#include "PAL/events.h"
#include "PAL/radios/mock_radio.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdio>
#include <algorithm>

namespace ale {

// ============================================================================
// NetStore
// ============================================================================

bool test_net_store_add_remove_find()
{
    std::cout << "\n[NetStore] add/remove/find\n";

    NetStore store;
    bool added = store.add_net("XYZ");
    bool dup_rejected = !store.add_net("XYZ");
    bool found = (store.find("XYZ") != nullptr);
    bool missing = (store.find("ZZZ") == nullptr);

    std::cout << "  add_net succeeds: " << (added ? "PASS" : "FAIL") << "\n";
    std::cout << "  duplicate add rejected: " << (dup_rejected ? "PASS" : "FAIL") << "\n";
    std::cout << "  find existing: " << (found ? "PASS" : "FAIL") << "\n";
    std::cout << "  find missing returns null: " << (missing ? "PASS" : "FAIL") << "\n";

    bool removed = store.remove_net("XYZ");
    bool gone = (store.find("XYZ") == nullptr);
    bool remove_missing_fails = !store.remove_net("XYZ");
    std::cout << "  remove_net succeeds: " << (removed ? "PASS" : "FAIL") << "\n";
    std::cout << "  removed net no longer found: " << (gone ? "PASS" : "FAIL") << "\n";
    std::cout << "  remove of already-removed net fails: " << (remove_missing_fails ? "PASS" : "FAIL") << "\n";

    return added && dup_rejected && found && missing && removed && gone && remove_missing_fails;
}

bool test_net_store_assign_unassign_channel()
{
    std::cout << "\n[NetStore] assign/unassign channel (dedup, unknown net)\n";

    NetStore store;
    store.add_net("XYZ");

    bool assigned = store.assign_channel("XYZ", "C-1");
    store.assign_channel("XYZ", "C-3");
    store.assign_channel("XYZ", "C-1");  // duplicate, should not double-add
    bool unknown_net_rejected = !store.assign_channel("NOPE", "C-9");

    const Net* net = store.find("XYZ");
    bool two_members = (net && net->channel_ids.size() == 2);
    std::cout << "  assign succeeds: " << (assigned ? "PASS" : "FAIL") << "\n";
    std::cout << "  duplicate assign de-duplicated (2 members, not 3): "
              << (two_members ? "PASS" : "FAIL") << "\n";
    std::cout << "  assign to unknown net rejected: " << (unknown_net_rejected ? "PASS" : "FAIL") << "\n";

    store.unassign_channel("XYZ", "C-1");
    net = store.find("XYZ");
    bool one_left = (net && net->channel_ids.size() == 1 && net->channel_ids[0] == "C-3");
    std::cout << "  unassign removes the right member: " << (one_left ? "PASS" : "FAIL") << "\n";

    return assigned && two_members && unknown_net_rejected && one_left;
}

bool test_net_store_unassign_everywhere()
{
    std::cout << "\n[NetStore] unassign_channel_everywhere\n";

    NetStore store;
    store.add_net("A");
    store.add_net("B");
    store.assign_channel("A", "C-1");
    store.assign_channel("B", "C-1");
    store.assign_channel("B", "C-2");

    store.unassign_channel_everywhere("C-1");

    bool a_empty = store.find("A")->channel_ids.empty();
    bool b_has_only_c2 = (store.find("B")->channel_ids.size() == 1
                        && store.find("B")->channel_ids[0] == "C-2");
    std::cout << "  removed from net A: " << (a_empty ? "PASS" : "FAIL") << "\n";
    std::cout << "  removed from net B, C-2 untouched: " << (b_has_only_c2 ? "PASS" : "FAIL") << "\n";

    return a_empty && b_has_only_c2;
}

bool test_net_scan_channel_count()
{
    std::cout << "\n[net_scan_channel_count] counts only enabled member channels\n";

    Net net{"XYZ", {"C-1", "C-2", "C-3", "C-9"}};  // C-9 doesn't exist in the channel list

    std::vector<Channel> channels;
    Channel c1(14250000); c1.id = "C-1"; c1.enabled = true;  channels.push_back(c1);
    Channel c2(7100000);  c2.id = "C-2"; c2.enabled = true;  channels.push_back(c2);
    Channel c3(3500000);  c3.id = "C-3"; c3.enabled = false; channels.push_back(c3);  // SCAN=N
    Channel c4(10000000); c4.id = "C-4"; c4.enabled = true;  channels.push_back(c4);  // not a member

    const uint32_t count = net_scan_channel_count(net, channels);
    std::cout << "  C-1(Y)+C-2(Y)+C-3(N)+C-9(missing) -> 2: "
              << (count == 2 ? "PASS" : "FAIL") << " (got " << count << ")\n";

    return count == 2;
}

// ============================================================================
// ContactStore
// ============================================================================

bool test_contact_store_add_update_remove()
{
    std::cout << "\n[ContactStore] add/update/remove/find\n";

    AddressBook ab;
    ContactStore store(ab);
    Contact c;
    c.callsign = "BOB"; c.name = "Bob"; c.enabled = true;
    c.net_members = {"XYZ"};
    bool added = store.add_or_update(c);

    c.name = "Bobby";
    store.add_or_update(c);  // update in place, not a second entry
    bool still_one = (store.size() == 1);
    const Contact* found = store.find("BOB");
    bool updated = (found && found->name == "Bobby");

    std::cout << "  add succeeds: " << (added ? "PASS" : "FAIL") << "\n";
    std::cout << "  re-add with same callsign updates in place: " << (still_one ? "PASS" : "FAIL") << "\n";
    std::cout << "  update applied: " << (updated ? "PASS" : "FAIL") << "\n";

    bool removed = store.remove("BOB");
    bool gone = (store.find("BOB") == nullptr);
    std::cout << "  remove succeeds: " << (removed ? "PASS" : "FAIL") << "\n";
    std::cout << "  removed contact not found: " << (gone ? "PASS" : "FAIL") << "\n";

    bool empty_rejected = !store.add_or_update(Contact{});
    std::cout << "  empty callsign rejected: " << (empty_rejected ? "PASS" : "FAIL") << "\n";

    return added && still_one && updated && removed && gone && empty_rejected;
}

// ============================================================================
// SelfAddressStore
// ============================================================================

bool test_self_address_store_primary_defaults_to_first()
{
    std::cout << "\n[SelfAddressStore] primary defaults to first entry added\n";

    SelfAddressStore store;
    bool added1 = store.add(SelfAddressEntry{"SAM", true, {}, true});
    bool primary_is_sam = (store.primary() == "SAM");

    store.add(SelfAddressEntry{"SAM2", true, {}, true});
    bool primary_unchanged = (store.primary() == "SAM");

    std::cout << "  first add succeeds: " << (added1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  primary defaults to first entry: " << (primary_is_sam ? "PASS" : "FAIL") << "\n";
    std::cout << "  adding a second entry doesn't steal primary: " << (primary_unchanged ? "PASS" : "FAIL") << "\n";

    return added1 && primary_is_sam && primary_unchanged;
}

bool test_self_address_store_set_primary_and_remove()
{
    std::cout << "\n[SelfAddressStore] set_primary / remove reassigns primary\n";

    SelfAddressStore store;
    store.add(SelfAddressEntry{"SAM", true, {}, true});
    store.add(SelfAddressEntry{"SAM2", true, {}, true});

    bool switched = store.set_primary("SAM2");
    bool primary_is_sam2 = (store.primary() == "SAM2");
    bool unknown_rejected = !store.set_primary("NOPE");

    std::cout << "  set_primary to known entry succeeds: " << (switched ? "PASS" : "FAIL") << "\n";
    std::cout << "  primary updated: " << (primary_is_sam2 ? "PASS" : "FAIL") << "\n";
    std::cout << "  set_primary to unknown entry rejected: " << (unknown_rejected ? "PASS" : "FAIL") << "\n";

    store.remove("SAM2");
    bool reassigned = (store.primary() == "SAM");
    std::cout << "  removing primary reassigns to remaining entry: " << (reassigned ? "PASS" : "FAIL") << "\n";

    bool matches = store.matches_self("SAM") && !store.matches_self("ZZZ");
    std::cout << "  matches_self: " << (matches ? "PASS" : "FAIL") << "\n";

    return switched && primary_is_sam2 && unknown_rejected && reassigned && matches;
}

bool test_self_address_store_min_capacity_20()
{
    std::cout << "\n[SelfAddressStore] minimum capacity 20 entries, all fields present (AC-GEN-005-001)\n";

    SelfAddressStore store;
    for (int i = 0; i < 20; ++i) {
        std::string addr = "S" + std::to_string(i + 1);
        SelfAddressEntry e;
        e.address      = addr;
        e.enabled      = true;
        e.valid_channels = {"C-" + std::to_string(i + 1)};
        e.all_channels = false;
        bool ok = store.add(e);
        if (!ok) {
            std::cout << "  FAIL: add() rejected entry " << i << " before reaching 20\n";
            return false;
        }
    }
    bool full_at_20 = (store.size() == 20);
    std::cout << "  20 entries accepted: " << (full_at_20 ? "PASS" : "FAIL")
              << " (size=" << store.size() << ")\n";

    SelfAddressEntry extra; extra.address = "S21";
    bool rejected = !store.add(extra);
    std::cout << "  21st entry rejected at kCapacity: " << (rejected ? "PASS" : "FAIL") << "\n";

    bool capacity_constant_ok = (SelfAddressStore::kCapacity == 20);
    std::cout << "  kCapacity == 20: " << (capacity_constant_ok ? "PASS" : "FAIL") << "\n";

    const auto& first = store.all()[0];
    bool has_fields = (!first.address.empty()
                    && first.valid_channels.size() == 1
                    && !first.all_channels);
    std::cout << "  SelfAddressEntry has address/valid_channels/all_channels fields: "
              << (has_fields ? "PASS" : "FAIL") << "\n";

    // Updating an existing entry while full must succeed (not a new slot).
    SelfAddressEntry update; update.address = "S1"; update.enabled = false;
    bool update_ok = store.add(update);
    bool still_20  = (store.size() == 20);
    std::cout << "  update of existing entry while full succeeds: " << (update_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  size unchanged after update: " << (still_20 ? "PASS" : "FAIL") << "\n";

    return full_at_20 && rejected && capacity_constant_ok && has_fields && update_ok && still_20;
}

// ============================================================================
// ALEController — channel-ID auto-assignment + net unassign-on-delete
// ============================================================================

bool test_controller_channel_id_auto_assignment()
{
    std::cout << "\n[ALEController] channel-ID auto-assignment\n";

    ALEController ctrl;
    ctrl.add_channel(Channel(14250000));
    ctrl.add_channel(Channel(7100000));
    ctrl.add_channel(Channel(3500000, 3600000));

    const auto& chans = ctrl.channels();
    bool ids_ok = (chans.size() == 3
                && chans[0].id == "C-1"
                && chans[1].id == "C-2"
                && chans[2].id == "C-3");
    std::cout << "  3 channels auto-assigned C-1..C-3: " << (ids_ok ? "PASS" : "FAIL");
    if (!ids_ok) {
        std::cout << " (got";
        for (const auto& c : chans) std::cout << " '" << c.id << "'";
        std::cout << ")";
    }
    std::cout << "\n";

    Channel explicit_ch(10000000);
    explicit_ch.id = "C-99";
    ctrl.add_channel(explicit_ch);
    bool explicit_preserved = (ctrl.channels().back().id == "C-99");
    std::cout << "  explicit id preserved: " << (explicit_preserved ? "PASS" : "FAIL") << "\n";

    return ids_ok && explicit_preserved;
}

bool test_controller_del_channel_unassigns_from_nets()
{
    std::cout << "\n[ALEController] del_channel unassigns the channel from all nets\n";

    ALEController ctrl;
    ctrl.add_channel(Channel(14250000));  // C-1
    ctrl.add_net("XYZ");
    ctrl.assign_channel_to_net("XYZ", "C-1");

    bool assigned = (ctrl.nets().front().channel_ids.size() == 1);
    ctrl.del_channel(14250000);
    bool unassigned = ctrl.nets().front().channel_ids.empty();

    std::cout << "  channel assigned before delete: " << (assigned ? "PASS" : "FAIL") << "\n";
    std::cout << "  net membership cleared after delete: " << (unassigned ? "PASS" : "FAIL") << "\n";

    return assigned && unassigned;
}

// ============================================================================
// .ale persistence — IDs + nets round-trip, legacy file compatibility
// ============================================================================

bool test_ale_roundtrip_ids_and_nets()
{
    std::cout << "\n[.ale persistence] round-trip channel IDs and nets\n";

    const std::string path = "test_channel_net_roundtrip.ale";

    {
        ALEController ctrl;
        ctrl.add_channel(Channel(14250000));  // C-1
        ctrl.add_channel(Channel(7100000));   // C-2
        ctrl.add_net("XYZ");
        ctrl.assign_channel_to_net("XYZ", "C-1");
        ctrl.assign_channel_to_net("XYZ", "C-2");
        ctrl.save_channels(path);
    }

    ALEController reloaded;
    bool loaded = reloaded.load_channels(path);
    bool ids_survived = (reloaded.channels().size() == 2
                       && reloaded.channels()[0].id == "C-1"
                       && reloaded.channels()[1].id == "C-2");
    bool net_survived = (reloaded.nets().size() == 1
                       && reloaded.nets().front().name == "XYZ"
                       && reloaded.nets().front().channel_ids.size() == 2);

    std::cout << "  file loads: " << (loaded ? "PASS" : "FAIL") << "\n";
    std::cout << "  channel IDs survive round-trip: " << (ids_survived ? "PASS" : "FAIL") << "\n";
    std::cout << "  net + assignments survive round-trip: " << (net_survived ? "PASS" : "FAIL") << "\n";

    std::remove(path.c_str());
    return loaded && ids_survived && net_survived;
}

bool test_ale_legacy_file_without_ids_still_loads()
{
    std::cout << "\n[.ale persistence] legacy file (no ID:/NET: lines) still loads\n";

    const std::string path = "test_channel_net_legacy.ale";
    {
        std::ofstream f(path);
        f << "# ALE channel list — MIL-STD-188-141B\n";
        f << "# rx_hz tx_hz mode [label]\n";
        f << "14250000 0 USB 40m-Calling\n";
        f << "7100000 0 USB\n";
    }

    ALEController ctrl;
    bool loaded = ctrl.load_channels(path);
    bool ids_assigned = (ctrl.channels().size() == 2
                       && ctrl.channels()[0].id == "C-1"
                       && ctrl.channels()[1].id == "C-2");
    bool no_nets = ctrl.nets().empty();

    std::cout << "  legacy file loads: " << (loaded ? "PASS" : "FAIL") << "\n";
    std::cout << "  IDs auto-assigned on load: " << (ids_assigned ? "PASS" : "FAIL") << "\n";
    std::cout << "  no nets (none in file): " << (no_nets ? "PASS" : "FAIL") << "\n";

    std::remove(path.c_str());
    return loaded && ids_assigned && no_nets;
}

// ============================================================================
// .ale persistence — per-channel inhibit flags + rx_only/tx_only round-trip
// ============================================================================

bool test_ale_roundtrip_inhibit_flags()
{
    std::cout << "\n[.ale persistence] per-channel inhibit flags + rx_only/tx_only round-trip\n";

    const std::string path = "test_channel_flags_roundtrip.ale";

    {
        ALEController ctrl;
        Channel a(14250000, 0, "USB", "USB");
        a.id = "C-1"; a.inhibit_calling = true; a.label = "40m-Calling";
        ctrl.add_channel(a);

        Channel b(7100000, 0, "USB", "USB");
        b.id = "C-2"; b.inhibit_sounding = true; b.inhibit_reporting = true; b.rx_only = true;
        ctrl.add_channel(b);

        Channel c(3500000, 0, "USB", "USB");
        c.id = "C-3"; c.enabled = false;  // master off via the OFF code
        ctrl.add_channel(c);

        Channel d(18160000, 0, "USB", "USB");
        d.id = "C-4"; d.tx_only = true;   // Direction=TX (no RX)
        ctrl.add_channel(d);

        ctrl.save_channels(path);
    }

    ALEController reloaded;
    bool loaded = reloaded.load_channels(path);
    const auto& ch = reloaded.channels();
    bool count_ok = (ch.size() == 4);
    bool c1 = (ch[0].id == "C-1" && ch[0].inhibit_calling
               && !ch[0].inhibit_sounding && !ch[0].inhibit_reporting
               && !ch[0].rx_only && !ch[0].tx_only && ch[0].enabled && ch[0].label == "40m-Calling");
    bool c2 = (ch[1].id == "C-2" && ch[1].inhibit_sounding && ch[1].inhibit_reporting
               && ch[1].rx_only && !ch[1].tx_only && !ch[1].inhibit_calling && ch[1].enabled
               && ch[1].label.empty());
    bool c3 = (ch[2].id == "C-3" && !ch[2].enabled
               && !ch[2].inhibit_calling && !ch[2].inhibit_sounding
               && !ch[2].inhibit_reporting && !ch[2].rx_only && !ch[2].tx_only);
    bool c4 = (ch[3].id == "C-4" && ch[3].tx_only && !ch[3].rx_only
               && !ch[3].inhibit_calling && !ch[3].inhibit_sounding
               && !ch[3].inhibit_reporting && ch[3].enabled);

    std::cout << "  file loads: " << (loaded ? "PASS" : "FAIL") << "\n";
    std::cout << "  4 channels: " << (count_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  C-1 inhibit_calling only: " << (c1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  C-2 inhibit_sounding+reporting+rx_only: " << (c2 ? "PASS" : "FAIL") << "\n";
    std::cout << "  C-3 enabled=false (OFF): " << (c3 ? "PASS" : "FAIL") << "\n";
    std::cout << "  C-4 tx_only (Direction=TX): " << (c4 ? "PASS" : "FAIL") << "\n";

    std::remove(path.c_str());
    return loaded && count_ok && c1 && c2 && c3 && c4;
}

// A bracketed label that is NOT all-recognized flag codes must be preserved as
// the label text (not swallowed as flags).
bool test_ale_bracketed_label_not_swallowed()
{
    std::cout << "\n[.ale persistence] bracketed non-flag token kept as label\n";

    const std::string path = "test_channel_bracket_label.ale";
    {
        std::ofstream f(path);
        f << "ID:C-1 14250000 0 USB [40m] Backup Channel\n";
    }

    ALEController ctrl;
    bool loaded = ctrl.load_channels(path);
    const auto& ch = ctrl.channels();
    bool ok = (ch.size() == 1 && ch[0].id == "C-1"
               && ch[0].label == "[40m] Backup Channel"
               && ch[0].enabled && !ch[0].inhibit_calling && !ch[0].rx_only);

    std::cout << "  file loads: " << (loaded ? "PASS" : "FAIL") << "\n";
    std::cout << "  bracketed label preserved: " << (ok ? "PASS" : "FAIL") << "\n";

    std::remove(path.c_str());
    return loaded && ok;
}

// ============================================================================
// End-to-end: per-net calling_length_c drives scanning-call length
// ============================================================================

bool test_initiate_call_uses_net_calling_length_c()
{
    std::cout << "\n[target_scan_channels] initiate_call uses net's calling_length_c\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");

    for (int i = 0; i < 5; ++i) {
        Channel ch(14250000 + i * 1000);
        ch.enabled = (i != 4);
        ctrl.add_channel(ch);
    }
    ctrl.add_net("XYZ");
    for (const auto& c : ctrl.channels())
        ctrl.assign_channel_to_net("XYZ", c.id);
    ctrl.add_contact("BOB", "Bob", "enabled", "XYZ", "ALL");

    // Set net's calling_length_c to 7; global assumed_scan_channels to 4.
    // The net's value must win over the global fallback.
    Net policy;
    policy.name = "XYZ";
    policy.calling_length_c = 7;
    ctrl.update_net(policy);
    ctrl.set_assumed_scan_channels(4);  // global fallback — must NOT win when net is known

    bool started = ctrl.initiate_call("BOB");
    bool used    = (ctrl.get_target_scan_channels() == 7);  // net C=7, not global 4

    std::cout << "  call started: " << (started ? "PASS" : "FAIL") << "\n";
    std::cout << "  net calling_length_c=7 used (global assumed=4 ignored): "
              << (used ? "PASS" : "FAIL")
              << " (got " << ctrl.get_target_scan_channels() << ")\n";

    return started && used;
}

bool test_initiate_call_without_contact_leaves_target_scan_channels_untouched()
{
    std::cout << "\n[target_scan_channels] no contact/net mapping -> value left untouched\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    ctrl.set_target_scan_channels(7);  // simulates ale_cli's explicit configuration

    bool started = ctrl.initiate_call("UNKNOWN");
    bool unchanged = (ctrl.get_target_scan_channels() == 7);

    std::cout << "  call started: " << (started ? "PASS" : "FAIL") << "\n";
    std::cout << "  target_scan_channels left at 7 (no contact/net regression): "
              << (unchanged ? "PASS" : "FAIL")
              << " (got " << ctrl.get_target_scan_channels() << ")\n";

    return started && unchanged;
}

// Direction=RX (rx_only): a receive-only channel is a hard TX prohibition — it
// is filtered out of the outbound call set, so initiate_call() must refuse when
// every configured channel is rx_only (no callable channels). A tx_only channel
// (Direction=TX) is the mirror: TX is permitted, so it stays callable. The
// tx_inhibited / rx_inhibited helpers must resolve per-frequency from flags.
bool test_initiate_call_rx_only_channel_rejected()
{
    std::cout << "\n[Direction] rx_only refuses calling, tx_only stays callable\n";

    // All RX-only -> initiate_call refuses (no callable channels).
    ALEController rx_ctrl;
    rx_ctrl.set_self_address("SAM");
    Channel a(14250000); a.id = "C-1"; a.rx_only = true;
    Channel b(7100000);  b.id = "C-2"; b.rx_only = true;
    rx_ctrl.add_channel(a);
    rx_ctrl.add_channel(b);
    bool rx_refused = !rx_ctrl.initiate_call("BOB");
    bool tx_helper  = rx_ctrl.tx_inhibited(14250000) && rx_ctrl.tx_inhibited(7100000)
                      && !rx_ctrl.rx_inhibited(14250000);

    // TX-only -> callable (TX permitted), so initiate_call proceeds past the
    // callable filter (sm_.initiate_call returns true in the bare controller).
    ALEController tx_ctrl;
    tx_ctrl.set_self_address("SAM");
    Channel c(3500000); c.id = "C-3"; c.tx_only = true;
    tx_ctrl.add_channel(c);
    bool tx_callable = tx_ctrl.initiate_call("BOB");
    bool rx_helper   = tx_ctrl.rx_inhibited(3500000) && !tx_ctrl.tx_inhibited(3500000);

    bool unknown = !rx_ctrl.tx_inhibited(99999999) && !tx_ctrl.rx_inhibited(99999999);

    std::cout << "  RX-only call refused: " << (rx_refused ? "PASS" : "FAIL") << "\n";
    std::cout << "  TX-only stays callable: " << (tx_callable ? "PASS" : "FAIL") << "\n";
    std::cout << "  tx_inhibited()/rx_inhibited() per-freq: "
              << ((tx_helper && rx_helper) ? "PASS" : "FAIL") << "\n";
    std::cout << "  unconfigured freq -> false: " << (unknown ? "PASS" : "FAIL") << "\n";

    return rx_refused && tx_callable && tx_helper && rx_helper && unknown;
}

// Active-net scoping: with set_active_scan_net() set, initiate_call() must
// push only that net's channels to the SM (get_calling_channels()), while C
// still comes from the contact's net. Empty/missing active net → all callable
// channels (fallback). Locks in the GUI Network-pill behaviour.
bool test_initiate_call_scoped_to_active_scan_net()
{
    std::cout << "\n[active_scan_net] initiate_call scopes outbound channels to the active net\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");

    // 6 enabled channels; net ABC gets the first 3, net XYZ gets all 6.
    for (int i = 0; i < 6; ++i) {
        Channel ch(14250000 + i * 1000);
        ch.enabled = true;
        ctrl.add_channel(ch);
    }
    std::vector<std::string> allIds;
    for (const auto& c : ctrl.channels()) allIds.push_back(c.id);
    ctrl.add_net("ABC");
    ctrl.add_net("XYZ");
    for (int i = 0; i < 3; ++i) ctrl.assign_channel_to_net("ABC", allIds[i]);
    for (const auto& id : allIds) ctrl.assign_channel_to_net("XYZ", id);

    // Contact BOB belongs to XYZ with calling_length_c=7 — C must stay 7 even
    // though the active net (ABC) is different.
    ctrl.add_contact("BOB", "Bob", "enabled", "XYZ", "ALL");
    Net policy;
    policy.name = "XYZ";
    policy.calling_length_c = 7;
    ctrl.update_net(policy);
    ctrl.set_assumed_scan_channels(4);

    auto ids_of = [](const std::vector<Channel>& v) {
        std::vector<std::string> ids;
        for (const auto& c : v) ids.push_back(c.id);
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    // 1) Active net = ABC → SM gets only ABC's 3 channels; C still 7.
    //    The first call drives the SM into CALLING (no fake peer completes the
    //    handshake), so later calls' return value is dominated by SM state —
    //    what we assert is the calling-channel set the controller pushes, which
    //    is set before the SM state check. So we don't gate on the return value.
    ctrl.set_active_scan_net("ABC");
    bool started1 = ctrl.initiate_call("BOB");
    auto  got1    = ids_of(ctrl.get_calling_channels());
    std::vector<std::string> abcIds(allIds.begin(), allIds.begin() + 3);
    std::sort(abcIds.begin(), abcIds.end());
    bool scoped  = (got1 == abcIds);
    bool cStill7 = (ctrl.get_target_scan_channels() == 7);

    std::cout << "  scoped to ABC (3 ch): " << (scoped ? "PASS" : "FAIL") << "\n";
    std::cout << "  C still 7 (contact's net, not active net): "
              << (cStill7 ? "PASS" : "FAIL")
              << " (got " << ctrl.get_target_scan_channels() << ")\n";

    // 2) Active net cleared → fallback to all 6 callable channels.
    ctrl.set_active_scan_net("");
    (void)ctrl.initiate_call("BOB");
    auto  got2    = ids_of(ctrl.get_calling_channels());
    std::vector<std::string> sortedAll = allIds;
    std::sort(sortedAll.begin(), sortedAll.end());
    bool fallback = (got2 == sortedAll);

    std::cout << "  fallback to all channels (6 ch): " << (fallback ? "PASS" : "FAIL") << "\n";

    // 3) Active net with no assigned callable channels → also falls back (never blocks).
    ctrl.add_net("EMPTY");
    ctrl.set_active_scan_net("EMPTY");
    (void)ctrl.initiate_call("BOB");
    auto  got3    = ids_of(ctrl.get_calling_channels());
    bool fbEmpty  = (got3 == sortedAll);
    std::cout << "  empty active net falls back (not blocked): " << (fbEmpty ? "PASS" : "FAIL") << "\n";

    return started1 && scoped && cStill7 && fallback && fbEmpty;
}

// step_channel() must iterate ONLY the active net's channels when a net is
// selected, and all calling_channels_ when no net is selected. The operator's
// header step arrows must not wander outside the selected net. Uses MockRadio
// (radio_ is required by step_channel) and captures targets via on_channel_changed.
bool test_step_channel_scoped_to_active_scan_net()
{
    std::cout << "\n[active_scan_net] step_channel iterates only the active net's channels\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");

    // 6 channels C-1..C-6; net ABC gets only C-3 and C-5.
    for (int i = 0; i < 6; ++i) {
        Channel ch(14000000 + i * 1000);   // C-1=14000000, C-2=14001000, ... C-6=14005000
        ch.enabled = true;
        ctrl.add_channel(ch);
    }
    std::vector<std::string> allIds;
    for (const auto& c : ctrl.channels()) allIds.push_back(c.id);
    ctrl.add_net("ABC");
    ctrl.assign_channel_to_net("ABC", allIds[2]);   // C-3
    ctrl.assign_channel_to_net("ABC", allIds[4]);   // C-5

    pal::MockRadio radio;
    ctrl.set_radio(&radio);

    auto bus = pal::create_event_handler();
    uint32_t lastRx = 0;
    bus->on(pal::EventType::CHANNEL_CHANGED, [&](const pal::Event& ev) {
        const auto* ch = static_cast<const Channel*>(ev.data);
        lastRx = ch->rx_frequency_hz;
    });
    pal::set_event_handler(std::move(bus));

    auto stepN = [&](int dir, int n) {
        std::vector<uint32_t> seq;
        for (int i = 0; i < n; ++i) { ctrl.step_channel(dir); seq.push_back(lastRx); }
        return seq;
    };
    auto allIn = [](const std::vector<uint32_t>& seq, const std::vector<uint32_t>& allow) {
        for (uint32_t f : seq) if (std::find(allow.begin(), allow.end(), f) == allow.end()) return false;
        return true;
    };
    auto visitsAll = [](const std::vector<uint32_t>& seq, const std::vector<uint32_t>& allow) {
        for (uint32_t f : allow) if (std::find(seq.begin(), seq.end(), f) == seq.end()) return false;
        return true;
    };

    const uint32_t c3 = 14002000, c5 = 14004000;
    const std::vector<uint32_t> abcHz = {c3, c5};

    // 1) Active net = ABC → every step lands inside ABC, and both channels visit.
    ctrl.set_active_scan_net("ABC");
    auto seq1 = stepN(+1, 6);
    bool scoped  = allIn(seq1, abcHz);
    bool covers = visitsAll(seq1, abcHz);
    std::cout << "  +1 x6 stays within ABC {C-3,C-5}: " << (scoped ? "PASS" : "FAIL") << "\n";
    std::cout << "  both ABC channels visited: " << (covers ? "PASS" : "FAIL") << "\n";

    // 2) Reverse direction also stays within ABC.
    auto seq2 = stepN(-1, 6);
    bool scopedRev = allIn(seq2, abcHz);
    std::cout << "  -1 x6 stays within ABC: " << (scopedRev ? "PASS" : "FAIL") << "\n";

    // 3) Clear active net → stepping visits ALL 6 channels.
    ctrl.set_active_scan_net("");
    auto seq3 = stepN(+1, 8);   // >6 steps to guarantee a full cycle
    std::vector<uint32_t> allHz;
    for (int i = 0; i < 6; ++i) allHz.push_back(14000000 + i * 1000);
    bool visitsAll6 = visitsAll(seq3, allHz);
    std::cout << "  no net → all 6 channels visited: " << (visitsAll6 ? "PASS" : "FAIL") << "\n";

    return scoped && covers && scopedRev && visitsAll6;
}

// rename_channel() renames a channel id and propagates to net membership. It
// rejects empty/whitespace ids, collisions with an existing channel id, and
// unknown old_id. No-op when new == old. The GUI ID input rides this.
bool test_rename_channel_propagates_to_nets()
{
    std::cout << "\n[rename_channel] renames id + propagates to net membership\n";

    ALEController ctrl;
    ctrl.set_self_address("SAM");
    for (int i = 0; i < 3; ++i) {
        Channel ch(14000000 + i * 1000);
        ch.enabled = true;
        ctrl.add_channel(ch);
    }
    std::vector<std::string> ids;
    for (const auto& c : ctrl.channels()) ids.push_back(c.id);   // C-1, C-2, C-3
    ctrl.add_net("N1");
    ctrl.assign_channel_to_net("N1", ids[0]);
    ctrl.assign_channel_to_net("N1", ids[1]);

    auto netIds = [&](const std::string& name) {
        const Net* n = nullptr;
        for (const auto& nn : ctrl.nets()) if (nn.name == name) { n = &nn; break; }
        std::vector<std::string> v = n ? n->channel_ids : std::vector<std::string>{};
        std::sort(v.begin(), v.end());
        return v;
    };
    auto chanId = [&](int i){ return ctrl.channels()[i].id; };

    // 1) Rename C-2 → C-9: channel id changes, net N1 follows.
    bool ok1 = ctrl.rename_channel(ids[1], "C-9");
    bool idChanged = (chanId(1) == "C-9");
    std::vector<std::string> after = { "C-1", "C-9" };
    std::sort(after.begin(), after.end());
    bool netFollows = (netIds("N1") == after);
    std::cout << "  rename C-2→C-9: " << (ok1 && idChanged ? "PASS" : "FAIL") << "\n";
    std::cout << "  net N1 follows ({C-1,C-9}): " << (netFollows ? "PASS" : "FAIL") << "\n";

    // 2) Reject empty new_id.
    bool ok2 = ctrl.rename_channel("C-9", "");
    bool rejectEmpty = !ok2 && chanId(1) == "C-9";
    std::cout << "  reject empty new_id: " << (rejectEmpty ? "PASS" : "FAIL") << "\n";

    // 3) Reject whitespace new_id.
    bool ok3 = ctrl.rename_channel("C-9", "C 9");
    bool rejectWs = !ok3 && chanId(1) == "C-9";
    std::cout << "  reject whitespace new_id: " << (rejectWs ? "PASS" : "FAIL") << "\n";

    // 4) Reject collision with an existing channel id (C-1).
    bool ok4 = ctrl.rename_channel("C-9", "C-1");
    bool rejectDup = !ok4 && chanId(1) == "C-9";
    std::cout << "  reject collision with C-1: " << (rejectDup ? "PASS" : "FAIL") << "\n";

    // 5) Reject unknown old_id.
    bool ok5 = ctrl.rename_channel("NOPE", "C-7");
    bool rejectUnknown = !ok5;
    std::cout << "  reject unknown old_id: " << (rejectUnknown ? "PASS" : "FAIL") << "\n";

    // 6) No-op rename (new == old) returns true, id unchanged.
    bool ok6 = ctrl.rename_channel("C-9", "C-9");
    bool noop = ok6 && chanId(1) == "C-9";
    std::cout << "  no-op (new==old) true: " << (noop ? "PASS" : "FAIL") << "\n";

    return ok1 && idChanged && netFollows && rejectEmpty && rejectWs && rejectDup && rejectUnknown && noop;
}

// set_channel_enabled() toggles the scan-list membership flag; set_channel_mode()
// overrides RX/TX mode. Both reject unknown ids; set_channel_mode rejects empty.
bool test_controller_set_channel_enabled_and_mode()
{
    std::cout << "\n[set_channel_enabled / set_channel_mode] toggle + override\n";
    ALEController ctrl;
    ctrl.add_channel(Channel(14250000, 0, "USB", "USB"));
    ctrl.add_channel(Channel(7100000,  0, "USB", "USB"));

    auto findCh = [&](const std::string& id) -> const Channel* {
        for (const auto& c : ctrl.channels()) if (c.id == id) return &c;
        return nullptr;
    };

    bool okDisable = ctrl.set_channel_enabled("C-1", false);
    bool disabled  = okDisable && findCh("C-1") && !findCh("C-1")->enabled;
    std::cout << "  disable C-1: " << (disabled ? "PASS" : "FAIL") << "\n";

    bool noop = ctrl.set_channel_enabled("C-1", false);  // already disabled
    std::cout << "  re-disable is no-op true: " << (noop ? "PASS" : "FAIL") << "\n";

    ctrl.set_channel_enabled("C-1", true);
    bool reEnabled = findCh("C-1") && findCh("C-1")->enabled;
    std::cout << "  re-enable C-1: " << (reEnabled ? "PASS" : "FAIL") << "\n";

    bool unknownRejected = !ctrl.set_channel_enabled("NOPE", true);
    std::cout << "  unknown id rejected: " << (unknownRejected ? "PASS" : "FAIL") << "\n";

    bool okMode  = ctrl.set_channel_mode("C-2", "USB-D");
    bool modeCh  = okMode && findCh("C-2") && findCh("C-2")->rx_mode == "USB-D"
                   && findCh("C-2")->tx_mode == "USB-D";
    std::cout << "  override C-2 mode to USB-D: " << (modeCh ? "PASS" : "FAIL") << "\n";

    bool emptyRejected    = !ctrl.set_channel_mode("C-2", "");
    bool unknownModeRej   = !ctrl.set_channel_mode("NOPE", "USB-D");
    std::cout << "  empty mode rejected: " << (emptyRejected ? "PASS" : "FAIL") << "\n";
    std::cout << "  unknown id mode rejected: " << (unknownModeRej ? "PASS" : "FAIL") << "\n";

    return disabled && noop && reEnabled && unknownRejected && modeCh
        && emptyRejected && unknownModeRej;
}

bool test_channel_store_min_capacity_100()
{
    std::cout << "\n[ChannelStore] minimum capacity 100 entries, all fields present (AC-GEN-004-001)\n";

    ChannelStore store;
    for (int i = 0; i < 100; ++i) {
        Channel ch;
        ch.rx_frequency_hz = static_cast<uint32_t>(3000000 + i * 1000);
        ch.tx_frequency_hz = ch.rx_frequency_hz;
        ch.rx_mode = "USB";
        ch.tx_mode = "USB";
        bool ok = store.add_channel(ch);
        if (!ok) {
            std::cout << "  FAIL: add_channel rejected entry " << i << " before reaching 100\n";
            return false;
        }
    }
    bool full_at_100 = (store.size() == 100);
    std::cout << "  100 channels accepted: " << (full_at_100 ? "PASS" : "FAIL")
              << " (size=" << store.size() << ")\n";

    Channel extra;
    extra.rx_frequency_hz = 99000000;
    bool rejected = !store.add_channel(extra);
    std::cout << "  101st entry rejected at kCapacity: " << (rejected ? "PASS" : "FAIL") << "\n";

    const auto& first = store.all()[0];
    bool has_fields = (first.rx_frequency_hz == 3000000
                    && first.tx_frequency_hz == 3000000
                    && first.rx_mode == "USB");
    std::cout << "  Channel has rx_freq/tx_freq/mode fields: " << (has_fields ? "PASS" : "FAIL") << "\n";

    bool capacity_constant_ok = (ChannelStore::kCapacity == 100);
    std::cout << "  kCapacity == 100: " << (capacity_constant_ok ? "PASS" : "FAIL") << "\n";

    return full_at_100 && rejected && has_fields && capacity_constant_ok;
}

bool test_channel_store_duplicate_rejected()
{
    std::cout << "\n[ChannelStore] duplicate rx_frequency_hz rejected (AC-GEN-004-001)\n";

    ChannelStore store;
    bool first  = store.add_channel(Channel(7100000));
    bool second = !store.add_channel(Channel(7100000));
    bool size_1 = (store.size() == 1);

    std::cout << "  first add accepted: "    << (first  ? "PASS" : "FAIL") << "\n";
    std::cout << "  duplicate rejected: "    << (second ? "PASS" : "FAIL") << "\n";
    std::cout << "  size remains 1: "        << (size_1 ? "PASS" : "FAIL") << "\n";

    return first && second && size_1;
}

// ============================================================================
// ChannelStore — IPersistenceBackend (AC-GEN-004-002)
// ============================================================================

// Mock backend — captures save() call and replays it on load().
struct MockPersistenceBackend : public IPersistenceBackend {
    std::vector<Channel> stored;
    int  save_calls = 0;
    int  load_calls = 0;
    bool fail_save  = false;
    bool fail_load  = false;

    bool save(const std::vector<Channel>& ch) override {
        ++save_calls;
        if (fail_save) return false;
        stored = ch;
        return true;
    }
    bool load(std::vector<Channel>& ch) override {
        ++load_calls;
        if (fail_load) return false;
        ch = stored;
        return true;
    }
};

bool test_channel_store_persistence_interface()
{
    std::cout << "\n[ChannelStore] IPersistenceBackend interface — mock round-trip (AC-GEN-004-002)\n";

    ChannelStore store;
    Channel c1(14250000); c1.id = "C-1"; c1.rx_mode = "USB"; c1.tx_mode = "USB";
    Channel c2(7100000);  c2.id = "C-2"; c2.rx_mode = "USB"; c2.tx_mode = "USB";
    store.add_channel(c1);
    store.add_channel(c2);

    MockPersistenceBackend backend;

    bool saved = store.save(backend);
    bool save_called_once = (backend.save_calls == 1);
    bool stored_2 = (backend.stored.size() == 2);
    std::cout << "  save() returns true: " << (saved ? "PASS" : "FAIL") << "\n";
    std::cout << "  backend.save() called once: " << (save_called_once ? "PASS" : "FAIL") << "\n";
    std::cout << "  backend received 2 channels: " << (stored_2 ? "PASS" : "FAIL") << "\n";

    ChannelStore store2;
    bool loaded = store2.load(backend);
    bool load_called_once = (backend.load_calls == 1);
    bool restored_2 = (store2.size() == 2);
    bool ids_ok = (store2.all()[0].id == "C-1" && store2.all()[1].id == "C-2");
    std::cout << "  load() returns true: " << (loaded ? "PASS" : "FAIL") << "\n";
    std::cout << "  backend.load() called once: " << (load_called_once ? "PASS" : "FAIL") << "\n";
    std::cout << "  store2 has 2 channels after load: " << (restored_2 ? "PASS" : "FAIL") << "\n";
    std::cout << "  channel IDs preserved: " << (ids_ok ? "PASS" : "FAIL") << "\n";

    // Failure paths
    backend.fail_save = true;
    bool save_fail = !store.save(backend);
    backend.fail_save = false;
    backend.fail_load = true;
    bool load_fail = !store2.load(backend);
    std::cout << "  save() propagates backend failure: " << (save_fail ? "PASS" : "FAIL") << "\n";
    std::cout << "  load() propagates backend failure: " << (load_fail ? "PASS" : "FAIL") << "\n";

    return saved && save_called_once && stored_2
        && loaded && load_called_once && restored_2 && ids_ok
        && save_fail && load_fail;
}

bool test_channel_store_file_backend_round_trip()
{
    std::cout << "\n[ChannelStore] FileChannelBackend round-trip (AC-GEN-004-002)\n";

    const std::string path = "test_channel_store_persistence.ale";

    // ── save ──
    ChannelStore store;
    Channel c1(14250000, 0, "USB", "USB"); c1.id = "C-1"; c1.label = "40m-ALE";
    Channel c2(7100000,  0, "USB", "USB"); c2.id = "C-2";
    Channel c3(3500000, 3600000, "USB", "USB"); c3.id = "C-3";
    store.add_channel(c1);
    store.add_channel(c2);
    store.add_channel(c3);

    FileChannelBackend out_backend(path);
    bool saved = store.save(out_backend);
    std::cout << "  save() succeeds: " << (saved ? "PASS" : "FAIL") << "\n";

    // ── load into fresh store ──
    ChannelStore store2;
    FileChannelBackend in_backend(path);
    bool loaded = store2.load(in_backend);
    std::cout << "  load() succeeds: " << (loaded ? "PASS" : "FAIL") << "\n";

    bool count_ok = (store2.size() == 3);
    std::cout << "  3 channels restored: " << (count_ok ? "PASS" : "FAIL") << "\n";

    bool freq_ok = (store2.all()[0].rx_frequency_hz == 14250000
                 && store2.all()[1].rx_frequency_hz == 7100000
                 && store2.all()[2].rx_frequency_hz == 3500000
                 && store2.all()[2].tx_frequency_hz == 3600000);
    std::cout << "  frequencies survive round-trip: " << (freq_ok ? "PASS" : "FAIL") << "\n";

    bool id_ok = (store2.all()[0].id == "C-1"
               && store2.all()[1].id == "C-2"
               && store2.all()[2].id == "C-3");
    std::cout << "  IDs survive round-trip: " << (id_ok ? "PASS" : "FAIL") << "\n";

    bool label_ok = (store2.all()[0].label == "40m-ALE");
    std::cout << "  label survives round-trip: " << (label_ok ? "PASS" : "FAIL") << "\n";

    // ── load from non-existent file ──
    FileChannelBackend bad_backend("__nonexistent__.ale");
    bool bad_load = !store2.load(bad_backend);
    std::cout << "  load() from missing file returns false: " << (bad_load ? "PASS" : "FAIL") << "\n";

    std::remove(path.c_str());
    return saved && loaded && count_ok && freq_ok && id_ok && label_ok && bad_load;
}

// ============================================================================
// OtherStationStore — capacity (AC-GEN-006-001)
// ============================================================================

bool test_other_station_store_min_capacity_100()
{
    std::cout << "\n[OtherStationStore] minimum capacity 100 entries (AC-GEN-006-001)\n";

    OtherStationStore store;
    for (int i = 0; i < 100; ++i) {
        StationInfo info;
        info.address = "S" + std::to_string(i + 1);
        bool ok = store.add_station(info);
        if (!ok) {
            std::cout << "  FAIL: add_station() rejected entry " << i << " before reaching 100\n";
            return false;
        }
    }
    bool full_at_100 = (store.size() == 100);
    std::cout << "  100 entries accepted: " << (full_at_100 ? "PASS" : "FAIL")
              << " (size=" << store.size() << ")\n";

    StationInfo extra; extra.address = "S101";
    bool rejected = !store.add_station(extra);
    std::cout << "  101st entry rejected at kCapacity: " << (rejected ? "PASS" : "FAIL") << "\n";

    bool capacity_constant_ok = (OtherStationStore::kCapacity == 100);
    std::cout << "  kCapacity == 100: " << (capacity_constant_ok ? "PASS" : "FAIL") << "\n";

    // update_contact() on an existing entry must still work when store is full
    store.update_contact("S1", 9999);
    bool update_ok = (store.get("S1") && store.get("S1")->last_contact_ms == 9999);
    bool still_100 = (store.size() == 100);
    std::cout << "  update_contact on existing entry works when full: "
              << (update_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  size unchanged after update: " << (still_100 ? "PASS" : "FAIL") << "\n";

    // update_contact() on unknown address must NOT exceed kCapacity
    store.update_contact("NEWGUY", 1000);
    bool no_overflow = (store.size() == 100);
    std::cout << "  update_contact with unknown addr doesn't overflow kCapacity: "
              << (no_overflow ? "PASS" : "FAIL") << "\n";

    return full_at_100 && rejected && capacity_constant_ok && update_ok && still_100 && no_overflow;
}

// ============================================================================
// OperatingParameters — operator-programmable setters (AC-GEN-007-001)
// ============================================================================

bool test_operating_parameters_setters_ac_gen_007_001()
{
    std::cout << "\n[OperatingParameters] operator-programmable setters (AC-GEN-007-001)\n";

    OperatingParameters p;

    // ── defaults ────────────────────────────────────────────────────────────
    bool def_scan = (p.scan_dwell_ms == static_cast<uint32_t>(ale::TD2_MS));
    std::cout << "  default scan_dwell_ms == TD2_MS: " << (def_scan ? "PASS" : "FAIL") << "\n";

    bool def_timeout = (p.call_timeout_ms == ale::Twa_ms);
    std::cout << "  default call_timeout_ms == Twa_ms: " << (def_timeout ? "PASS" : "FAIL") << "\n";

    bool def_flags = (p.lqa_enabled && p.amd_enabled && p.lbt_enabled);
    std::cout << "  default accept-flags all true: " << (def_flags ? "PASS" : "FAIL") << "\n";

    // ── scan_dwell_ms setter ────────────────────────────────────────────────
    bool scan_valid = p.set_scan_dwell_ms(1000);
    std::cout << "  set_scan_dwell_ms(1000) returns true: " << (scan_valid ? "PASS" : "FAIL") << "\n";

    bool scan_applied = (p.scan_dwell_ms == 1000u);
    std::cout << "  scan_dwell_ms updated to 1000: " << (scan_applied ? "PASS" : "FAIL") << "\n";

    bool scan_reject = !p.set_scan_dwell_ms(100); // below TD2_MS floor
    std::cout << "  set_scan_dwell_ms(100) rejected (< TD2_MS): " << (scan_reject ? "PASS" : "FAIL") << "\n";

    bool scan_unchanged = (p.scan_dwell_ms == 1000u); // must not have been altered
    std::cout << "  scan_dwell_ms unchanged after rejection: " << (scan_unchanged ? "PASS" : "FAIL") << "\n";

    // ── sounding_period_ms setter ───────────────────────────────────────────
    p.set_sounding_period_ms(300000);
    bool sounding_ok = (p.sounding_period_ms == 300000u);
    std::cout << "  set_sounding_period_ms(300000): " << (sounding_ok ? "PASS" : "FAIL") << "\n";

    // ── call_timeout_ms setter ──────────────────────────────────────────────
    bool timeout_valid = p.set_call_timeout_ms(60000);
    std::cout << "  set_call_timeout_ms(60000) returns true: " << (timeout_valid ? "PASS" : "FAIL") << "\n";

    bool timeout_applied = (p.call_timeout_ms == 60000u);
    std::cout << "  call_timeout_ms updated to 60000: " << (timeout_applied ? "PASS" : "FAIL") << "\n";

    bool timeout_reject = !p.set_call_timeout_ms(1000); // below Twa_ms floor
    std::cout << "  set_call_timeout_ms(1000) rejected (< Twa_ms): " << (timeout_reject ? "PASS" : "FAIL") << "\n";

    bool timeout_unchanged = (p.call_timeout_ms == 60000u);
    std::cout << "  call_timeout_ms unchanged after rejection: " << (timeout_unchanged ? "PASS" : "FAIL") << "\n";

    // ── amd_max_length setter ───────────────────────────────────────────────
    bool amd_len_valid = p.set_amd_max_length(64);
    std::cout << "  set_amd_max_length(64) returns true: " << (amd_len_valid ? "PASS" : "FAIL") << "\n";

    bool amd_len_applied = (p.amd_max_length == 64u);
    std::cout << "  amd_max_length updated to 64: " << (amd_len_applied ? "PASS" : "FAIL") << "\n";

    bool amd_len_reject = !p.set_amd_max_length(91); // exceeds 90-char spec cap
    std::cout << "  set_amd_max_length(91) rejected (> 90): " << (amd_len_reject ? "PASS" : "FAIL") << "\n";

    bool amd_len_unchanged = (p.amd_max_length == 64u);
    std::cout << "  amd_max_length unchanged after rejection: " << (amd_len_unchanged ? "PASS" : "FAIL") << "\n";

    // ── accept-flags ────────────────────────────────────────────────────────
    p.set_lqa_enabled(false);
    bool lqa_off = !p.lqa_enabled;
    std::cout << "  set_lqa_enabled(false): " << (lqa_off ? "PASS" : "FAIL") << "\n";

    p.set_lqa_enabled(true);
    bool lqa_on = p.lqa_enabled;
    std::cout << "  set_lqa_enabled(true): " << (lqa_on ? "PASS" : "FAIL") << "\n";

    p.set_amd_enabled(false);
    bool amd_off = !p.amd_enabled;
    std::cout << "  set_amd_enabled(false): " << (amd_off ? "PASS" : "FAIL") << "\n";

    p.set_lbt_enabled(false);
    bool lbt_off = !p.lbt_enabled;
    std::cout << "  set_lbt_enabled(false): " << (lbt_off ? "PASS" : "FAIL") << "\n";

    // ── lbt_listen_ms setter ────────────────────────────────────────────────
    p.set_lbt_listen_ms(2000);
    bool lbt_ms_ok = (p.lbt_listen_ms == 2000u);
    std::cout << "  set_lbt_listen_ms(2000): " << (lbt_ms_ok ? "PASS" : "FAIL") << "\n";

    return def_scan && def_timeout && def_flags
        && scan_valid && scan_applied && scan_reject && scan_unchanged
        && sounding_ok
        && timeout_valid && timeout_applied && timeout_reject && timeout_unchanged
        && amd_len_valid && amd_len_applied && amd_len_reject && amd_len_unchanged
        && lqa_off && lqa_on && amd_off && lbt_off
        && lbt_ms_ok;
}

// ============================================================================
// MessageStore — minimum capacity (AC-GEN-008-001)
// ============================================================================

bool test_message_store_min_capacity_ac_gen_008_001()
{
    std::cout << "\n[MessageStore] minimum 12 messages / 1000 chars capacity (AC-GEN-008-001)\n";

    // ── compile-time constants ─────────────────────────────────────────────
    static_assert(MessageStore::kMinMessages >= 12,
                  "kMinMessages must be >= 12 (REQ-GEN-020)");
    static_assert(MessageStore::kMinChars >= 1000,
                  "kMinChars must be >= 1000 (REQ-GEN-020)");

    bool const_msgs_ok = (MessageStore::kMinMessages == 12);
    bool const_chars_ok = (MessageStore::kMinChars == 1000);
    std::cout << "  kMinMessages == 12: " << (const_msgs_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  kMinChars == 1000: "  << (const_chars_ok ? "PASS" : "FAIL") << "\n";

    // ── default store holds >= kMinMessages entries ────────────────────────
    MessageStore store;
    for (size_t i = 0; i < MessageStore::kMinMessages; ++i) {
        ALEMessage msg;
        msg.from_address = "S" + std::to_string(i);
        msg.complete = true;
        store.push(msg);
    }
    bool min_msgs_accepted = (store.size() == MessageStore::kMinMessages);
    std::cout << "  " << MessageStore::kMinMessages
              << " messages accepted by default store: "
              << (min_msgs_accepted ? "PASS" : "FAIL") << "\n";

    // ── constructor with capacity < kMinMessages clamps up ─────────────────
    MessageStore tiny(1);   // request 1, must silently clamp to kMinMessages
    for (size_t i = 0; i < MessageStore::kMinMessages; ++i) {
        ALEMessage msg;
        msg.from_address = "T" + std::to_string(i);
        msg.complete = true;
        tiny.push(msg);
    }
    bool clamp_ok = (tiny.size() == MessageStore::kMinMessages);
    std::cout << "  capacity clamped to kMinMessages when requested < kMinMessages: "
              << (clamp_ok ? "PASS" : "FAIL") << "\n";

    // ── total character capacity >= kMinChars for default store ───────────
    // Each AMD message can hold up to 90 chars; default capacity 64 messages
    // gives 64 * 90 = 5760 >> 1000.  Verify design invariant at kMinMessages:
    // kMinMessages * 90-char AMD = 12 * 90 = 1080 >= kMinChars.
    const size_t chars_per_msg = 90; // AMD max length per spec
    bool total_chars_ok = (MessageStore::kMinMessages * chars_per_msg >= MessageStore::kMinChars);
    std::cout << "  kMinMessages * 90 chars >= kMinChars (" << MessageStore::kMinChars << "): "
              << (total_chars_ok ? "PASS" : "FAIL") << "\n";

    // ── ring-buffer eviction still works after filling beyond kMinMessages ─
    MessageStore ring(MessageStore::kMinMessages);
    for (size_t i = 0; i < MessageStore::kMinMessages + 3; ++i) {
        ALEMessage msg;
        msg.from_address = "R" + std::to_string(i);
        msg.complete = true;
        ring.push(msg);
    }
    bool evict_size_ok = (ring.size() == MessageStore::kMinMessages);
    bool evict_oldest_ok = (ring.all().front().from_address == "R3"); // first 3 evicted
    std::cout << "  ring eviction keeps size at kMinMessages: "
              << (evict_size_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  oldest entry evicted (R3 is new front): "
              << (evict_oldest_ok ? "PASS" : "FAIL") << "\n";

    return const_msgs_ok && const_chars_ok && min_msgs_accepted
        && clamp_ok && total_chars_ok && evict_size_ok && evict_oldest_ok;
}

// ── MessageStore save/load persistence (AC-GEN-008-002) ──────────────────────

bool test_message_store_retention_ac_gen_008_002()
{
    std::cout << "\n[MessageStore] 1h power-loss retention via save/load (AC-GEN-008-002)\n";

    // kMinRetentionMs must be at least 1 hour in milliseconds
    static_assert(MessageStore::kMinRetentionMs >= 3600000u,
                  "kMinRetentionMs must be >= 3600000 ms (1 hour)");

    bool retention_const_ok = (MessageStore::kMinRetentionMs == 3600000u);
    std::cout << "  kMinRetentionMs == 3600000u: "
              << (retention_const_ok ? "PASS" : "FAIL") << "\n";

    // Build a store with kMinMessages messages of varying types/addresses
    MessageStore store;
    const size_t N = MessageStore::kMinMessages;
    for (size_t i = 0; i < N; ++i) {
        ALEMessage msg;
        msg.call_type    = (i % 2 == 0) ? CallType::INDIVIDUAL : CallType::AMD;
        msg.from_address = "SRC" + std::to_string(i);
        msg.to_addresses.push_back("DST" + std::to_string(i));
        msg.data_content.push_back("PAYLOAD" + std::to_string(i));
        msg.start_time_ms = static_cast<uint32_t>(i * 1000);
        msg.duration_ms   = 250;
        msg.complete      = true;

        // add one word so word serialization is exercised
        ALEWord w;
        w.type           = PreambleType::FROM;
        w.raw_payload    = static_cast<uint32_t>(0xABC + i);
        w.fec_errors     = static_cast<uint8_t>(i % 4);
        w.unanimous_votes = 48;
        w.valid          = true;
        w.timestamp_ms   = static_cast<uint32_t>(i * 500);
        std::snprintf(w.address, 4, "%c%c%c",
                      static_cast<char>('A' + i % 26),
                      static_cast<char>('A' + (i+1) % 26),
                      static_cast<char>('A' + (i+2) % 26));
        msg.words.push_back(w);

        store.push(msg);
    }
    bool filled_ok = (store.size() == N);
    std::cout << "  store filled with " << N << " messages: "
              << (filled_ok ? "PASS" : "FAIL") << "\n";

    // Save to a temp file
    const std::string tmpfile = "test_msgstore_tmp.bin";
    bool save_ok = store.save_to_file(tmpfile);
    std::cout << "  save_to_file succeeded: "
              << (save_ok ? "PASS" : "FAIL") << "\n";

    // Clear and reload — simulates power cycle
    store.clear();
    bool cleared_ok = store.empty();
    std::cout << "  store cleared before reload: "
              << (cleared_ok ? "PASS" : "FAIL") << "\n";

    bool load_ok = store.load_from_file(tmpfile);
    std::cout << "  load_from_file succeeded: "
              << (load_ok ? "PASS" : "FAIL") << "\n";

    // Verify count
    bool count_ok = (store.size() == N);
    std::cout << "  message count restored (" << N << "): "
              << (count_ok ? "PASS" : "FAIL") << "\n";

    // Verify message fields round-tripped correctly
    bool fields_ok = true;
    for (size_t i = 0; i < store.size(); ++i) {
        const ALEMessage& m = store.all()[i];
        CallType expected_ct = (i % 2 == 0) ? CallType::INDIVIDUAL : CallType::AMD;
        std::string expected_from = "SRC" + std::to_string(i);
        std::string expected_to   = "DST" + std::to_string(i);
        std::string expected_data = "PAYLOAD" + std::to_string(i);
        if (m.call_type != expected_ct ||
            m.from_address != expected_from ||
            m.to_addresses.empty() || m.to_addresses[0] != expected_to ||
            m.data_content.empty() || m.data_content[0] != expected_data ||
            m.start_time_ms != static_cast<uint32_t>(i * 1000) ||
            m.duration_ms   != 250 ||
            !m.complete) {
            fields_ok = false;
            break;
        }
        // verify word
        if (m.words.size() != 1 ||
            m.words[0].type != PreambleType::FROM ||
            m.words[0].raw_payload != static_cast<uint32_t>(0xABC + i) ||
            m.words[0].fec_errors  != static_cast<uint8_t>(i % 4) ||
            !m.words[0].valid) {
            fields_ok = false;
            break;
        }
    }
    std::cout << "  all message fields round-tripped correctly: "
              << (fields_ok ? "PASS" : "FAIL") << "\n";

    // Verify load rejects corrupt magic
    bool bad_magic_ok = false;
    {
        std::ofstream bad("test_msgstore_bad.bin", std::ios::binary);
        bad.write("GARBAGE123", 10);
        bad.close();
        MessageStore s2;
        bad_magic_ok = !s2.load_from_file("test_msgstore_bad.bin");
        std::remove("test_msgstore_bad.bin");
    }
    std::cout << "  corrupt file correctly rejected: "
              << (bad_magic_ok ? "PASS" : "FAIL") << "\n";

    std::remove(tmpfile.c_str());

    return retention_const_ok && filled_ok && save_ok && cleared_ok
        && load_ok && count_ok && fields_ok && bad_magic_ok;
}

int run_all_tests()
{
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  Channel-ID / Net / Contact / SelfAddress store tests\n";
    std::cout << "================================================================\n";

    int pass_count = 0;
    int fail_count = 0;

    auto run = [&](const char* name, bool result) {
        if (result) { ++pass_count; }
        else        { ++fail_count; std::cout << "  *** FAILED: " << name << "\n"; }
    };

    run("NetStore add/remove/find",                test_net_store_add_remove_find());
    run("NetStore assign/unassign channel",         test_net_store_assign_unassign_channel());
    run("NetStore unassign_channel_everywhere",     test_net_store_unassign_everywhere());
    run("net_scan_channel_count counts enabled only", test_net_scan_channel_count());

    run("ChannelStore min capacity 100 + fields",  test_channel_store_min_capacity_100());
    run("ChannelStore duplicate rejected",         test_channel_store_duplicate_rejected());
    run("ChannelStore IPersistenceBackend mock round-trip", test_channel_store_persistence_interface());
    run("ChannelStore FileChannelBackend round-trip",       test_channel_store_file_backend_round_trip());

    run("ContactStore add/update/remove",           test_contact_store_add_update_remove());

    run("SelfAddressStore primary defaults to first", test_self_address_store_primary_defaults_to_first());
    run("SelfAddressStore set_primary/remove",      test_self_address_store_set_primary_and_remove());
    run("SelfAddressStore min capacity 20 + fields (AC-GEN-005-001)",
        test_self_address_store_min_capacity_20());

    run("OtherStationStore min capacity 100 (AC-GEN-006-001)",
        test_other_station_store_min_capacity_100());

    run("OperatingParameters operator-programmable setters (AC-GEN-007-001)",
        test_operating_parameters_setters_ac_gen_007_001());

    run("MessageStore min 12 messages / 1000 chars (AC-GEN-008-001)",
        test_message_store_min_capacity_ac_gen_008_001());

    run("MessageStore 1h power-loss retention via save/load (AC-GEN-008-002)",
        test_message_store_retention_ac_gen_008_002());

    run("ALEController channel-ID auto-assignment", test_controller_channel_id_auto_assignment());
    run("ALEController del_channel unassigns from nets", test_controller_del_channel_unassigns_from_nets());

    run(".ale round-trip: IDs and nets survive",    test_ale_roundtrip_ids_and_nets());
    run(".ale legacy file still loads",             test_ale_legacy_file_without_ids_still_loads());
    run(".ale round-trip: inhibit flags + rx_only/tx_only", test_ale_roundtrip_inhibit_flags());
    run(".ale bracketed non-flag label kept",       test_ale_bracketed_label_not_swallowed());

    run("initiate_call uses net's calling_length_c (global assumed is fallback)",
        test_initiate_call_uses_net_calling_length_c());
    run("initiate_call without contact leaves target_scan_channels untouched",
        test_initiate_call_without_contact_leaves_target_scan_channels_untouched());
    run("initiate_call scopes outbound channels to the active scan net",
        test_initiate_call_scoped_to_active_scan_net());
    run("Direction=RX: initiate_call rejected when all channels are RX-only",
        test_initiate_call_rx_only_channel_rejected());
    run("step_channel iterates only the active net's channels",
        test_step_channel_scoped_to_active_scan_net());
    run("rename_channel renames id + propagates to net membership",
        test_rename_channel_propagates_to_nets());
    run("set_channel_enabled / set_channel_mode toggle + override",
        test_controller_set_channel_enabled_and_mode());

    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  Results — Passed: " << pass_count << "  Failed: " << fail_count << "\n";
    std::cout << "================================================================\n\n";

    return (fail_count == 0) ? 0 : 1;
}

} // namespace ale

int main()
{
    return ale::run_all_tests();
}
