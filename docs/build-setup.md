# Build Environment Setup — OpenRTX for Retevis C62

**Audience:** Senior developer joining the C62 port. This document gets you from a
fresh machine to a working firmware build and flash. It assumes Linux (Debian/Ubuntu)
and no root access (everything lives under the workspace root).

**Workspace root:** `/srv/WorkSpace/OpenRTX-C62` (referred to as `$WS` throughout).

---

## 1. What you are building

OpenRTX firmware for the **Retevis C62** (ListenAI CSK6 SoC, Cortex-M33 + HiFi4 DSP,
BK4819 transceiver, BK1080 FM receiver). The build produces two artifacts:

| Artifact | Flash offset | Size | Source |
|---|---|---|---|
| `zephyr.bin` | `0x000000` | ~406 KB | Zephyr app (CMake/west) |
| `dsp_firmware.bin` | `0x100000` | 456 KB | Downloaded from lsf-zephyr-sdk (pinned by SHA-256) |

The C62 is **not** a standard Zephyr target. It uses ListenAI's Zephyr 3.4 fork
(`zephyr-listenai`) and the 0.16.8 SDK. Building requires `west` + `cmake` + `ninja`
with specific versions.

---

## 2. Prerequisites

| Tool | Version | Notes |
|---|---|---|
| Python | 3.11+ (3.13 tested) | For west, meson, and Zephyr scripts |
| CMake | **3.31.x** (NOT 4.x) | CMake 4 breaks Zephyr 3.4 `FindZephyr-sdk.cmake` |
| Ninja | 1.10+ | Build backend |
| west | 1.5.0 | Zephyr meta-tool |
| Zephyr SDK | 0.16.8 | arm-zephyr-eabi toolchain only |
| git | 2.x | With submodule support |
| cskburn | 1.31.0 | ListenAI flashing tool (npm package) |
| pdftotext | (optional) | For reading reference PDFs |

All components live under `$WS`. No root/sudo required.

---

## 3. Directory layout (must be preserved)

```
$WS/                              # /srv/WorkSpace/OpenRTX-C62
├── env.sh                        # Source before any build/flash command
├── .venv/                        # Python virtualenv (west, meson, cmake, ninja)
├── zephyr-sdk-0.16.8/            # Zephyr SDK (arm-zephyr-eabi)
├── zephyr-listenai/              # Zephyr 3.4 fork (ZEPHYR_BASE)
├── OpenRTX/                      # OpenRTX source (git clone, manifest.self.path=OpenRTX)
│   ├── west.yml                  # West manifest (group-filter: +csk6)
│   ├── meson.build
│   ├── CMakeLists.txt            # Top-level CMake (Zephyr entry point)
│   ├── platform/targets/c62/     # C62-specific code
│   └── build/                    # Zephyr build output (board_root, etc.)
├── modules/                      # West modules (hal, af, lsf, urpc, etc.)
├── csk/                          # ListenAI CSK HAL
├── docs/                         # Documentation
└── Retevis_C62/                  # Flashing tools, firmware binaries, backups
    ├── cskburn                   # Flashing tool
    ├── factory.bin               # Factory firmware backup (4 MB)
    ├── dsp_firmware.bin          # 48 kHz DSP firmware (pinned)
    └── zephyr.bin                # Built app (copy from build output)
```

**Critical:** The workspace layout is fixed by `west.yml` (`self: path: OpenRTX`). The
OpenRTX clone **must** sit as `OpenRTX/` inside the workspace root. Do not run
`west init -l .` in the clone directly — it spreads module repos into the parent.

---

## 4. Step-by-step setup

### 4.1 Create the Python virtualenv

```bash
python3 -m venv $WS/.venv
$WS/.venv/bin/pip install --upgrade pip
$WS/.venv/bin/pip install \
    west pyusb meson intelhex==2.3.0 setuptools cmake==3.31.10 ninja
$WS/.venv/bin/pip install \
    -r $WS/zephyr-listenai/scripts/requirements.txt  # or requirements-base.txt
```

**Why cmake 3.31.10?** CMake 4.x breaks Zephyr 3.4's `FindZephyr-sdk.cmake`. The pip
shim ensures the venv's cmake takes precedence over any system cmake.

### 4.2 Install the Zephyr SDK (arm only)

```bash
cd $WS
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/\
zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
tar xf zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
cd zephyr-sdk-0.16.8
PATH=$WS/.venv/bin:$PATH ./setup.sh -t arm-zephyr-eabi -c
```

The `-c` flag registers the SDK with CMake. The toolchain lives at
`$WS/zephyr-sdk-0.16.8/arm-zephyr-eabi/`.

### 4.3 Clone OpenRTX and set up west workspace

