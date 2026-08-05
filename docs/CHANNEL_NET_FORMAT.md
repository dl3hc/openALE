# Channel, Net, and Contact Management

Channels control the frequency (and mode) on which openALE transmits and receives.
Without configured Channels, the radio remains on the frequency it was on before startup.
Nets group Channel IDs to automatically determine the appropriate Scanning-Call length when calling a remote station; Contacts link an address book callsign with a Net.

## Management via the WebSocket Bridge (JSON)

These commands apply to `ALEController` (`apps/ale_bridge.cpp`); the GUI (Desktop + Mobile)
uses exactly the same API.

**Channels:**

| Command                          | Description                                                                                                                                                                                               |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CHANNELS_LIST`                  | Query current Channel list                                                                                                                                                                                |
| `CHANNEL_ADD`                    | Add/overwrite Channel — fields: `rx_hz`, `tx_hz`, `mode`, `id`, `label`, `enabled`, `rx_only`, `tx_only`, `voice_use`, `data_use`, `inhibit_calling`, `inhibit_sounding`, `inhibit_reporting`, `ale_only` |
| `CHANNEL_DEL`                    | Remove Channel by `rx_hz`                                                                                                                                                                                 |
| `CHANNEL_RENAME`                 | Rename Channel ID (`old_id`, `new_id`)                                                                                                                                                                    |
| `STATION_LOAD` / `CHANNELS_LOAD` | Load station file (`path`)                                                                                                                                                                                |
| `STATION_SAVE` / `CHANNELS_SAVE` | Write station file (`path`)                                                                                                                                                                               |

**Nets:**

| Command                         | Description                                                                                                             |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `NETS_LIST`                     | Query all Nets with assigned Channels                                                                                   |
| `NET_ADD`                       | Add Net (`name`)                                                                                                        |
| `NET_DEL`                       | Remove Net (`name`)                                                                                                     |
| `NET_ASSIGN`                    | Assign Channel ID to a Net (`net`, `channel_id`)                                                                        |
| `NET_UNASSIGN`                  | Remove Channel ID from a Net (`net`, `channel_id`)                                                                      |
| `NET_UPDATE`                    | Set Net policy: `name`, `dwell_ms`, `scanning_enabled`, `sounding_enabled`, `sounding_interval_sec`, `calling_length_c` |
| `SCAN_NET_SET` / `SCAN_NET_GET` | Set/query active Scan Net                                                                                               |

**Contacts (Address Book):**

| Command          | Description                                       |
| ---------------- | ------------------------------------------------- |
| `CONTACTS_LIST`  | Query address book                                |
| `CONTACT_ADD`    | Add contact (`callsign`, `name`)                  |
| `CONTACT_DEL`    | Remove contact (`callsign`)                       |
| `CONTACT_SELECT` | Select contact as active call target (`callsign`) |

A Net determines the Scanning-Call length when calling a remote station:
`Tsc = C × 2 × Trw`, where `C` (`calling_length_c`) is the number of **scan-capable** Channels of the Net to which the remote station belongs according to the address book. If the remote station is not in the address book or is not assigned to a Net, the previously configured Scanning-Call length (default: 1 Channel) remains unchanged.

## Non-Volatile Storage (`.ale` file format)

Channels, Nets, and Contacts are stored together in a text file with the `.ale` extension
(see `save_station_file()` / `load_station_file()` in
[ale_controller.cpp](../src/App/ale_controller.cpp)) — one entry per line, comments
begin with `#`.

### Channel Line

```
[ID:id] rx_hz tx_hz mode [flags] [label]
```

| Field     | Description                                                                                                                                                                     |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ID:id`   | Channel ID (optional when loading — old files without IDs automatically receive sequential `C-<n>` IDs; always written when saving; does not change when the frequency changes) |
| `rx_hz`   | RX frequency in Hz (required), or `rx_hz:tx_hz` (short form)                                                                                                                    |
| `tx_hz`   | TX frequency in Hz (0 or missing = simplex operation)                                                                                                                           |
| `mode`    | Modulation type: `USB` (default), `LSB`, `AM`, `FM`, `FMW`, `CWU`, `CWL`, `FSK`, `DATA_USB`, …                                                                                  |
| `[flags]` | Optional comma-separated flag token directly after `mode` (only written if a flag differs from the default)                                                                     |
| `label`   | Free-text label (optional, arbitrary length)                                                                                                                                    |

Flag codes:

| Code  | Meaning                                                                      |
| ----- | ---------------------------------------------------------------------------- |
| `OFF` | `enabled=false` (Channel disabled)                                           |
| `RX`  | `rx_only` (receive only, no TX)                                              |
| `TX`  | `tx_only` (transmit only, no RX)                                             |
| `IC`  | `inhibit_calling`                                                            |
| `IS`  | `inhibit_sounding`                                                           |
| `IR`  | `inhibit_reporting` (bilateral LQA CMD-'a' exchange)                         |
| `AO`  | `ale_only` — short LBT window according to A.5.4.7.1 (784 ms instead of 2 s) |

**Example:**

```
# openALE station file — MIL-STD-188-141B
# ID:id rx_hz tx_hz mode [flags] [label]   flags=[OFF,RX,IC,IS,IR]
ID:C-1 14250000 0 USB 40m-Calling
ID:C-2 7100000 0 USB [IC] 40m-Backup
ID:C-3 3500000 3600000 LSB [IS,IR,RX] 80m-DX
ID:C-4 14074000 0 DATA_USB FT8
```

### Net Line

```
NET:name id,id,...  dwell=ms scan=0|1 sound=0|1 sndint=sec c=N
```

```
# NET:name id,id,...  [dwell=ms] [scan=0|1] [sound=0|1] [sndint=sec] [c=N]
NET:XYZ C-1,C-3 dwell=200 scan=1 sound=0 sndint=300 c=2
```

| Field    | Description                                                                       |
| -------- | --------------------------------------------------------------------------------- |
| `dwell`  | Scan dwell time per Channel in ms                                                 |
| `scan`   | Scanning enabled for this Net                                                     |
| `sound`  | Auto-Sounding enabled for this Net                                                |
| `sndint` | Sounding interval in seconds                                                      |
| `c`      | `calling_length_c` — number of scan-capable Channels for the Scanning-Call length |

### Contact Line

```
CONTACT:callsign|name|status|net_members|valid_channels
```

```
# CONTACT:callsign|name|status|net_members|valid_channels
CONTACT:BOB|Bob K1ABC|enabled|XYZ|ALL
```

`status` is `enabled`/`disabled`; `net_members` is a comma-separated list of Net names;
`valid_channels` is either `ALL` or a comma-separated list of Channel IDs.

### Loading/Saving

With the `STATION_LOAD`/`STATION_SAVE` command (or during startup via the GUI onboarding flow),
Channels, Nets, and Contacts are fully restored or written. Changes made via
`CHANNEL_ADD`/`CHANNEL_DEL`/`NET_*`/`CONTACT_*` are not automatically written back to a file —
an explicit `STATION_SAVE` is required for that.
