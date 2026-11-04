# Release split: memories first, CPS editing second

Status: proposal, 2026-10-28.

> **Upstream alignment (2026-10-28):** the core team has independently
> announced the same split. See "Upstream alignment" at the end of this
> document — it changes two of our assumptions (filesystem, shared USB).

The work divides along a natural seam: making the radio **hold and use
memories** is a firmware problem with a closed loop we can test on hardware;
**CPS editing** is a host-tool and protocol problem that depends on other
projects' release cadence. Bundling them means the storage layer waits for
someone else's schedule.

## Why split

| | R1 — Memories | R2 — CPS editing |
|---|---|---|
| Deliverable | radio stores, lists and selects channels | desktop editing of channels, contacts, zones |
| Depends on | nothing external | qdmr format agreement, later rtxlink |
| Risk | flash layout, codec, UI | third-party coordination, protocol |
| Testable on hardware | yes, immediately | only after R1 |
| Blocked by M17/P25 audio work | no | no, but richer modes need it |

Splitting lets the format be **proven on real hardware** before anyone edits
it, and gives R2 a stable target instead of a moving one.

## R1 — Memories

### Goal

The radio boots with a codeplug from flash, shows a channel list, and lets the
user select a memory and transmit on it. Today it cannot store a channel at
all (`cps_io_stub`).

### In scope

1. **Partition** — add `cps_partition` (448 KiB at `0x2B2000`) and shrink
   `storage_partition` to 8 KiB. Existing settings/VFO records stay valid.
2. **Codec** — `.rtxc` v0.2 encode/decode: header, channels (43 B + name
   pool), contacts (10 B), zones. Static assertions on record sizes.
3. **`cps_io` backend** — `cps_open` (validate magic + version),
   `cps_readChannel`, `cps_readContact`, zone accessors, plus a write path
   used for first-boot defaults.
4. **First-boot defaults** — if the partition is blank or invalid, create a
   minimal codeplug (a few FM channels + a local TG) so the UI has something
   to show.
5. **Channel list UI** — list, page, select; display name, RX/TX, mode.
   Memory ⇄ VFO switching.
6. **Import path without a CPS** — the release notes document:
   - `cskburn -s /dev/ttyUSB0 -C 6 -b 115200 0x2B2000 codeplug.bin`, and
   - a small `cpsgen` script that emits a `.rtxc` from a CSV/YAML source.
   This is deliberately *not* a CPS: no GUI, no live link, no validation UI.
7. **Tests** — unit tests for encode/decode round-trip against golden
   fixtures; emulator (`linux` target) channel-list test; hardware check:
   flash a codeplug, browse 100+ channels, select, transmit.

### Out of scope

- Any GUI or live PC connection.
- Zones UI beyond whatever the list needs to filter.
- Per-channel M17/P25/DMR configuration (the storage supports it; nothing
  consumes it yet, and M17/P25 are blocked on audio).
- Parsing the OEM `factory_chan` codeplug — a separate reverse-engineering
  project, and not needed to boot.

### Definition of done

- Radio boots from a flashed codeplug and shows its channels.
- Selecting a memory retunes and transmits on FM.
- Blank/blanked partition falls back to defaults instead of bricking.
- Format documented; golden fixtures committed.

## R2 — CPS editing

### Goal

Edit the same codeplug from a desktop, without hand-flashing.

### In scope

1. **qdmr v0.2 support** — upstream `SupportedVersion` bump and the reserved
   block layout.
2. **File workflow** — qdmr opens/saves `.rtxc`; user writes it to the radio
   with `cskburn`. No firmware change needed.
3. **rtxlink** — port the existing branch (SLIP + FMP + XMODEM + CAT) to the
   C62, over UART first. Live download/upload from qdmr.
4. **Rich editing** — contacts/talkgroups, zones, channel templates,
   frequency/tone validation against the radio's bands and power limits.

### Out of scope

- New modes. R2 edits what the firmware can already do.

## What R1 must get right so R2 is cheap

1. **Freeze v0.2 now.** Reserved 16 B mode block, 10 B contacts, pooled
   names. If the format is still moving when R2 starts, every codeplug
   written in R1 breaks.
2. **Publish the spec and fixtures early.** qdmr compatibility is easier to
   negotiate with a working encoder and a set of golden files than with a
   design document alone.
3. **Keep `cps_io` mode-agnostic.** R2 adds modes to the editor; it must not
   require storage changes.
4. **Never write a codeplug in the old layout.** R1 is the first writer, so
   there is no migration debt.

## Rough effort

| Task | R1 | R2 |
|---|---|---|
| Partition + storage | 1–2 d | — |
| Codec + unit tests | 2–3 d | — |
| `cps_io` backend | 1–2 d | — |
| Channel list UI | 1–2 d | — |
| Defaults + import script | 1 d | — |
| Hardware validation | 1–2 d | — |
| qdmr v0.2 upstream | — | days, external |
| File workflow | — | ~1 wk |
| rtxlink port | — | 1–2 wk |
| Editing UI polish | — | ~1 wk |