```bash
cd $WS
git clone https://github.com/OpenRTX/OpenRTX.git OpenRTX
cd OpenRTX
# Configure west to use the ListenAI Zephyr fork
west init -l .
west config zephyr.base zephyr-listenai
west config manifest.group-filter "+csk6"   # include CSK6 support modules
west update --group-filter +csk6
```

This pulls: `zephyr-listenai` (Zephyr 3.4 fork), `listenai-csk`, `csk6_cm33` HAL,
`listenai-modules-af`, `listenai-modules-freertos_shims`, `listenai-modules-lsf`,
`listenai-modules-urpc`.

### 4.4 Vendor codec2

The C62 build needs codec2. Normally meson handles this, but since we build via
`west` directly, it must be vendored manually:

```bash
cd $WS/OpenRTX
git clone https://github.com/drowe67/codec2-dev subprojects/codec2
cd subprojects/codec2 && git checkout a298f3789d7ef9e829049ebe14da8845d97ba8c1
cp ../packagefiles/codec2/meson.build .
mkdir -p codec2 && cp ../packagefiles/codec2/codec2/version.h codec2/
echo "subprojects/codec2/" >> ../../.git/info/exclude
```

### 4.5 Install cskburn (flashing tool)

```bash
npm install -g @listenai/cskburn   # installs to ~/.npm-global/bin
# The bare 'cskburn' npm package no longer exists
```

### 4.6 Source the environment

```bash
source $WS/env.sh
```

This exports `WS`, `PATH` (venv + npm-global), `ZEPHYR_16_SDK_INSTALL_DIR`,
`ZEPHYR_SDK_INSTALL_DIR`, `ZEPHYR_BASE`, and `BOARD=c62`.

---

## 5. Building

### 5.1 Standard build (Zephyr/west)

```bash
source $WS/env.sh
cd $WS
rm -rf build.c62                    # clean any stale build
west build -b c62 -d build.c62 OpenRTX
```

Output:
- `build.c62/zephyr/zephyr.bin` — application (flash at 0x000000)
- `build.c62/zephyr/dsp_firmware.bin` — DSP firmware (flash at 0x100000)

The DSP firmware is downloaded automatically from OpenRTX's lsf-zephyr-sdk
(pinned by SHA-256: `9579b617f048c646465118f6d0b9e1b765b4d7d811ff9c8649c5754fe45a8903`).
For offline builds, set `C62_DSP_FIRMWARE=/path/to/dsp_firmware.bin`.

### 5.2 Build branches

| Branch | Contents | Build dir |
|---|---|---|
| `Retevis-c62` | Upstream C62 baseline (FM only) | `build.c62` |
| `c62-m17-plus` | M17 + P25 Phase 1 + IMBE + persistence + UI (current) | `build.p25test` |
| `pr-542` | M17 voice (48 kHz DSP) from upstream PR | `build.pr542` |

```bash
cd $WS/OpenRTX
git checkout <branch>
source $WS/env.sh
cd $WS
rm -rf build.<name>
west build -b c62 -d build.<name> OpenRTX
```

### 5.3 Build output sizes (reference)

| Build | zephyr.bin | Flash % |
|---|---|---|
| M17 + persistence | 351,284 B | 33.5% |
| M17 + persistence + P25 | 406,144 B | 38.7% |
| M17 + P25 + TX fixes (current, `build.p25test`) | 423,756 B | 40.4% |

---

## 6. Flashing

### 6.1 Enter bootloader mode

1. Radio **off**.
2. Hold **secondary PTT** (lower side button).
3. Power on, then release.

The radio enumerates as a CH340 USB-serial adapter at `/dev/ttyUSB0`.

### 6.2 Flash command

```bash
cd $WS/Retevis_C62
sg dialout -c "cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v \
  --reset-attempts 8 --timeout 30000 \
  0x000000 zephyr.bin \
  0x100000 dsp_firmware.bin"
```

**Permission gotcha:** In this environment, `id` may not show `dialout` even though
`/etc/group` lists the user. Direct `cskburn` invocation fails with `E3002`
(Permission denied). Always launch via `sg dialout -c '...'`.

**Timing:** ~75 seconds total (31s app + 42s DSP at ~11 KB/s).

### 6.3 Flash partition map (4 MB total)

| Offset | Size | Content | Notes |
|---|---|---|---|
| `0x000000` | 1 MB | Application | Linked at 0, no bootloader |
| `0x100000` | 1 MB | DSP firmware | HiFi4 DSP, copied to PSRAM at boot |
| `0x200000` | 704 KB | OEM resources | **Preserved** — do not write |
| `0x2B0000` | 640 KB | OpenRTX storage | Settings (8 KB) + cps_partition (448 KB) |
| `0x350000` | 384 KB | OEM resources | **Preserved** — do not write |
| `0x3B0000` | 320 KB | OEM calibration | **Never write** — OEM rewrites with defaults if invalid |

### 6.4 Backup / restore

