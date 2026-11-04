# Ideas from RadioFoundry

Source: <https://github.com/radiofoundry/RadioFoundry> (MIT, actively developed,
~18 stars). Free firmware for DMR radios — DM-32UV, DM-1701, DM-1801, MD-380,
GD-77, MD-9600 — with a shared core and thin per-board layers.

Examined 2026-10-28. **Nothing here is a dependency or a fork**; these are
design ideas worth adopting where they fit.

## Two ideas that map directly onto our open problems

### 1. Settings ring + commit word (instead of erase-per-save)

`docs/spec/settings-store.md` describes a settings store built for power loss
and wear:

- Settings live in a **ring of 4 KiB sectors**; each sector has 16 slots of
  256 B.
- A record is `magic ("OSET") | sequence | layout/page | length | payload |
  CRC-32 | commit word`.
- **The commit word is written last**, then the slot is read back. A power cut
  before it leaves the previous set in force — no torn state.
- Saves go to the **next free slot**; a sector is erased only when it fills.
  With 8 sectors that is one erase every 64 saves, instead of one erase per
  save.
- `settings_t` is **paged** (236 B per page) so adding fields is append-only
  and a shorter payload fills what it can and defaults the rest.
- A **foreign-region guard** refuses to trust a region written by stock
  firmware.

Why it matters here: this is a much better answer to turnrye's "we're stingy
about nvm writes" than our current erase-rewrite-then-write approach. Our
debounced write removes write *frequency*, but each save still erases a 4 KiB
sector. A ring removes the erase per save as well, and is power-safe by
construction.

Recommendation: land the debounce now (small, reviewable), and propose the
ring + commit word as the follow-up for C62 persistence. It also removes the
"erase stalls the XIP bus" concern from the hot path.

### 2. A qdmr-compatible CPS shim

They program the radio three ways, and one of them is **qdmr through a shim
that speaks the open-firmware CPS protocol qdmr already supports**:

| Part | Where |
|---|---|
| Protocol engine, **no RTOS, no USB** | `core/cps/cps_openfw.c` |
| Hook to host link and reboot | `core/cps/cps_port.c` |
| Host tests | `tests/host/cpsTest.c` |
| Simulator on stdin/stdout | `tests/host/cpsSim` |
| Test client (info/read/write/verify) | `tools/cps_client.py` |

The protocol is small ASCII framing (`R area addr len`, `W …`, `C 5` to
close), not SLIP+FMP+XMODEM. It is testable without hardware, and
`cps_client.py` stands in for qdmr in CI.

Why it matters here: our R2 plan assumed porting **rtxlink** for live
programming. A CPS shim that qdmr already speaks is a far smaller target, and
their engine is deliberately OS-free so it can be host-tested. Worth
evaluating before committing to rtxlink.

## Smaller ideas worth stealing

| Idea | Where | Why it fits us |
|---|---|---|
| **First-byte protocol dispatch** on one host port (`0xE0` MMDVM, `A5 5A` debug, else CPS) | `core/hostlink` | We have one UART shared by console, flashing and any future CPS |
| **Housekeeping task** performs storage writes as queued jobs | `core/codeplug`, `core/storage` | Answers "heavy in the state machine": UI never blocks on flash |
| **Staging area** for a new codeplug image (write, verify, then swap) | `staging_addr` | Power-safe codeplug replacement, mirrors our reserve idea |
| **Web installer + browser codeplug editor** over WebSerial | radiofoundry.org | ptSCD is a file format; a browser editor could read/write it with no install |
| **Host tests + UI screenshot gallery + HIL CI** | `tests/host`, `.github/workflows/hil.yml` | We want exactly this for the Channel Memories UI |
| **Firmware shipped without the codec** | releases | Precedent for the AMBE legal position: feature present, codec supplied separately |
| **Board status table generated from per-board TOML** | `boards/*/board.toml` | Keeps support matrices honest |

## What does not transfer

- **Hardware layers.** Their radios are STM32/NXP + HR_C6000/AT1846S. The C62
  is a CSK6 with a BK4819 and a HiFi4 DSP. Nothing at that level is reusable.
- **Their DMR stack.** XCMP/CMS/CACH implementation is tied to those radios'
  baseband chips.
