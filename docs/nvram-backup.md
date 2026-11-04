# C62 NVRAM backup and restore

How to back up and restore the Retevis C62 flash regions that hold
persistent state, using `cskburn` over the CH341 serial adapter.

## What lives where

| Region | Address | Size | Contents | Writable by OpenRTX |
|---|---|---|---|---|
| `slot0_partition` | `0x000000` | 1 MiB | OpenRTX application | yes (firmware flashing) |
| `dsp_firmware` | `0x100000` | 1 MiB | ListenAI DSP image | yes (DSP flashing) |
| `factory_res0` | `0x200000` | 720 KiB | OEM resources | no |
| `storage_partition` | `0x2B0000` | 640 KiB | **OpenRTX settings + VFO** | yes |
| `factory_res1` | `0x350000` | 384 KiB | OEM resources | no |
| `factory_cal` | `0x3B0000` | 64 KiB | **TX power calibration journal** (per-radio) | no — must never be written with foreign data |
| `factory_chan` | `0x3C0000` | 256 KiB | OEM channel store | no |

Total flash: 4 MiB.

## OpenRTX NVRAM layout

`platform/drivers/NVM/nvmem_C62.c` stores two records inside
`storage_partition`:

| Offset | Record | Payload |
|---|---|---|
| `0x0000` | settings | `settings_t` (84 bytes, packed) |
| `0x1000` | VFO channel | `channel_t` (59 bytes, packed) |

Each record starts with an 8-byte header:

```
uint32_t magic;   /* 0x4F525458, "ORTX" */
uint16_t crc;     /* crc_ccitt() of the payload, initial value 0 */
uint16_t pad;
```

Payloads are padded to the 4-byte flash write block. An erased record
(all `0xFF`) fails the magic check and the driver falls back to defaults.
Redundant writes are skipped by comparing the record CRC.

## Backing up

The scripts live next to `cskburn` in `/srv/WorkSpace/Retevis_C62`.

```bash
cd /srv/WorkSpace/Retevis_C62

./nvram-backup.sh --fast    # settings + VFO only, 640 KiB,  ~2 min
./nvram-backup.sh --data    # all OEM data + NVRAM,  2 MiB,  ~6 min
./nvram-backup.sh --full    # entire 4 MiB flash image,     ~12 min
```

Each run creates `nvram-backups/<timestamp>-<name>/` containing the image,
`SHA256SUMS` and a `MANIFEST` recording address and size.

Put the radio in bootloader mode when prompted: hold the secondary PTT
(lower side button), power on, release.

`--read` runs at roughly 5.7 KiB/s, so the timings above are dominated by
the serial link.

**Recommended cadence:** `--fast` before every firmware flash; `--data`
after any change to calibration or channels; `--full` when you want a
complete, bootable snapshot (it includes the app and DSP images too).

A pristine factory image from before any OpenRTX work is kept at
`factory.bin` (4 MiB, read 2026-10-24).

## Restoring

```bash
cd /srv/WorkSpace/Retevis_C62
./nvram-restore.sh nvram-backups/20261028-120000-fast
```

The script verifies `SHA256SUMS`, shows the address and size, requires an
explicit `YES`, then writes the image back at the recorded address.

## Inspecting a backup without flashing

```bash
./nvram-inspect.py nvram-backups/20261028-120000-fast/storage.bin
```

Decodes the settings and VFO records, including CRC validation, and prints
brightness, squelch, callsign, M17 destination, RX/TX frequency and TX
power. Handles both the `--fast` image (partition at 0) and the
`--data`/`--full` images (partition at `0x0B0000`).

## Warnings

- `factory_cal` is unique to each radio. Restoring a `--data`/`--full`
  backup to a *different* radio will load the wrong TX calibration.
- Never write arbitrary data to `factory_cal` or `factory_chan`.
- `cskburn` erases before writing; a partial restore leaves the region
  erased. Keep the backup until you have verified the radio boots and
  transmits correctly.
- If settings become corrupt, erasing just `storage_partition` returns the
  radio to defaults without touching calibration:
  `./cskburn -s /dev/ttyUSB0 -C 6 -b 115200 --erase 0x2b0000:0xa0000`