```bash
# Backup (do before first flash)
cd $WS/Retevis_C62
./cskburn --read "0x0:4194304:factory.bin" -C 6 -b 115200 -v -s /dev/ttyUSB0

# Restore factory firmware
./cskburn -s /dev/ttyUSB0 -C 6 -b 115200 0x000000 factory.bin
```

Factory backup: `factory.bin`, 4,194,304 bytes, captured 2026-10-24.

---

## 7. Gotchas learned the hard way

1. **Workspace structure is fixed by the manifest.** `west.yml` has `self: path: OpenRTX`.
   The clone must sit as `OpenRTX/` inside the workspace root.

2. **Moving the workspace breaks absolute paths** in the venv and SDK registry.
   Recreate the venv and re-run SDK `setup.sh -c` after any move.

3. **CMake must be < 4.x.** Keep the pip shim at 3.31.x. CMake 4 breaks Zephyr 3.4.

4. **Stale build directories cache wrong state.** Always `rm -rf build.<name>` before
   retrying a full configure. Stale `build.c62/` caches wrong `CMAKE_MAKE_PROGRAM`.

5. **`id` may not show `dialout`** even though `/etc/group` lists the user. Use
   `sg dialout -c '...'` for cskburn.

6. **cskburn exit code 91** is normal after a successful burn (vendor-specific signaling).
   Check the log for "Finished" and "100.00%".

7. **First burn may fail mid-way** (ETIMEDOUT on a block). Retry with
   `--reset-attempts 8 --timeout 30000`. The chip's bootloader mode can time out
   between probe and burn — enter BL mode and burn immediately without a prior probe.

8. **`nvm_init()` must be called from `platform_init_csk6()`** for C62. All other
   targets call it; the C62 was missing this (root cause of the persistence bug).

9. **`channel_t` is 94 bytes packed** (not 59 as the stale comment says). Records
   must be padded to 4-byte boundaries for flash writes.

10. **`// 59B` comment in `cps.h` is stale.** True sizes measured on C62 ELF:
    channel=94, contact=39, settings=84, header=88.

---

## 8. Key files

| File | Purpose |
|---|---|
| `$WS/env.sh` | Environment setup (source before build/flash) |
| `$WS/OpenRTX/platform/targets/c62/hwconfig.h` | C62 hardware config (CONFIG_M17, CONFIG_P25, etc.) |
| `$WS/OpenRTX/platform/targets/c62/CMakeLists.txt` | C62 build sources and include paths |
| `$WS/OpenRTX/platform/drivers/baseband/radio_C62.cpp` | BK4819 radio driver (FM/M17/P25) |
| `$WS/OpenRTX/platform/drivers/audio/audio_c62.c` | Audio routing (source/sink matrix) |
| `$WS/OpenRTX/platform/drivers/NVM/nvmem_C62.c` | Flash-backed NVM (settings + VFO persistence) |
| `$WS/OpenRTX/openrtx/src/ui/default/ui.c` | Main UI FSM |
| `$WS/OpenRTX/openrtx/src/ui/default/ui_menu.c` | Menu rendering |
| `$WS/OpenRTX/openrtx/include/rtx/rtx.h` | RTX status struct and opmode enum |
| `$WS/OpenRTX/openrtx/include/core/settings.h` | Settings struct (packed) |
| `$WS/OpenRTX/openrtx/include/core/threads.h` | Thread stack sizes |
| `$WS/docs/flashing.md` | Flashing procedure and partition map |
| `$WS/docs/pr-checklist.md` | Upstream PR checklist |
| `$WS/docs/status.md` | Build status and reference-firmware lineage |
| `$WS/docs/p25.md` | P25 operation, settings, verification and TX bug history |

---

## 9. Pulling upstream updates

```bash
source $WS/env.sh
cd $WS/OpenRTX
git fetch origin
git fetch lurk
git fetch fork
# Check for changes on tracked branches
$WS/upstream-watch/check.sh
```

Tracked branches: `origin/Retevis-c62`, `origin/Retevis-c62` (rtxlink),
`lurk/p25-pr`, `fork/c62-nvmem-persistence`.

---

## 10. Upstream PR workflow

All upstream PRs must follow `docs/pr-checklist.md`:
1. One logical change per PR
2. Tidy commits (squash fixups)
3. Read CONTRIBUTING.md
4. Fill out the PR template (Summary, Related issue, Design notes, Testing, Changelog, Checklist)
5. Add "Retevis C62" to the test-target list
6. Post review responses via REST API (`gh api -X PATCH`) — `gh pr edit` fails with Projects-classic deprecation

Current open PRs:
- **#542** — M17 voice (48 kHz DSP) — open, mergeable
- **#550** — CI build for C62 — open, mergeable
- **#551** — Flash-backed nvmem storage — open, mergeable, awaiting review
