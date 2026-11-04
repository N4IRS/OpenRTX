> **SUPERSEDED 2026-10-28:** the binary record design below was replaced by the adopted **ptSCD** standard. See [`ptscd-notes.md`](ptscd-notes.md) for the current record sizes, capacity and open questions.

# CPS storage budget and per-mode fields

Status: design budget, 2026-10-28. Measured against the built C62 ELF.
Supersedes earlier figures in this file that used the stale `// 59B` comment
and that did not charge name storage to the pool.

## Measured baseline

| Struct | Size | Notes |
|---|---|---|
| `channel_t` | **94 B** | mode union at offset 89, max member 5 B |
| `contact_t` | **39 B** | `name[32]` + mode + 6 B union |
| `settings_t` | **84 B** | global, not per channel |
| `cps_header_t` | **88 B** | one per codeplug |

`channel_t` offsets: `mode` 0, flags 1, `power` 2, `rx_frequency` 6,
`tx_frequency` 10, `scanList_index` 14, `groupList_index` 15, `name` 16,
`descr` 48, `ch_location` 80, mode union 89.

## Design decisions

1. **16 B mode block** per channel (largest mode, TETRA, needs 9 B).
   Replaces the 5 B union; keeps `sizeof(channel_t)` fixed forever.
2. **Names move to a shared pool.** Talkgroup names are **required**: users
   read the name on the display, not the number. `name_index = 0xFFFF` remains
   as a sentinel for a genuinely missing name (imported data), never as the
   default for talkgroups.
3. **`descr[32]` is dropped** from the channel record — 32 B of every
   channel for a field most codeplugs never populate.
4. **Talkgroups are contacts** with `call_type = group`. One table, one
   address space, one name pool.
5. **Contacts never take space from the app or DSP partitions.** Firmware
   headroom is not negotiable.
6. **A named reserve is kept** inside `storage_partition` for future data
   features.
7. **Target capacity: 10,000 contacts/talkgroups, all named.**

## Per-mode fields

Only per-channel data. Source IDs, callsigns, APRS beacon timing and squelch
stay in `settings_t`.

| Mode | Fields | Bytes |
|---|---|---|
| **FM** | `rxToneEn` 1, `rxTone` 7, `txToneEn` 1, `txTone` 7 | **2** |
| **DMR** | `contact_index` 16, colour code 4, timeslot 1, group/private 1, emergency 1, spare 1 | **3** |
| **M17** | `rxCan` 4, `txCan` 4, mode 2, encryption 2, GPS 1, spare 3, `contact_index` 16 | **4** |
| **P25** | NAC 12, phase 1, spare 3, `contact_index` 16, RX group list 16, encrypted 1, algid 7, key id 8 | **8** |
| **APRS** | beacon enable 1, path index 8, spare 7 | **2** |
| **NXDN** | `contact_index` 16, RAN 6, group/private 1, encrypted 1, emergency 1, spare 7 | **4** |
| **TETRA** | MCC 10, MNC 14, group/individual 1, encryption class 3, slot 2, priority 3, SSI 24, spare 7 | **9** |

DCS, if ever added to FM, costs ~3 B more — still inside 16 B.

## Record layouts

### Channel — 43 B

| Field | Bytes | Notes |
|---|---|---|
| `mode` | 1 | FM / DMR / M17 / P25 / APRS / NXDN / TETRA |
| flags | 1 | bandwidth, rx_only |
| `power` | 4 | mW |
| `rx_frequency` | 4 | Hz |
| `tx_frequency` | 4 | Hz |
| `scanList_index` | 1 | |
| `groupList_index` | 1 | |
| `name_index` | 2 | into the name pool |
| `ch_location` | 9 | geolocation, kept |
| mode info block | 16 | per-mode union, zero-filled |
| **total** | **43** | |

### Contact / talkgroup — 10 B

| Field | Bits | Notes |
|---|---|---|
| `name_index` | 16 | into the name pool; **`0xFFFF` = unnamed, display the address** |
| `addr_type` | 4 | DMR / M17 / P25 / NXDN / TETRA |
| `call_type` | 2 | private (unit), group, all-call |
| `flags` | 2 | emergency, reserved |
| `address` | 48 | 6 B; M17 encoded address, 24-bit IDs in the low bits |
| spare | 8 | second flag byte |

