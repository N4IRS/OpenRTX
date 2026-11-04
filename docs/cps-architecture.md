# CPS architecture — C62 first, N modes from the start

Principle adopted 2026-10-28: **qdmr is the CPS, `.rtxc` is the contract, and
every mode we may add later (P25, NXDN, D-STAR, …) must be additive.**

The C62 implementation is the first consumer, not the design target.

## Layers

```
  UI (on-radio)        qdmr (desktop)        CLI tools (dump/convert)
        \                    |                     /
         \                   |                    /
          ──────  model: channel_t / contact_t / banks  ──────
                              |
                    codec: .rtxc encode/decode
                              |
                    storage: cps_io (flash_area)
                              |
        transport: plain file (cskburn)  |  rtxlink (SLIP/FMP/XMODEM)
```

Rules that keep modes out of the plumbing:

- `cps_io` knows **blocks**, not channels. No mode logic.
- The **codec** is the only place that knows record layouts.
- Mode-specific behaviour lives in **one descriptor table**, never in switch
  statements scattered across UI code:

```c
struct cpsModeOps {
    uint8_t     mode;          /* Mode_FM, Mode_DMR, Mode_M17, Mode_P25, ... */
    const char *name;
    size_t      infoSize;      /* bytes used inside the mode info block      */
    void      (*defaults)(void *info);
    int       (*validate)(const void *info);
};
```

- The UI enumerates channels generically and renders the mode block through
  the descriptor; adding a mode adds a row, not a code path.

## Mode inventory — what actually exists (verified 2026-10-28)

A claim circulates that the radio "does FM, M17, P25, APRS, DMR and TETRA".
Checking the repositories:

| Mode | Status | Where |
|---|---|---|
| **FM** | ✅ shipped | `origin/master` `OpMode_FM.cpp`; works on C62 |
| **M17** | ✅ shipped, ⚠️ C62 blocked | `origin/master` `OpMode_M17.cpp`; C62 blocked on audio/DSP |
| **P25** | 🟡 implement­ed in a PR | `lurk/p25-pr` → our `p25-c62`; not merged upstream |
| **APRS** | 🟡 experimental branch | `origin/aprs`: `OpMode_APRS.cpp` + `protocols/APRS/*` (decoder, demod, slicer, packets) |
| **DMR** | ❌ no implementation | only vestigial `dmrInfo_t` and UI strings remain in `cps.h`/`EnglishStrings.h`; **no `OpMode_DMR`** |
| **TETRA** | ❌ nothing | no code, anywhere |

So the realistic set to design for is **FM, M17, P25, APRS**, with DMR as a
maybe and TETRA as headroom only.

### Per-mode info block requirements

| Mode | Needs | Rough size |
|---|---|---|
| FM | RX/TX CTCSS index + enables | 2 B |
| DMR | contact index, colour code, timeslot | 4 B |
| M17 | RX/TX CAN, mode, encryption, GPS, contact index | 5 B |
| P25 | NAC (12 b), target TGID/unit ID (24 b), phase, encryption, contact | ~8 B |
| APRS | beacon enable, path/comment reference | ~2–4 B |
| TETRA (if ever) | MCC/MNC, SSI/GSSI (24 b), group vs individual, encryption class, slot | ~12–16 B |

A **16-byte reserved block** covers every listed mode with headroom, and
keeps `sizeof(channel_t)` fixed for the lifetime of the format.

APRS is worth calling out: it is an *operating mode* in the `aprs` branch
(`OpMode_APRS`), not just a data application, so it needs a channel mode value
in the codeplug like the others.

### Also found: rtxlink is already implemented

`origin/rtxlink` contains a complete firmware implementation of the protocol
qdmr expects:

```
openrtx/src/core/rtxlink.c       openrtx/src/core/slip.c
openrtx/src/core/rtxlink_cat.c   openrtx/src/core/xmodem.c
openrtx/src/core/rtxlink_dat.c   openrtx/src/core/backup.c
openrtx/src/core/rtxlink_fmp.c   tests/unit/slip.c
```

including a commit "Add missing source files to CMakeLists.txt for zephyr
targets" — i.e. it is already being made to work on Zephyr platforms like the
C62. That branch, not a bespoke protocol, is the live-programming path: it is
what qdmr's `openrtx_interface.cc` speaks (SLIP + FMP + XMODEM over a serial
port).

### Compile-time layout guard

The `// 59B` comments in `cps.h` are not authoritative. The codec should carry
static assertions on every record size:

```c
_Static_assert(sizeof(channel_t) == CHANNEL_RECORD_SIZE, "codeplug layout drift");
_Static_assert(sizeof(contact_t) == CONTACT_RECORD_SIZE, "codeplug layout drift");
```

so any future change to a struct fails the build instead of silently
corrupting existing codeplugs.

## Mode ID registry (superseded)

> **Superseded 2026-10-28:** mode numbering now comes from **ptSCD**,
> which already defines FM, M17, DMR, P25, NXDN, Tetra, APRS and more,
> plus 250–255 for custom modes. See [`ptscd-notes.md`](ptscd-notes.md).
> The table below is kept for historical reasoning only.


Decision (2026-10-28): **build to the full claimed capability.** Mode IDs are
assigned now, permanently, so no future mode can force a renumbering — even
though several are not implemented yet.

| ID | Mode | Status today | Info block use |
|---|---|---|---|
| 0 | `None` | reserved | — |
| 1 | `FM` | implemented | 2 B (CTCSS RX/TX index + enables) |
| 2 | `DMR` | **not implemented** (no opmode) | 4 B (contact, colour code, timeslot) |
| 3 | `M17` | implemented (C62 blocked) | 5 B (CANs, mode, encr, GPS, contact) |
| 4 | `P25` | PR (`p25-pr`) | ≤ 16 B (NAC, TGID/unit ID, phase, encr, contact) |
| 5 | `APRS` | branch (`aprs`) | ≤ 16 B (beacon enable, path ref) |
| 6 | `TETRA` | **not implemented** | ≤ 16 B (MCC/MNC, SSI/GSSI, group, encr, slot) |
| 7 | `NXDN` | **not implemented** | ≤ 16 B (RAN, group/individual, unit ID, encr) |
| 8+ | reserved | — | for whatever comes next |

Rules that make this safe:

- **Never renumber.** A mode ID, once published, is permanent. Unimplemented
  IDs are simply rejected by the firmware and preserved by editors.
- **Uniform record size.** Every mode consumes the same fixed block, so
  `sizeof(channel_t)` and `sizeof(contact_t)` are constant forever.
- **Unknown modes are preserved, not dropped**, so a codeplug authored on a
  newer tool round-trips through an older radio unchanged.

## Reserved block sizing

| Record | Today | Target |
|---|---|---|
| `channel_t` mode info | union, max 5 B | **16 B** reserved block |
| contact info | union, max 6 B | **6 B address + type byte** (contacts become a 10 B record) |

Measured sizes and the full per-mode field budget are in
[`cps-storage-budget.md`](cps-storage-budget.md). Summary of the v0.2 shape:

| Record | Today | v0.2 |
|---|---|---|
| Channel | 94 B, inline `name[32]` + `descr[32]` | **43 B** + ~9 B in the name pool |
| Contact / talkgroup | 39 B, inline `name[32]` | **10 B** + ~9 B in the name pool |

The mode block stays 16 B because the largest plausible mode (TETRA: MCC/MNC,
24-bit SSI/GSSI, group/individual, encryption class, slot) needs 9 B today and
must never force a record resize again.

Ship this as **`.rtxc` version 0.2**, once, in the same series as the qdmr
`SupportedVersion` update. After 0.2, adding a mode never changes the layout.

## Vocoder reality check

DMR and NXDN share a vocoder, which is a genuine economy — but the vocoder is
also the gate.

| Mode | Voice codec | Status in our tree |
|---|---|---|
| M17 | Codec2 3200 | ✅ `subprojects/codec2` (clean, LGPL) |
| P25 Phase 1 | IMBE 4400 | ✅ vendored (`imbe_audio_codec.cpp`) |
| **DMR** | AMBE+2 3600 half-rate | ❌ absent |
| **NXDN** | AMBE+2 3600 half-rate | ❌ absent |
| **P25 Phase 2** | AMBE+2 3600 half-rate | ❌ absent |
| **TETRA** | TETRA ACELP (EN 300 395-2) | ❌ absent — **not AMBE**, separate codec and patent question |

`protocols/P25/AMBEFEC.*` is G4KLX's **AMBE forward error correction**, not a
vocoder — it fixes up corrupted AMBE frames, it cannot synthesise speech.

Consequences:

- **DMR and NXDN share the same AMBE+2 3600 bps half-rate codec**, so one
  implementation serves both. Adding NXDN after DMR costs modulation and
  protocol work, not a second codec.
