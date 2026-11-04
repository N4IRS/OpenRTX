## Current branch / build state (updated 2026-11-04)

**PR workflow rules: see `docs/pr-checklist.md` — mandatory for every upstream PR.**
**NVRAM backup/restore: see `docs/nvram-backup.md`.**
**P25 operation and verification: see `docs/p25.md`.**

Branches:
- `c62-m17-plus` — **current development branch** (fork `N4IRS/OpenRTX`,
  not pushed upstream). FM + M17 + P25 Phase 1 + IMBE + UI on top of
  `c62-nvmem-persistence`. P25 TX/RX **verified on hardware**: a real P25
  subscriber radio decodes the voice and `dsd-fme` decodes the same key-up
  with zero IMBE/LCW errors. Build: `build.p25test`, `zephyr.bin`
  423,756 B (40.4 % flash).
- `c62-kbd-backlight` — PR **#548** (merged-pending), keyboard backlight.
- `c62-nvmem-persistence` — PR **#551** (merged-pending), flash-backed nvmem.
- `p25-c62` — **superseded** local integration branch, FM-only (22 commits,
  rebased onto `c62-nvmem-persistence` on 2026-10-28). Backup of the
  pre-rebase state: `p25-c62-backup` (`32452d0d`).

Persistence is **fixed and verified on hardware** (446.000 survives power
cycle). Root cause was three stacked defects: `nvm_init()` never called
(0-sized write/erase units → divide-by-zero hard fault), wrong `baseAddr`
(must be `FIXED_PARTITION_OFFSET(storage_partition)`), and unpadded writes
(packed `channel_t` = 59 B vs 4-byte write block).

### Historical (pre-rebase, 2026-10-27)

Branch: **`p25-c62`** (merged `lurk/p25-pr` 39fa5df8 + C62 wiring + five fixes).
Latest build: FLASH 347,440 B / 33.13%, RAM 179,824 B / 68.6% — binary in
`Retevis_C62/local_build/zephyr.bin` (MD5 `4db0ca5f…`).

State: **FM-only** (CONFIG_M17 and CONFIG_P25 both commented out, matching
upstream merged branch). P25 source files remain in the tree (from the
lurk/p25-pr merge) but are not compiled. AF-type fix in radio_C62.cpp
retained (harmless in FM mode — uses `bk4819_set_modulation(dev, true)`).

### Fixes applied this session (five upstream C62 gaps) — VERIFIED 2026-10-27

1. **Battery ADC** (`platform.c`): removed bipolar -2048 offset, changed
   resolution 11→12 bit, added `BAT_DEBUG` printk for raw ADC values.
   ✅ Verified working.
2. **Keyboard backlight** (`ui.c`): turns off on display standby, restores
   on wake. ✅ Verified working.
3. **Squelch** (`OpMode_FM.cpp`): software RSSI-based, accessed via macro
   screen arrows (hold secondary PTT → arrows adjust). ✅ Verified working.
4. **Nvmem persistence** (`nvmem_c62.c`): NVS-backed implementation on
   `storage_partition` (0x2B0000). Settings and VFO channel data now
   persist across reboots. ✅ Verified working.
   - `nvm_init()` called from `platform_init_csk6()`.
   - `CONFIG_NVS=y` added to `c62_defconfig`.
   - `nvm_writeSettings` called on settings menu exit, squelch/brightness changes.
5. **Charger power-on** (`platform.c`): by design — `platform_pwrButtonStatus()`
   returns `vbat > 3000` (always true when charging). Documented as known
   limitation. ✅ Documented.

### Debug instrumentation removed. Clean working tree (except intentional fixes).

## Where this queue goes next

- `c62-m17-plus` is the working branch; FM, M17 and P25 all run from the same
  image. Keep P25 on the fork — there is no upstream P25 PR.
- P25 next steps: move NAC/TGID from global settings into the channel (a
  `p25Info_t` in `channel_t` plus a `Mode_P25` channel mode), see
  `docs/chirp-cps-feasibility.md`.
- Optional: calibrate `TX Level` against a deviation meter; 160 is the
  verified on-air value but the control is an uncalibrated amplitude.

The provider's "OpenRTX-gains" DSP audio work is no longer needed: our
`audio_path.cpp` path receives and transmits M17 and P25 correctly. The
static-diff notes below are kept for reference.
---

## Static binary diff: provider vs ours (2026-10-27)

**Key finding: the audio architectures are fundamentally different.**

Our tree uses the generic `audio_path.cpp` framework (audioPath_request,
audio_connect, audio_disconnect, streaming threads, ring buffers). The
provider's "OpenRTX-gains" fork **rewrote the C62 audio subsystem** to
configure the DSP audio service directly with channel/frameSize/shared-
channels/cellSize parameters — no streaming threads, no audio_path framework.

Provider-only strings (audio/DSP):
- `C62 DSP output: channels=%d frameSize=%d shared channels=%d cellSize=%d`
- `48k ADC/DAC configuration`
- `samplingRate=%u;channels=%u`
- `recording buffer layout`, `stereo playback buffer layout`
- `DSP input stream configuration mismatch` (their config validation)
- `ADC01 HPF2 bypass readback mismatch/skipped`
- `ADC gain`, `DAC gain`
- `Audio initialization failed at %s: %d`
- `c62_audio`, `c62_audio_stream`

Our-only strings (audio framework):
- `Audio path will connect: %d->%d (total: %d)`
- `Audio streaming threads started/stopped`
- `AudioTrack_write failed: %d`
- `Failed to initialize AudioRecord RTX_INPUT / AudioTrack RTX_OUTPUT`

Conclusion: the fix is NOT a register tweak — it requires adopting the
provider's audio architecture (direct DSP service config instead of generic
audio_path framework). Getting their audio_c62.c/dsp.c/lsf.c source is
essential. The "C62 DSP output: channels=%d frameSize=%d shared channels=%d
cellSize=%d" print is the smoking gun — their audio setup configures the DSP
service with parameters our tree doesn't even reference.
