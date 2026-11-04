# CHIRP / CPS integration — feasibility assessment

Status: **assessment only, no code written.**
Date: 2026-10-28. Target: Retevis C62 running OpenRTX (`Retevis-c62` upstream).

## Verdict (initial assessment — superseded, see reassessment below)

CHIRP is a viable front end, but **the C62 currently has no codeplug layer at
all** — that, not CHIRP, is the real work. Two integration models exist:

- **Offline (Option A):** firmware codeplug storage + a CHIRP CSV converter.
  Low risk, no new protocol, delivers channel management with a
  convert-and-flash workflow.
- **Live (Option B/B'):** a programming protocol plus a CHIRP driver, giving
  true clone-in/clone-out. Substantially more work; USB is the risky path,
  UART is the safer one.

Recommendation: **Option A first.** It is a prerequisite for either live
route anyway, and it ships value quickly.

## Reassessment (2026-10-28): qdmr, not CHIRP, is the intended CPS

After checking the wider ecosystem, CHIRP is **not** the right foundation for
a full CPS on OpenRTX. The evidence:

1. **OpenRTX issue #158** ("Support OpenRTX-defined Codeplug") defines the
   `.rtxc` format explicitly so that M17 and DMR channels can be represented,
   which the OEM codeplugs cannot. It names
   [qdmr](https://github.com/hmatuschek/qdmr) as the intended CPS consumer and
   asks for the format to be documented so other CPS projects can adopt it.
2. **qdmr already speaks `.rtxc`.** `lib/openrtx_codeplug.cc` implements
   encode/decode with `MagicNumber = 0x43585452` and `SupportedVersion = 0.1`
   — identical to `cps.h` (`CPS_MAGIC 0x43585452`, `CPS_VERSION_MAJOR 0`,
   `CPS_VERSION_MINOR 1`).
3. **qdmr already has M17 as a first-class channel type.** `M17Channel`,
   M17 contacts, `M17RadioID` and GUI dialogs all exist (qdmr #251).
4. **qdmr defines the live protocol too** — "rtxlink" (documented at
   openrtx.org/#/rtxlink): SLIP framing over a serial port, multiplexing
   stdio, CAT, FMP (file management) and XMODEM. No custom driver would be
   needed, and nothing to upstream into a third-party project.
5. CHIRP has no M17 mode at all, and its generic CSV cannot express contacts,
   so M17 would always be a comment-encoded hack there.

### What is missing on our side

| Piece | Status |
|---|---|
| `.rtxc` codeplug format | ✅ `core/cps.h` |
| C62 codeplug storage | ❌ `cps_io_stub.c` |
| `.rtxc` file import/export | ⚠️ needs only a flash write path |
| rtxlink on the device (live programming) | ❌ not implemented anywhere in the firmware |
| XMODEM transport | ✅ `core/xmodem.c` (portable) |
| Codeplug dump/restore helper | ⚠️ `core/backup.c` exists but is hard-coded to W25Qx and unwired |
| USB CDC-ACM | ⚠️ `cdc_acm.c` present in the CSK6 Zephyr fork; `usbotg` disabled |

`backup.c` is the interesting precedent: `eflash_dump()` / `eflash_restore()`
already implement XMODEM-based codeplug dump and restore, but they call
`W25Qx_*` directly and are not referenced anywhere in the UI. Generalising
them onto the `nvmem`/`flash_area` layer would give every target — including
the C62 — codeplug backup and restore over the serial console.

### Revised recommendation

1. **C62 `cps_io` storage** for `.rtxc` (still the prerequisite for anything).
2. **File-based workflow with qdmr:** edit channels (including M17), save
   `.rtxc`, write the partition with `cskburn`, reboot. No converter, no
   custom driver, M17 native.
3. **`eflash_dump`/`eflash_restore` generalised** to `flash_area` so codeplug
   backup and restore work over the console without extra tooling.
4. **rtxlink later** (SLIP + FMP + XMODEM over UART) for live clone/upload
   from qdmr. This is ecosystem work, not C62-specific, and benefits every
   OpenRTX target.
5. **CHIRP remains useful only as an import path** for people with existing
   CHIRP CSV files — a one-way converter into `.rtxc`, not the foundation.

### Caveats

- qdmr's OpenRTX support is labelled "basic" (added March 2026) and has not
  been tested against a C62; expect to contribute fixes upstream.
- `.rtxc` is not yet frozen (RFC refactor in 2023), so the version field
  matters: reject mismatched major versions rather than misparse.
- rtxlink has no firmware implementation yet; qdmr's live download will fail
  against a stock OpenRTX build today.

## Current state

| Item | Status |
|---|---|
| Codeplug storage on C62 | ❌ `cps_io_stub.c` linked; every `cps_*` returns −1 |
| Codeplug format | ✅ `core/cps.h`: `cps_header_t` (88 B) + packed contacts/channels/banks |
| Reference backends | ✅ `cps_io_native_GDx.c` (flash), `cps_io_libc.c` (file, Linux target) |
| Settings/VFO persistence | ✅ our PR #551, `storage_partition` 0x2B0000 (640 KiB) |
| USB device (`usbotg`) | ❌ `status = "disabled"` in `c62.dts` |
| USB device drivers in CSK6 fork | ⚠️ generic `usb_dc_dw.c` present, unproven on this board |
| CTCSS tones | ✅ `ctcss_tone[]` index in `fmInfo_t` |
| DCS | ❌ not implemented (index only reaches the CTCSS table) |
| Zones/banks | ✅ format supports `bankHdr_t`; UI support varies |

### Sizing

`channel_t` = 59 B, `contact_t` = 39 B, `cps_header_t` = 88 B,
`bankHdr_t` = 18 B + 2 B/channel.

| Channels | Codeplug size |
|---|---|
| 128 | ≈ 7.7 KiB |
| 256 | ≈ 15 KiB |
| 1024 | ≈ 60 KiB |

A **64 KiB codeplug partition** comfortably covers ~1000 channels, matching
the largest codeplugs other OpenRTX targets support.

> **Superseded:** these figures assumed 59 B channel records. Measured sizes
> and the current budget are in
> [`cps-storage-budget.md`](cps-storage-budget.md) — 43 B channel + name pool,
> 10 B contact, 384 KiB partition with a 248 KiB reserve.

## CHIRP integration models

### Offline — CHIRP generic CSV

CHIRP's `CSVRadio` (`chirp/drivers/generic_csv.py`) is a file-backed driver
with these columns:

```
Location, Name, Frequency, Duplex, Offset, Tone, rToneFreq, cToneFreq,
DtcsCode, DtcsPolarity, RxDtcsCode, CrossMode, Mode, TStep, Skip, Power,
Comment
```

Any radio that can export/import this CSV is editable in CHIRP, including
copy/paste between radios and spreadsheet editing. No CHIRP code changes are
needed for this route.

### Live — a CHIRP driver

CHIRP drivers subclass `chirp_common.Radio` and talk to the radio over a
serial port, implementing `get_memory`/`set_memory` plus `sync_in`/`sync_out`.
Supporting the C62 means:

1. a programming protocol in firmware,
2. a driver module (`chirp/drivers/…`) registered with `@directory.register`,
3. upstream review in CHIRP (they generally prefer a driver only for radios
   someone can test).

---

## Option A — codeplug storage + CSV converter

### Firmware work

1. Carve a `cps_partition` out of flash (see below) or reserve a region inside
   `storage_partition`.
2. Implement `cps_io` for the C62, modelled on `cps_io_native_RT4D.c` /
   `cps_io_native_GDx.c`, but using the `flash_area` API already used by our
   `nvmem_C62.c`:
   - `cps_open()` — open the area, validate the `RTXC` magic and version,
   - `cps_create()` — write a default header + the default VFO channel,
   - `cps_readChannel()` / `cps_readContact()` / bank accessors — read the
     requested struct,
   - `cps_writeChannel()` etc. — erase/write the affected sector.
3. Remove `cps_io_stub.c` from the C62 source list.
4. Optional: a UI menu entry to reset the codeplug to defaults.

### Partition layout proposal

Today `storage_partition` spans `0x2B0000`–`0x350000` (640 KiB) and we use the
first 8 KiB. Splitting is cleaner for flashing:

```
storage_partition : 0x2B0000  8 KiB   settings + VFO (unchanged layout)
cps_partition     : 0x2B2000  384 KiB OpenRTX codeplug
reserve           : 0x312000  248 KiB future features
```

> **Superseded:** the 64 KiB figure above predates the measured record sizes.
> See [`cps-storage-budget.md`](cps-storage-budget.md) for the current budget.

Both are 4 KiB-aligned. Writing the codeplug via `cskburn 0x2B2000
codeplug.bin` then leaves settings and VFO untouched.

### Host tooling

- `chirp2cps.py` — CHIRP CSV → `RTXC` binary:
  - `Frequency` + `Duplex` + `Offset` → `rx_frequency` / `tx_frequency`
  - `Name` → `name` (32 chars), `Comment` → `descr`
  - `Power` → `power` (mW, clamped to the C62 range)
  - `Mode` FM/NFM → `bandwidth`; DMR/M17 → corresponding mode, if the
    corresponding stack is compiled in
  - `Tone`/`rToneFreq`/`cToneFreq` → `fm.rxToneEn`/`txToneEn` + index into
    `ctcss_tone[]`
- `cps2chirp.py` — the reverse, so the radio's codeplug can be opened in CHIRP.
- Optional: `cps-inspect.py` to dump a codeplug readably.

### Workflow

```
CHIRP: edit channels → File ▸ Export ▸ CSV
./chirp2cps.py channels.csv codeplug.bin
# radio to bootloader
./cskburn -s /dev/ttyUSB0 -C 6 -b 115200 0x2B2000 codeplug.bin
# power-cycle
```

### Limitations

- Not live: each update needs bootloader mode and a power-cycle.
- No read-back from the radio without a separate dump (`cskburn --read`).
- DCS channels, scan lists and per-channel "skip" have no representation in
  `fmInfo_t` today; they would be dropped (see Gaps).

### Effort

Firmware backend 1–2 days, converters 1 day, iteration on hardware 1–2 days.

---

## Option B' — live driver over UART2

Uses the existing CH341 serial link (the one `cskburn` uses) instead of
bringing up USB.

### Protocol sketch

```
frame:  SYNC(2) | CMD(1) | LEN(2) | PAYLOAD(LEN) | CRC16(2)

CMD 0x01 HELLO        → model, firmware version, codeplug layout
CMD 0x02 READ_BLOCK   addr(4) len(2)  → payload
CMD 0x03 WRITE_BLOCK  addr(4) data…   → ack
CMD 0x04 ERASE_SECTOR addr(4)         → ack
CMD 0x05 REBOOT
```

- 256-byte blocks keep the per-frame overhead low on a 115200 baud link
  (~22 ms per block; a 15 KiB codeplug ≈ 60 blocks ≈ 2 s).
- A magic handshake (or a UI "programming mode" entry) switches UART2 from
  console duty to protocol duty; the console is restored on reboot.
- The firmware must suspend the rtx thread and the UI while erasing, since
  flash erase blocks the bus the code runs from.

### CHIRP driver

`chirp/drivers/c62.py`: `Radio` subclass with `get_features`, `get_memory`,
`set_memory`, `sync_in` (read the whole codeplug), `sync_out` (erase + write
changed channels). Map the same fields as Option A.

### Effort and risk

Protocol + firmware 3–5 days, CHIRP driver 2–3 days, plus upstream review.
Risk is moderate: no USB unknowns, but flash-erase-under-XIP and console
switching need care.

---

## Option B — live driver over USB CDC

Same protocol and driver as B', but over the radio's own USB port.

- `usbotg` must be enabled in `c62.dts` and a CDC-ACM function configured.
- The CSK6 Zephyr fork ships `usb_dc_dw.c`; whether the C62's USB pins are
  wired and the controller works is **unverified**. This is the single
  largest unknown in the whole plan and could consume days with no result.
- Upside: no console contention, faster transfers, and a better long-term
  story (the same link could expose codeplug, DFU and serial console).

Effort: B' plus an open-ended USB bring-up. Recommend deferring until Option A
has shipped and there is a reason to invest.

---

## Field mapping (CHIRP CSV ↔ `channel_t`)

| CHIRP | OpenRTX | Notes |
|---|---|---|
| `Location` | channel index | contiguous storage required |
| `Name` | `name[32]` | CHIRP allows longer; truncate |
| `Frequency` | `rx_frequency` | Hz |
| `Duplex` + `Offset` | `tx_frequency` | `""` → TX = RX; `+`/`-` → RX ± offset; `split` → absolute; `off` → `rx_only` |
| `Mode` FM/NFM | `bandwidth` | `NFM` → 12.5 kHz, `FM` → 25 kHz |
| `Tone`, `rToneFreq`, `cToneFreq` | `fm.rxToneEn/rxTone`, `txToneEn/txTone` | CTCSS index via `ctcss_tone[]`; nearest-value match |
| `Power` | `power` | mW; clamp to the radio's range |
| `Comment` | `descr[32]` | truncate |
| `DtcsCode`, `DtcsPolarity`, `RxDtcsCode`, `CrossMode` | — | **unsupported** (no DCS in `OpMode_FM`) |
| `Skip` | — | no per-channel skip/skip-scan in the C62 UI |
| `TStep` | — | global setting, ignored per channel |

## M17 channels

Short answer: **the codeplug format supports them, the firmware only
partially, and CHIRP cannot represent them natively.**

### Codeplug format — yes

`channel_t.mode` selects the opmode, and `m17Info_t` (5 B) carries everything
an M17 channel needs:

| Field | Meaning |
|---|---|
| `rxCan : 4`, `txCan : 4` | Channel Access Numbers for RX and TX |
| `mode : 4` | `DIGITAL_VOICE`, `DIGITAL_DATA`, `DIGITAL_VOICE_DATA` |
| `encr : 4` | `PLAIN`, `SCRAMBLER`, `AES` |
| `gps_mode` | `NO_GPS` or `GPS_META` |
| `contact_index` | index into the contact list |

The destination is **not** stored inline: it is a `contact_index` into
`m17Contact_t` (6-byte encoded M17 address). A codeplug with M17 channels
therefore also needs a contact table.

### Firmware — partially wired

`core/threads.c` sets `rtx_cfg.opMode = state.channel.mode`, so an M17
channel does select the M17 opmode. But the M17 destination and CANs are
taken from **global settings**, not from the channel:

```c
rtx_cfg.can                  = state.settings.m17_can;
rtx_cfg.canRxEn              = state.settings.m17_can_rx;
strncpy(rtx_cfg.destination_address, state.settings.m17_dest, 10);
```

So per-channel `m17Info_t` values are currently ignored. Making M17 channels
meaningful needs a small change: read CAN/mode/encryption from
`state.channel.m17` and resolve `contact_index` through `cps_readContact()`.

Also note M17 is compiled out on our tree (`CONFIG_M17` commented) and the
mode is blocked on the missing audio/DSP integration, so M17 channels would
be inert until that is resolved.

### CHIRP — not natively supported

CHIRP's mode list is:

```
WFM, FM, NFM, AM, NAM, DV, USB, LSB, CW, RTTY, DIG, PKT,
NCW, NCWR, CWR, P25, Auto, RTTYR, FSK, FSKR, DMR, DN, NXDN
```

There is **no M17**, and the generic CSV has no contact list at all, so a
destination address cannot be expressed.

Two workarounds:

1. **Option A (offline CSV):** use a valid placeholder mode (e.g. `PKT` or
   `DIG`) and encode the M17 data in `Comment`, which CHIRP preserves:

   ```
   ... ,PKT, ..., M17 DEST=W1ABC CAN=3 MODE=voice ENCR=plain
   ```

   `chirp2cps.py` parses the comment, sets `mode`, fills `m17Info_t`, and
   creates a contact entry for each unique destination. CHIRP will show the
   channel as a generic PKT channel, but the data round-trips intact.

2. **Option B/B' (custom driver):** add `M17` to `rf.valid_modes` and expose
   CAN/destination/encryption/GPS through `Memory.extra` (CHIRP renders
   arbitrary extra fields), using a call list for M17 contacts. This is the
   clean solution and is incremental work on top of the driver already scoped
   above.

### Recommendation for M17

Do not try to make M17 first-class through generic CSV. Ship FM/NFM channels
in Phase 1–2, and add M17 properly in the driver phase (Option B/B'), where
`valid_modes` and `extra` fields make it a real M17 channel in the UI rather
than a comment-encoded hack.

## P25 and future modes — format extension required

Decision: **qdmr is the CPS**, so `.rtxc` is the contract we extend when new
modes land. Today that contract covers only FM, DMR and M17.

### Current state on both sides

OpenRTX `channel_t` has a union of `fmInfo_t`, `dmrInfo_t` and `m17Info_t`.
There is **no `p25Info_t`**: as merged from the P25 work, P25 uses only global
settings (`settings.p25_srcId`, `settings.p25_dstId`) — per-channel P25
configuration does not exist in the codeplug.

qdmr's OpenRTX codec matches exactly:

```cpp
enum Mode {
  Mode_None = 0,
  Mode_FM   = 1,
  Mode_DMR  = 2,
  Mode_M17  = 3
};
```

No P25 entry on either side.

### What adding P25 requires

1. **Firmware:** a `p25Info_t` in `channel_t` (NAC, target TGID/unit ID,
   phase, encryption, and a contact index if P25 contacts are wanted) plus a
   `Mode_P25` channel mode. Move `rtx_cfg.P25_SrcId/P25_DstId` from global
   settings to the channel, mirroring the M17 per-channel work.
2. **Format version:** bump `CPS_VERSION_MINOR` (additive change) and update
   qdmr's `SupportedVersion` in lockstep. Only a layout change to existing
   structs justifies a major bump.
3. **qdmr:** a `P25Channel` mapping in `openrtx_codeplug.cc` plus a channel
   dialog — an upstream contribution.
4. **Graceful unknown modes:** both the firmware and qdmr should skip or
   reject channels whose mode they do not understand rather than misparse
   them. Appending `Mode_P25 = 4` keeps existing readers working as long as
   they treat unknown modes safely.

### Design rule for our C62 work

Keep the codeplug layer **mode-agnostic**: a table of mode → record size and
decoder, with no hard-coded assumption that there are three modes. Then
adding P25 — or NXDN, D-STAR or whatever else is claimed — is additive rather
than a rewrite.

Sequencing: land the `.rtxc` P25 extension **with** the P25 firmware support.
Shipping a format revision with no consumer just creates drift, and until
then P25 configuration stays in global settings as it is today.

## Gaps and risks

1. **DCS is not implemented** in OpenRTX's FM opmode — DCS channels would be
   silently dropped. Acceptable for a first version if documented; supporting
   DCS is a separate feature.
2. **Scan lists / skip flags** have no storage in `fmInfo_t`.
3. **Flash erase blocks the bus** the firmware executes from. Every write
   path must run with interrupts and the rtx thread quiesced, or the radio
   will fault — the same class of bug we hit when `nvm_init()` was missing.
4. **Codeplug versioning**: `cps_header_t` carries a version; a mismatched
   major version must fall back to defaults rather than misparse.
5. **Channel reordering**: CHIRP allows arbitrary `Location` values; the
   converter must pack them contiguously or preserve holes deliberately.
6. **Power values**: CHIRP's generic driver uses arbitrary power levels;
   values must be clamped to what the C62 PA supports.

## Recommended phasing

| Phase | Deliverable | Effort | Risk |
|---|---|---|---|
| 1 | `cps_partition` + flash `cps_io` backend, channels survive reboot | 1–2 d | low |
| 2 | `chirp2cps.py` / `cps2chirp.py` + flash workflow | 1–2 d | low |
| 3 | (optional) live driver over UART2 + CHIRP module | 5–8 d | medium |
| 4 | (optional) USB CDC transport | open-ended | high |

## Open questions

1. Is the target a *personal* channel list, or do we need zones/banks and
   scan lists to match the OEM firmware?
2. Is DCS needed, or is CTCSS sufficient?
3. For live programming, is UART acceptable, or is USB (single cable, no
   bootloader dance) a hard requirement?
4. Should the codeplug be a separate partition (recommended) or live inside
   `storage_partition`?