- AMBE+2 is DVSI-licensed and patent-encumbered. There is no clean
  open-source implementation; the existing reverse-engineered ones are
  legally contentious. That is the same gate that blocks DMR and P25 Phase 2,
  and it is a policy decision, not an engineering one.
- Modem side: DMR (2-slot TDMA 4FSK, 4800 baud) and NXDN (4FSK, 2400 baud
  for 6.25 kHz / 4800 baud for 12.5 kHz) are both 4FSK, as is M17 — so the
  C62 DSP work needed for M17 is the common prerequisite and testbed.

CPS impact: NXDN needs only a mode ID and ~8 bytes of per-channel info
(RAN, group/individual, unit ID, encryption), so reserving `Mode_NXDN = 7`
now costs nothing.

## DMR PoC precedent (`turnrye/dmr-poc`, examined 2026-10-28)

A real DMR proof-of-concept exists on a branch of turnrye's fork:

```
openrtx/src/rtx/OpMode_DMR.cpp
openrtx/src/protocols/DMR/{CallState,LinkControl,TxSchedule,Datatypes}.*
platform/drivers/baseband/dmr_UV3x0.cpp     (HR-C6000, MD-UV3x0)
platform/drivers/baseband/dmr_linux.c       (emulator)
platform/drivers/baseband/dmr_codec.{c,h}   (vocoder interface)
tests/unit/DMR_{callState,linkControl,txSchedule}.cpp
```

### How it solves the vocoder

Not with a clean-room codec — with a **donor blob**:

- `scripts/dmr_installer.py` extracts the AMBE+2 codec from a *user-supplied*
  donor firmware (TYT MD9600 V26.45) and merges it into the OpenRTX image.
- The blob runs at a fixed flash address `0x0807537C`, with hard-coded Thumb
  entry points (`0x08075600`, `0x08075954`, …) and fixed state buffer sizes.
- Nothing is redistributed: the script contains no codec bytes, only offsets.
- Without the blob installed, DMR degrades to header-only RX with no voice.

### Two consequences for us

**1. The interface is the seam we wanted.** `dmr_codec.h` exposes
`dmr_codec_available()`, init, encode and decode — precisely the shape a
clean-room AMBE could implement. Our `voiceCodec` abstraction should adopt
this interface rather than invent another.

**2. The blob cannot run on the C62.** It is position-dependent Cortex-M4
(ARMv7-M) code fixed at `0x0807xxxx` in an image loaded at `0x0800C000`. The
C62's application core is a **Cortex-M33 (ARMv8-M)** with an entirely
different flash map (`0x18000000`, partitions at `0x2B0000`+). There is no
path to re-target a binary blob without its source.

So for the C62: **DMR and NXDN voice remain gated on a portable codec**
(clean-room AMBE or an equivalent reimplementation). The donor-blob route is
viable only on the STM32F4-based MDx family.

### Codeplug impact: the PoC did not change `cps.h`

`git diff origin/master turnrye/dmr-poc -- openrtx/include/core/cps.h` is
empty; only `settings.h` grew (+12 lines). DMR configuration lives in **global
settings**, not per channel — exactly as P25 does today.

That is a useful correction to our v0.2 plan:

- Reserving a fixed-size mode block remains sound and cheap.
- But **do not define `dmrInfo_t`/`p25Info_t`/`nxdnInfo_t` structures yet** —
  no implementation currently needs per-channel mode data, and guessing the
  fields now would lock in a design before a consumer exists.
- Reserve the bytes; fill them when a mode actually needs them.

## Codec abstraction (prerequisite for any third codec)

Current state is duplicated, not abstracted:

| File | Codec | Notes |
|---|---|---|
| `core/audio_codec.c` | Codec2 3200 | hard-wired `codec2_create/encode/decode` |
| `core/imbe_audio_codec.cpp` | IMBE 4400 | a second, parallel manager with its own `imbe_init/startEncode/...` |

Adding AMBE as a third copy would make this worse. Plan a small refactor
first — it is independent of whether a clean AMBE ever appears, and it
benefits the existing P25 work:

```c
struct voiceCodec {
    const char *name;
    uint16_t    sampleRate;
    uint16_t    frameSamples;   /* PCM samples per frame */
    uint16_t    frameBytes;     /* compressed bytes per frame */
    int       (*init)(void);
    int       (*encode)(const int16_t *pcm, uint8_t *frame);
    int       (*decode)(const uint8_t *frame, int16_t *pcm);
    void      (*terminate)(void);
};
```