R1 is roughly **2–3 weeks part-time** and has no external dependency.

## Risks

- **Format churn** if qdmr wants a different v0.2 shape. Mitigated by
  publishing the spec and fixtures first.
- **UI capacity** — listing 4,000 channels needs paging; check the existing
  channel UI can handle it before committing to the count.
- **Flash write time** — erasing a 4 KiB sector per save is fine for
  first-boot defaults; edits from the radio itself (if ever added) need a
  write strategy.
- **Name pool sizing** — the 8-char assumption is an estimate; measure real
  codeplugs during R1.

## Recommendation

Ship R1 as its own PR series (partition, codec, `cps_io`, UI), then pause
before R2 until qdmr confirms v0.2. R1 is independently useful: it turns the
radio from "VFO only" into "holds your channels", which is the single biggest
functional gap today.

---

## Upstream alignment (2026-10-28)

The OpenRTX core team has announced the same split independently, as
**"Channel Memories"**, to start after the next release:

> save whatever you are tuned to as a named memory, browse memories from the
> Channels menu, tune any of them, delete the ones you do not need; memories
> persist across power cycles and firmware updates. Zones, contacts and
> computer-side management come in a later release.

### What matches our plan

| Our proposal | Upstream |
|---|---|
| R1 = memories only, on-radio | Channel Memories |
| R2 = CPS editing later | "bigger picture ... later release" |
| Separate storage partition, survives flashing | "stay put across power cycles and firmware updates" |
| Host tool for backup/restore | "back up your radio before anything changes ... put that backup back" |
| Live protocol for editing | shared USB stack |

### What changes for us

**1. A filesystem, not a bare partition.** Upstream is "adopting a
filesystem early" to unlock localization, message persistence and M17 signing.
Our plan assumed a raw `.rtxc` array at a fixed partition offset. A filesystem
does not remove the need for a codeplug *format* — it changes where and how
it is stored, and may replace fixed-offset records with files.

Open question to settle before writing `cps_io`: does the codeplug live as a
single file inside the filesystem (format preserved, our v0.2 work applies),
or as one file per memory (format simplified, bulk import becomes a directory
operation)? The answer decides whether the 43 B / 10 B record work is needed.

**2. Shared USB stack.** Upstream is unifying USB and bringing it to
CS7000-PLUS for the first time. That removes the risk we flagged around C62
USB bring-up (`usbotg` currently disabled) and makes rtxlink-over-USB the
likely live path instead of our UART-first fallback.

**3. Do not build a competing host tool.** Upstream will ship a backup/copy/
restore tool. Our `nvram-backup.sh` / `cpsgen` scripts are useful now, but
should be treated as interim: contribute to the official tool rather than
maintain a parallel one.

### What does not change

- **The C62 still needs persistence.** Our PR #551 (settings + VFO) is part
  of the same "persistence layer" upstream is finishing; it is not wasted
  work either way.
- **Mode-agnostic storage still matters.** Whatever the container is,
  channels must not assume a fixed set of modes.
- **The 448 KiB sizing** is still a reasonable budget for a memory/codeplug
  region, filesystem overhead aside.

### Revised immediate steps

1. Ask upstream which filesystem and what the codeplug container looks like
   before implementing `cps_io`.
2. Keep PR #551 moving — it is persistence-layer work upstream wants.
3. Do not start the `.rtxc` v0.2 record surgery until the container question
   is answered; if memories become individual files, most of that work is
   unnecessary.
4. Watch for the host tool landing, and test the C62 against it rather than
   building our own.

---

## Standard adopted: ptSCD (2026-10-28)

The codeplug standard is **ptSCD** <https://github.com/mdiepart/ptscd>,
currently at `0.1`, with a working group forming to settle the first
adoption. qdmr's maintainer has confirmed he will absorb codeplug version
migrations, so the format may evolve without stranding users.

Consequences for this plan, replacing earlier assumptions:

| Assumption | Now |
|---|---|
| Binary `.rtxc` array, fixed offsets | **ptSCD Part 1 over littlefs** |
| One packed file for everything | **One file for channels**, plus contacts, groups, blobs |
| Our 43 B channel record | **ptSCD 17 B channel** (40 % smaller) |
| Our mode registry | **ptSCD mode enum**, incl. P25/NXDN/Tetra, plus 250–255 custom |
| Names in our own pool | **ptSCD blob file** — the standard already pools them |

R1 "Channel Memories" therefore becomes: a littlefs partition, a ptSCD
reader/writer, and the Channels menu UI. The record-design work is dropped;
capacity now comfortably fits 4,000 channels + 10,000 named contacts in
384 KiB (see [`ptscd-notes.md`](ptscd-notes.md)).
