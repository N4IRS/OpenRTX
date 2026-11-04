# Flashing the Retevis C62

> **Warning — brick risk.** Stated in `OpenRTX/platform/targets/c62/README.md`:
> flashing is at your own risk. The factory backup below is the only way back.

## Bootloader mode

1. Radio **off**.
2. Hold the **secondary PTT (lower side button)**.
3. Power the radio on, then release the button.

The radio enumerates as a **CH341 USB-serial** adapter → `/dev/ttyUSB0`
(`dmesg`: `idVendor=1a86 idProduct=7523`). If it appears as something else,
adjust the `-s` argument.

## Flash partition map (4 MB)

| Offset | Size | Content | Notes |
|---|---|---|---|
| `0x000000` | 1 MB | Application (OpenRTX `zephyr.bin`) | linked at 0, no bootloader (`CONFIG_FLASH_LOAD_SIZE=0x100000`) |
| `0x100000` | 1 MB | `dsp_firmware` DSP image | **read-only**; HiFi4 DSP, copied to PSRAM at boot (`CONFIG_LSF_DSP_LOAD_ON_BOOT=y`) |
| `0x200000` | 0xB0000 | `factory_res0` OEM resources | preserved |
| `0x2B0000` | 0xA0000 | **OpenRTX storage** | erased in every factory dump examined |
| `0x350000` | 0x60000 | `factory_res1` OEM resources | preserved |
| `0x3B0000` | 0x30000 | `factory_cal` OEM settings + calibration journal | OEM rewrites with defaults if invalid — **never write here** |

Source: `OpenRTX/platform/targets/c62/c62.dts`.

## Backup (do this before first flash)

```bash
cd /srv/WorkSpace/Retevis_C62
./cskburn --read "0x0:4194304:factory.bin" -C 6 -b 115200 -v -s /dev/ttyUSB0
```

Takes ~12 min (≈5.7 KB/s). Result: `factory.bin`, 4194304 bytes
(**already captured 2026-10-24**; verify size/MD5 before relying on it).

## Burn OpenRTX + M17 DSP image

```bash
cd /srv/WorkSpace/Retevis_C62
./cskburn -s /dev/ttyUSB0 -C 6 -b 115200 \
  0x000000 <path-to-app-part-1-of-flash>.bin \
  0x100000 dsp_firmware.bin
```

Two app options:

| App image | Source | M17 | Notes |
|---|---|---|---|
| `local_build/zephyr.bin` (347,428 B) | our build, `OpenRTX` merged branch + `CONFIG_M17` | UI compiled in | produced locally; see `docs/status.md` for deltas vs reference |
| `zephyr.bin` (345,584 B) | developer's private **OpenRTX-gains** fork | working (tested) | provenance documented in `docs/status.md` |

Both pair with the same `dsp_firmware.bin` at `0x100000`.

## Restore factory firmware

```bash
./cskburn -s /dev/ttyUSB0 -C 6 -b 115200 0x000000 factory.bin
```

(Dry-run this restore path on a spare/unneeded radio first if ever available;
the factory image was read from this exact radio, so it should restore it.)

## Troubleshooting

- **No `/dev/ttyUSB0`** – not in bootloader mode; retry the button sequence,
  check `dmesg`.
- **“Parsed 0 parts from HEXs”** – you passed a `.bin` where cskburn expects
  whole-flash regions; use the partition-offset form above.
- **DSP fails to sync after boot** – see “Known deltas” in `docs/status.md`;
  first fallback is flashing the developer's `zephyr.bin` at `0x000000`.