Names are **required for talkgroups** — the display shows the name, not the
number. `0xFFFF` exists only for entries whose name is genuinely unknown
(imported data), and the UI then falls back to the numeric address.

### Zone — 18 B + 2 B per member channel

### Name pool

Variable length: `uint8 len` + `char[len]`, max 32 chars.

**This must be charged to capacity** — it is where the bytes went when the
inline array was removed.

## Effective cost per record (names included)

Names are mandatory in practice, so both records carry one. An 8-character
average name costs 9 B in the pool (1 length byte + 8 chars):

| Record | Struct | Name | Effective |
|---|---|---|---|
| Channel | 43 B | 9 B | **52 B** |
| Contact / talkgroup | 10 B | 9 B | **19 B** |
| Contact, name missing (sentinel) | 10 B | — | 10 B |

Talkgroup names in the field are often shorter than 8 characters (`TAC310`
is 6), so 17–19 B per entry is the realistic range.

## Capacity — 10,000-entry target

```
codeplug = 88 + 52×channels + contacts + zones(18 + 2×ch)
```

### 4,000 channels, 250 zones, 10,000 named contacts

| Name length | Contact cost | Total |
|---|---|---|
| 6 chars (7 B) | 170 KB | 390.6 KB |
| 8 chars (9 B) | 190 KB | **410.6 KB** |
| 12 chars (13 B) | 230 KB | 450.6 KB |

So **10,000 named contacts need ~400 KiB**, which rules out a 384 KiB
partition and makes **448 KiB the realistic minimum**.

### Capacity by partition

| Partition | 10,000 named | Max named entries |
|---|---|---|
| 384 KiB | ❌ (410.6 KB needed) | ~9,000 |
| **448 KiB** | ✅ 47 KiB spare | ~10,800 |
| 632 KiB | ✅ generous | ~16,000 |

### The 500,000-contact claim

At 19 B per named entry that is **9.5 MB**. The C62 has 4 MB total, 2 MB of
which is app + DSP. Not reachable under any encoding. Those figures come from
radios with far more flash, or count a compressed searchable DMR ID database.

## Partition map

Current flash:

```
0x000000  1 MiB    app            (~325 KiB used, ~700 KiB free)
0x100000  1 MiB    DSP image      (~456 KiB used)
0x200000  720 KiB  factory_res0   read-only, OEM
0x2B0000  640 KiB  storage        settings + codeplug
0x350000  384 KiB  factory_res1   read-only, OEM
0x3B0000   64 KiB  factory_cal    read-only, per-radio calibration
0x3C0000  256 KiB  factory_chan   read-only, OEM channel store
```

Recommended split:

```
storage_partition  0x2B0000    8 KiB    settings + VFO (existing layout)
cps_partition      0x2B2000  448 KiB    codeplug, 10,000 named contacts (0x70000)
reserve            0x322000  184 KiB    unallocated, future features
```

Firmware headroom is untouched: the app partition keeps its ~700 KiB and the
DSP image its ~568 KiB.

If a larger reserve is preferred, the lever is the channel record, not the
contact names: moving `ch_location` (9 B) into an optional pool saves 36 KiB
across 4,000 channels.

## Why not take the whole storage partition

632 KiB would give ~22,500 entries, but leaves nothing for backups, logs,
recorded messages or future settings. The reserve is cheap to keep and cannot
be recovered later without a repartition.

## Open items

- Average name length is assumed at 8 chars; talkgroup names are often 6
  (`TAC310`). The name pool should be a measured region, not an estimate.
- Channel `ch_location` (9 B) could move to an optional pool, saving 36 KiB
  across 4,000 channels if more reserve is wanted.
- Field layouts for DMR / NXDN / TETRA are reservations: no firmware consumer
  exists yet, and both the DMR PoC and P25 work keep their configuration in
  global settings rather than per channel.
