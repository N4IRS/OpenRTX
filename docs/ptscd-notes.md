# ptSCD — the adopted codeplug standard

Source: <https://github.com/mdiepart/ptscd> (`doc/main.md`, `sim/`).
Examined 2026-10-28, after the OpenRTX core team confirmed this is the
direction and that qdmr's maintainer will absorb future codeplug version
migrations.

**This supersedes the `.rtxc` v0.2 record design in
[`cps-storage-budget.md`](cps-storage-budget.md) and the mode registry in
[`cps-architecture.md`](cps-architecture.md).**

## What it is

A standardised codeplug description for low-resource radios. Two parts:

- **Part 1** — read/write, implemented over a **filesystem** (littlefs in the
  reference simulator). Five files: `ptscd.dat` (header + blob), `channels`,
  `contacts`, `groups`, and a blob file. Fixed-size entries to limit
  fragmentation.
- **Part 2** — read-only condensed form, can live bare-metal.

The device-side answer to our open question: **one file for channels**
(`channels`), one for contacts, one for groups, with names and other variable
data in the blob file.

## Header

`ptscd.dat` begins with the text `PTSCD`, a space, the version as
`major.minor` (currently `0.1`), the part (`p1`/`p2`), then — for part 1 — a
list of up to three null-terminated ASCII filenames (max 32 chars) in the
order `channels`, `contacts`, `groups`. A zero-length name means the codeplug
holds no such entries *and the radio cannot create them*, so a writable
codeplug needs an empty file listed.

## Record layouts

### Channel — 17 B

| Field | Size | Notes |
|---|---|---|
| ID | 2 B | **odd = active, even = deleted** (in-place delete, no compaction) |
| FREQ RX | 4 B | |
| FREQ TX | 4 B | |
| Squelch | 1 B | scale TBD |
| Power | 1 B | scale TBD |
| Mode | 1 B | see enum below |
| QA params | 2 B | mode-dependent quick-access data |
| BLOB_ID | 2 B | into the blob file |

### Group — 4 B

| Field | Size | Notes |
|---|---|---|
| ID | 2 B | |
| BLOB_ID | 2 B | blob holds the channel list, ~32 entries |

### Contact — **not yet defined**

The spec explicitly lists "Define content of contact entry" as outstanding.

### Blob

`BLOB_SIZE` (1 B) + `BLOB_DATA` (variable). Referenced by `BLOB_ID` from
channels, groups and (eventually) contacts.

## Mode enum

ptSCD already enumerates the modes we cared about, plus many more:

```
0 NBFM · 1 FM · 2 BCFM · 3 USB · 4 LSB · 5 CW · 6 RTTY · 7 APRS · 8 M17
9 DStar · 10 DMR · 11 YSF(NB) · 12 YSF(WB) · 13 P25 · 14 NXDN · 15 Tetra
16 POCSAG · 17 dPMR · 18 PMR446 · 250–255 custom/implementation-specific
```

That removes our mode-registry problem entirely: **we adopt their numbering**
rather than inventing one, and 250–255 exist for anything experimental.

### Per-mode QA params and blobs (as documented)

| Mode | QA params | Blob |
|---|---|---|
| FM | CTCSS RX (6 b) + CTCSS TX (6 b) | Name |
| M17 | CAN (3 b) | Destination |

Everything else is TBD — the spec's own "what needs to be done" list.

## Capacity, recalculated

Using ptSCD entry sizes, 8-character names (blob = 1 B size + 8 B text):

| Record | Entry | Blob | Effective |
|---|---|---|---|
| Channel | 17 B | 9 B | **26 B** |
| Contact (proposed 10 B) | 10 B | 9 B | **19 B** |
| Group | 4 B | 1 + 2×members | — |

### 4,000 channels · 250 groups · 10,000 named contacts

```
channels   4,000 × 26          = 104,000 B
contacts  10,000 × 19          = 190,000 B
groups      250 × 4 + indices  ≈   9,250 B
header + filenames             ≈     200 B
------------------------------------------
total                          ≈ 303.4 KB
```

| Partition | Spare |
|---|---|
| 320 KiB | ~24 KB (tight) |
| **384 KiB** | **~88 KB** |
| 448 KiB | ~152 KB |

ptSCD's 17 B channel record is **40 % smaller** than our 43 B design, so a
384 KiB partition now covers the full 10,000-named-contact target with a
comfortable reserve. Keeping 448 KiB would leave ~152 KB spare, which is
reasonable for littlefs metadata and growth.

## Our contributions to the working group

The spec has explicit gaps where we already have design work:

1. **Contact entry layout.** We proposed 10 B: `name_index`/`blob_id` (2 B),
   `addr_type` (4 b) + `call_type` (2 b) + flags (2 b), 6 B address. That
   fits ptSCD's fixed-entry style and covers DMR/M17/P25/NXDN/TETRA
   addresses in one uniform width.
2. **QA params for non-FM/M17 modes** — DMR colour code/timeslot, P25 NAC,
   NXDN RAN, TETRA MCC/MNC/SSI. The 2 B budget will need review: TETRA alone
   needs ~9 B, so QA params may need to grow or move into blobs.
3. **BLOB_ID width.** The spec itself worries that 2 B IDs cap usable flash
   at ~1.25 MB with 2 B IDs. For 4,000 channels + 10,000 contacts we are
   fine (~14,000 blobs), but it is worth settling early.
4. **Squelch/power scales** — needs a defined mapping, otherwise portability
   breaks at the first radio that disagrees.

## What this means for us

- **Do not build the 43 B / 10 B `.rtxc` v0.2 layout.** Adopt ptSCD Part 1
  instead. Most of the binary record work is unnecessary.
- **Storage backend becomes littlefs on a partition**, not raw offsets.
- **Names are already pooled** in the standard, which is exactly what our
  budget analysis concluded was necessary.
- **Modes are already enumerated**, including P25, NXDN and Tetra.
- The C62's storage plan reduces to: a littlefs partition, a ptSCD reader/
  writer, and the Channel Memories UI.

## Open questions for the working group

1. Contact entry layout (we have a proposal).
2. QA param budget per mode — 2 B is tight for P25/TETRA.
3. Blob ID width for future large-flash radios.
4. Squelch and power value scales.
5. Whether "one file for channels" implies a hard channel cap per file, or
   file growth is expected on device.