- **The open-firmware codeplug layout.** We are adopting **ptSCD**; their
  in-place open-firmware layout is a different standard.

## Licensing

Their code is MIT, which is GPL-3-compatible, so borrowing a *design* or even
code is legally fine with attribution. Prefer reimplementing the ideas for our
stack rather than importing files wholesale — the surrounding assumptions
differ too much.

## Suggested follow-ups for us

1. Keep the debounced write in PR #551 (small, addresses the review).
2. Propose a **settings ring with a commit word** as the next persistence
   step — fewer erases, torn-write safe, append-only growth.
3. Evaluate the **open-firmware CPS shim** as the live-programming path
   instead of rtxlink, and copy the `cpsSim` + `cps_client.py` test pattern.
4. Adopt the **UI gallery + HIL** testing style for Channel Memories.

---

## UI ideas worth borrowing

Their UX is unusually well specified (`docs/ux/principles.md`,
`information-architecture.md`, `task-flows.md`, `docs/spec/ui.md`), and much of
it maps onto Channel Memories. The C62 panel is 160x128, which their rules put
at **7 list rows**.

### Directly applicable to Channel Memories

| Idea | Their implementation | Why it fits us |
|---|---|---|
| **"New from current" as the first list row** | Channels screen, row 1 | Exactly "save what you're tuned to" — no separate menu path |
| **Save vs. Save as new channel** | Channel page, last rows | Distinguishes updating an existing memory from creating one |
| **Unsaved Changes prompt** | `discard` screen on BACK from the editor | Stops silent loss of an edit |
| **Long `SELECT` = save/confirm the focused change** | Global key meaning, principle 3 | One consistent way to commit a memory; no per-screen save keys |
| **Temporary vs. permanent** | Squelch is temporary, long `SELECT` saves; talkgroup "this transmission only" | Tuning stays free; saving is explicit |
| **Type-to-find (T9)** | `ui_find.c`: matches channel number, name, contact ID, recent | **Mandatory at 4,000 channels** — nobody scrolls that far |
| **Row format: name first, value shrinks** | `467.375 TG3100` → `467.375 3100` → `TG3100` | Long names must never be cut to make room for a frequency |
| **Delete with Undo, not a modal** | Messages: "Delete (with Undo)" | Fewer presses, recoverable |
| **Status strip always visible** | TX/RX pill on every screen incl. menus | PTT state must never be hidden by a list |
| **Hint row, grouped, Back right-aligned** | `"OK open  # more  Back"` | Discoverability on a 5-row screen |
| **Voice prompt on channel change** | "Channel 4, County fire" | Already have voice prompts in OpenRTX |

### Structural rules worth adopting wholesale

- **Ten principles with testable consequences.** Every principle has a
  "Test:" line the simulator enforces (press counts, layout pitch, status
  strip presence). That is how UI quality survives refactors.
- **One navigation stack, no dead ends.** BACK pops one level, long BACK is
  Home from anywhere, field tasks ≤ 3 levels deep.
- **Fixed long-press meanings across all screens.** Long press never transmits,
  deletes or resets.
- **Press budgets per task** (most tasks ≤ 4 presses, none > 6), enforced by
  scripted flows in the simulator.
- **Row density per display class** with safe-area insets, checked by a layout
  pass that fails the build if pitch drops below the class minimum.
- **Shorter text beats smaller font.** Fit problems are solved with
  abbreviations and alternates, never by shrinking type or squeezing leading.
- **Color is redundant**: every colour cue also carries a word or icon, so
  monochrome and colour-blind users see the same thing.
- **Logical actions, not physical keys.** Screens bind `SELECT`/`BACK`/`NAV`/
  `QUICK`/`CONTEXT`; each board maps them.

### What to adopt first for the C62

1. **T9/type-to-find** in the Channels list — without it, 4,000 channels is
   unusable.
2. **"New from current"** as row 1, with long `SELECT` to save.
3. **Save / Save as new / Unsaved Changes** on the channel page.
4. **Status strip** preserved inside the channel list (OpenRTX already shows
   TX/RX on the main screen; keep it in lists).
5. **Row format with a shrinking value** and a never-cut name.
6. **Simulator-driven checks** for row count, pitch and press budgets.
