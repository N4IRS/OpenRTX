# Build environment

Everything runs **without root** (`sudo` password is not available). All
components live under `/srv/WorkSpace/OpenRTX-C62`.

## Components

| Component | Location / version | Purpose |
|---|---|---|
| Python venv | `.venv/` – west 1.5, meson, `cmake==3.31.10` (**pip shim**), ninja, zephyr script deps | CMake **must be < 4.x**: CMake 4 breaks Zephyr 3.4 `FindZephyr-sdk.cmake` |
| Zephyr SDK | `zephyr-sdk-0.16.8/` – *linux-x86_64_minimal* bundle + `arm-zephyr-eabi` toolchain via its `setup.sh -t arm-zephyr-eabi -c` | OpenRTX CMake pins SDK 0.16 via `ZEPHYR_16_SDK_INSTALL_DIR` (`OpenRTX/CMakeLists.txt`) |
| west workspace | created with `west init -l OpenRTX` then `west update --group-filter +csk6` | pulls `zephyr-listenai` (Zephyr 3.4 fork), `listenai-csk`, `csk6_cm33` HAL, AF, freertos_shims, LSF, URPC |
| Zephyr base | `west config zephyr.base zephyr-listenai` | the CSK6 support lives in the fork, not mainline Zephyr |
| codec2 (vendored) | `OpenRTX/subprojects/codec2` = clone of `drowe67/codec2-dev` @ `a298f37`, overlaid with `OpenRTX/subprojects/packagefiles/codec2/*` | normally done by meson subproject (wrap); done manually here because we build via `west` directly |
| flashing tool | `cskburn` v1.11.6 at `~/.npm-global/bin` (`npm i -g @listenai/cskburn`; the bare `cskburn` npm name no longer exists) | burns the two flash partitions |

## Rebuilding the toolchain from scratch

```bash
python3 -m venv /srv/WorkSpace/OpenRTX-C62/.venv
/srv/WorkSpace/OpenRTX-C62/.venv/bin/pip install \
    west pyusb meson intelhex==2.3.0 setuptools cmake==3.31.10 ninja
/srv/WorkSpace/OpenRTX-C62/.venv/bin/pip install \
    -r /srv/WorkSpace/OpenRTX-C62/zephyr-listenai/scripts/requirements.txt

# Zephyr SDK 0.16.8 (arm only):
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
tar xf zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
cd zephyr-sdk-0.16.8 && PATH=<venv>/bin:$PATH ./setup.sh -t arm-zephyr-eabi -c

# codec2 vendoring:
git clone https://github.com/drowe67/codec2-dev OpenRTX/subprojects/codec2
cd OpenRTX/subprojects/codec2 && git checkout a298f3789d7ef9e829049ebe14da8845d97ba8c1
cp ../packagefiles/codec2/meson.build . && mkdir -p codec2 && cp ../packagefiles/codec2/codec2/version.h codec2/
echo "subprojects/codec2/" >> ../../.git/info/exclude   # keep git tree clean
```

`env.sh` then exports: `WS`, `PATH` (venv + `~/.npm-global/bin`),
`ZEPHYR_16_SDK_INSTALL_DIR`, `ZEPHYR_SDK_INSTALL_DIR`, `ZEPHYR_BASE`,
`BOARD=c62`.

## Gotchas learned the hard way

1. **Workspace structure is fixed by the manifest**: `west.yml` has
   `self: path: OpenRTX`, so the clone *must* sit as `OpenRTX/` inside the
   workspace root. Running `west init -l .` in the clone directly spreads
   module repos into the *parent* directory (i.e. `$HOME`!). Keep the
   workspace layout as-is.
2. **Moving the workspace breaks the venv and SDK registry** (absolute paths).
   Recreate the venv and re-run SDK `setup.sh -c` after any move.
3. **cmake < 4** – keep the pip shim at 3.31.x or Zephyr 3.4 fails during
   `find_package(Zephyr-sdk)`.
4. Stale `build.c62/` after missing tools caches wrong `CMAKE_MAKE_PROGRAM` –
   `rm -rf build.c62` before retrying a full configure.

## Pulling upstream updates

```bash
source /srv/WorkSpace/OpenRTX-C62/env.sh
cd /srv/WorkSpace/OpenRTX-C62/OpenRTX
git pull                      # branch Retevis-c62
west update --group-filter +csk6
```

Note: `CONFIG_M17` in `platform/targets/c62/hwconfig.h` is a local edit and
will conflict if upstream touches that line (the merged branch has it
commented out deliberately — M17 TX hang, see `docs/status.md`).
