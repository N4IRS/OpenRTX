# C62 hardware reference

Hardware description of the Retevis C62 as wired up by
`platform/targets/c62/`. Everything here is derived from the board devicetree
(`c62.dts`), `hwconfig.c`/`hwconfig.h` and the drivers under
`platform/drivers/`; if this document and the code disagree, the code wins.

| | |
|---|---|
| Model | Retevis C62 (dual band, 1–5 W) |
| SoC | ListenAI CSK6011B: Cortex-M33 application core + HiFi4 DSP |
| Transceiver | BK4819 (VHF/UHF) |
| Broadcast FM RX | BK1080 |
| Display | Sitronix ST7735R, 160 × 128, RGB565, SPI |
| Audio | CSK6 codec via the LSF DSP service, 48 kHz |
| Battery | 2S Li-ion (8.4 V full) |
| Flash | 4 MiB, memory-mapped at `0x18000000` |
| SRAM | 320 KiB |
| PSRAM | 4 MiB at `0x30000000` |

## Memory map

### SRAM

| Region | Address | Size | Use |
|---|---|---|---|
| Application SRAM | `0x00080000` | 256 KiB | Zephyr data, BSS, heap, stacks |
| DSP shared bank | `0x000C0000` (`0x200C0000` alias) | 64 KiB | Shared audio buffers, reserved |

`sram0` in the devicetree is deliberately limited to the first 256 KiB so the
top 64 KiB bank stays out of the application data and the heap.

### PSRAM

| Region | Address | Size | Use |
|---|---|---|---|
| CP (DSP) | `0x30000000` | 2 MiB | DSP image and working set |
| AP (application) | `0x30200000` | 1.75 MiB | Available as `PSRAMAP` |
| DSP stack | top 256 KiB | 256 KiB | Left clear for the CP stack at CP `0x60400000` |

### Flash

4 MiB at `0x18000000`, 4 KiB erase blocks, 4-byte write blocks, no bootloader:
the application is linked at offset 0 and must fit in 1 MiB
(`CONFIG_FLASH_LOAD_SIZE=0x100000`).

| Offset | Size | Partition | Notes |
|---|---|---|---|
| `0x000000` | 1 MiB | `slot0` | OpenRTX application |
| `0x100000` | 1 MiB | `dsp_firmware` | HiFi4 DSP image, read-only, copied to PSRAM at boot |
| `0x200000` | 0xB0000 | `factory_res0` | OEM resources, preserved |
| `0x2B0000` | 0xA0000 | `storage` | OpenRTX NVS settings (blank in every factory dump) |
| `0x350000` | 0x60000 | `factory_res1` | OEM resources, preserved |
| `0x3B0000` | 0x10000 | `factory_cal` | OEM settings + RF calibration journal, **never write** |
| `0x3C0000` | 0x40000 | `factory_chan` | OEM channel store, **never write** |

`factory_cal` is unique to each radio and the stock firmware rewrites it with
defaults if it finds no valid entry. Writing it destroys the per-unit
calibration. See `docs/flashing.md` for backup and flashing procedures.

## RF

### BK4819 transceiver

| Signal | Pin | Notes |
|---|---|---|
| SCLK | `GPIOA 13` | 3-wire bus, 200 kHz, half duplex |
| SDATA | `GPIOA 7` | bidirectional |
| SCN (chip select) | `GPIOA 8` | active low, driven by the driver, not the SPI controller |

The bus is described in the devicetree as `bk4819-spi` (Zephyr SPI bitbang);
the driver owns the chip select. Register settings come from the M17
Foundation A36Plus profile, with per-band power calibration.

The BK4819's own GPIOs drive the front end:

| BK4819 GPIO | Function |
|---|---|
| 0 | VHF RX LNA |
| 1 | UHF RX LNA |
| 2 | VHF TX PA |
| 3 | UHF TX PA |
| 4 | ALC / TX LED |

### TX power

Output power is set by the APC voltage on `GPIOA 3`, a PWM at **100 kHz**
(`gptchannel3`, 10 µs period). The duty cycle is interpolated between the low
and high power calibration points for the current band, mapping 1 W (low) to
5 W (high), as on the MD-3x0/MD-UV3x0. The stock firmware also uses 100 kHz;
at 1 kHz the APC voltage follows the ripple and the output power stops
tracking the duty cycle.

Calibration points are read from the OEM journal at `0x3B0000`
(`platform/targets/c62/calib_C62.c`): frequency, low/high power duty cycles and
six RX parameters per band. A C62 entry holds 20 records, a C2 entry 10. When
no journal is found, the stock defaults are used (136.125, 146.125, 156.125,
166.125, 173.975, 400.125, 420.125, 440.125, 460.125 and 479.975 MHz).

Band split is at 174 MHz: below it the VHF calibration and PA/LNA are used,
above it the UHF ones.

### BK1080 broadcast FM receiver

| Signal | Pin | Notes |
|---|---|---|
| SCLK | `GPIOA 5` | I²C bitbang, ~10 µs clock period |
| SDATA | `GPIOA 6` | |
| PWR | `GPIOB 2` | active low |

## Audio

Audio does not go through a simple codec on the application core: the CSK6
codec is driven by the **LSF service controller** and the HiFi4 DSP.