- one manager selects the codec from the active channel mode
- Codec2 and IMBE become two instances of the same interface
- each codec is gated by `CONFIG_CODEC_*` so a build can omit the ones it
  cannot ship
- an AMBE implementation, if it ever exists, is a fourth instance — not a
  fourth copy of the manager

## AMBE contingency — planned for, not depended on

Position: **no work in the CPS or mode registry depends on AMBE.**

| If a usable clean-room AMBE lands | If it never does |
|---|---|
| Mode_DMR (2) and Mode_NXDN (7) become implementable | the IDs stay reserved and unused |
| P25 Phase 2 becomes implementable | Phase 1 keeps working via IMBE |
| 4FSK modem + DSP work is still required (shared with M17) | no format change, no rework |
| TETRA remains separate — ACELP, not AMBE | — |

What we do *now*, at near-zero cost:

1. Keep mode IDs 2 and 7 reserved (done).
2. Keep the 16 B block large enough for DMR/NXDN channel data (done).
3. Plan the codec abstraction above so a fourth codec is a table entry.
4. Keep a golden-frame test corpus approach so a candidate AMBE can be
   evaluated against real captured frames, not synthetic vectors.
5. Nothing in `cps_io`, the codec manager, or the UI may assume the set of
   codecs is fixed.

## The trap: fixed-size records

Channels are stored as a **dense array of fixed-size records**. `channel_t` is
`__attribute__((packed))` and, measured from the built ELF, **94 bytes**, with
a union of `fmInfo_t` (2 B), `dmrInfo_t` (4 B) and `m17Info_t` (5 B) — the
union is 5 B. (The `// 59B` comment in `cps.h` is stale.)

A P25 info block (NAC, target TGID/unit ID, phase, encryption) exceeds 5 B, so
growing the union changes `sizeof(channel_t)` and shifts every channel after
the first — a breaking layout change, not an additive one. That forces a
`CPS_VERSION_MAJOR` bump unless we act first.

### Reserve space now

Before the format spreads (it is at `0.1`, implemented only by us and qdmr):

- Replace the bare union with a **fixed 16 B mode block**; existing modes use
  the first 2/4/5 bytes, the rest is zero padding.
- Contacts get a uniform **6 B address + type byte** instead of a per-mode
  union, which is why the contact record shrinks from 39 B to 10 B once names
  leave the record.
- Ship as **version 0.2**, once, with the qdmr `SupportedVersion` update.
- Afterwards every new mode is purely additive and record sizes never change.

The 16 B figure is not arbitrary: TETRA needs 9 B today, P25 needs 8 B, so
anything smaller leaves no room for a mode we already know is coming.

## Unknown-mode policy (forward compatibility)

- Readers must **preserve** channels whose mode they do not recognise,
  writing them back byte-for-byte, rather than dropping them.
- The descriptor table returns "unsupported" so the UI shows the channel as
  unavailable instead of misrendering it.
- Never renumber existing mode values; append new ones.

This is what lets an older firmware or older qdmr survive a codeplug produced
by a newer one — provided the record size is stable, which is why the reserved
block matters.

## Testing

- Keep **golden `.rtxc` fixtures** in the repo: one per mode, plus a mixed
  codeplug. Firmware unit tests, the CLI tools and qdmr should all round-trip
  them identically.
- A round-trip test only passes if `decode(encode(x)) == x` **and**
  `encode(decode(y)) == y` for a known-good binary fixture — the second form
  catches layout drift that the first misses.

## Checklist for adding a mode

1. Add the descriptor row (mode value, info size, defaults, validation).
2. Fill the mode info block — never resize `channel_t`.
3. Extend the UI: channel type label, edit screen, main-screen rendering.
4. Extend rtx configuration mapping (`threads.c`) to read from the channel,
   not from global settings.
5. Bump `CPS_VERSION_MINOR`; update qdmr `SupportedVersion` in the same PR
   series.
6. Add a golden fixture and a round-trip test.
7. Verify an **older** build still boots and preserves the new channel.

## Open decisions

1. ~~Mode info block size: 8 B or 16 B~~ → **16 B** (covers TETRA headroom).
2. ~~Reserve now as v0.2, or let P25 force a major bump later~~ → **reserve now, v0.2.**
3. ~~Whether contacts get reserved treatment~~ → **yes, 16 B in the same revision.**
4. Remaining: when to schedule the v0.2 layout change relative to the C62
   `cps_io` work, and whether to land it upstream before or together with
   qdmr's `SupportedVersion` bump.