- DSP firmware: 466,832 bytes, 48 kHz build, flashed at `0x100000`, pinned by
  sha256 in the build (see `platform/targets/c62/README.md`).
- Transport: 48 kHz stereo, 16-bit PCM, 10 ms frames (480 samples), through
  `AudioRecord`/`AudioTrack` LSF services.
- Application-side audio is resampled to 48 kHz; FM samples pass through
  unchanged.

Codec gains (`hwconfig.h`, analog `_A` / digital `_D`, in dB):

| Path | Analog | Digital |
|---|---|---|
| Microphone in (`C62_ADC_MIC_*`) | +6 | +10 |
| RX / line in (`C62_ADC_RX_*`) | +6 | +20 |
| Speaker out (`C62_DAC_SPK_*`) | −6 | 0 |
| TX DAC into BK4819 mic (`C62_DAC_TX_*`) | −18 | 0 |

`CONFIG_MIC_GAIN` is 13, matched to the +10 dB digital mic gain. The TX DAC
level must never be raised above 0 dB.

Board GPIOs:

| Signal | Pin | Notes |
|---|---|---|
| Speaker enable | `GPIOA 1` | active high |
| DTMF enable | `GPIOB 0` | active high |

The DSP firmware reconfigures shared pins while it brings audio up (ADC and
the console UART), so `platform.c` re-applies the ADC pin configuration and
`audio_c62.c` restores the console pins after codec start.

## Display

| Signal | Pin / setting |
|---|---|
| Controller | Sitronix ST7735R |
| Resolution | 160 × 128, RGB565 |
| Bus | SPI1, 20 MHz |
| Data/command | `GPIOA 10`, active low |
| Reset | `GPIOA 0`, active low |
| Backlight | `GPIOA 2`, `gptchannel2`, PWM 50 Hz (20 ms period) |

Panel init (MADCTL `0xA0`, gamma tables, `VMCTR1=0x0e`, inverted `INVCTR=0x07`)
is in the devicetree.

> **Debugging note:** the display reset line shares the SWD pins. Asserting
> `GPIOA 0` kills the debugger connection; the devicetree comment on
> `reset-gpios` is a warning, not decoration.

## Keys, LEDs

Keypad is a 5 × 4 matrix: 5 drive lines, 4 sense lines with pull-ups.

| Drive | Pins (DTS `col1`…`col5`) |
|---|---|
| 1–5 | `GPIOA 14`, `GPIOA 17`, `GPIOA 19`, `GPIOB 6`, `GPIOB 11` |

| Sense | Pins (DTS `row1`…`row4`) |
|---|---|
| 1–4 | `GPIOB 9`, `GPIOA 16`, `GPIOB 10`, `GPIOA 20` |

Key map (`keyboard_c62.c`, index = drive × 4 + sense):

| Drive | Sense 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| 1 | ENTER | UP | DOWN | ESC |
| 2 | 3 | 4 | 5 | 6 |
| 3 | 7 | 8 | 9 | * |
| 4 | 0 | # | — | — |
| 5 | F1 | F2 | 1 | 2 |

Separate GPIOs:

| Signal | Pin | Notes |
|---|---|---|
| Side key | `GPIOB 1` | active low, pull-up; also the boot-mode key |
| PTT | `GPIOA 15` | active low, pull-up |
| Green LED | `GPIOB 4` | active high |
| Red LED | — | no GPIO: lit by the PA activation line, not by the MCU |
| Keyboard backlight | `GPIOB 3` | active high |
| Flashlight (white LED) | `GPIOB 5` | active high |

## Power and battery

| | |
|---|---|
| Battery | 2S Li-ion, 8.4 V full |
| Battery sense | ADC channel 2, `GPIOB 8`, 200k/100k divider (×3) |
| Charge detect | ADC channel 1, raw threshold 2100 |
| Off | `NVIC_SystemReset()` — the MCU is always powered |

`platform_getVbat()` reads an 11-bit bipolar conversion (raw − 2048) and
multiplies by 3 for the divider. The ADC pin configuration has to be re-applied
after the DSP firmware runs, which overwrites it.

The C62 has **no power switch the MCU can observe**: the power knob cuts
power directly, so there is no graceful shutdown and `state_terminate()` never
runs. Settings are persisted by the debounced write in `state.c`
(`NVMEM_DEBOUNCED_WRITE`, 3 s). While charging, the power-button status reads
on (battery above 3 V), which is why the radio powers up on charger insertion.

## Hardware gotchas

- **PTT shares the boot ROM UART RX line.** Do not press PTT while `cskburn`
  is running or the transfer corrupts.
- **LCD reset shares SWD.** Do not toggle `GPIOA 0` with a debugger attached.
- **The DSP reconfigures shared pins** (ADC, console UART) during audio init;
  the board layer restores them afterwards.
- **No bootloader.** A bad image at offset 0 is recovered only through the
  boot ROM burn mode (radio off, hold the lower side key, power on).
- **`factory_cal` and `factory_chan` are per-unit.** Never write or restore
  them from another radio's dump.

## Related documents

- `docs/flashing.md` — boot mode, backup, flashing, partition map
- `docs/nvram-backup.md` — settings backup and restore
- `docs/p25.md` — P25 operation and verification
- `platform/targets/c62/README.md` — build and DSP firmware details
