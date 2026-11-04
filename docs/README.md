# C62 / P25 development notes

Working notes for OpenRTX on the Retevis C62 (ListenAI CSK6011B), maintained on
the `N4IRS/OpenRTX` fork. These are fork-local documents, not upstream OpenRTX
documentation.

| Document | Contents |
|---|---|
| [`status.md`](status.md) | Current branch/build state, what works, known limitations |
| [`c62-hardware.md`](c62-hardware.md) | Board hardware reference: SoC, memory map, RF, audio, display, keys, power |
| [`p25.md`](p25.md) | P25 Phase 1 operation, settings, verification procedure, TX bug history |
| [`build-setup.md`](build-setup.md) | Toolchain and west workspace setup |
| [`build-environment.md`](build-environment.md) | Rebuilding the build environment from scratch |
| [`flashing.md`](flashing.md) | Bootloader, flashing and partition map |
| [`nvram-backup.md`](nvram-backup.md) | Settings/NVRAM backup and restore |
| [`pr-checklist.md`](pr-checklist.md) | Checklist for upstream OpenRTX PRs |
| [`cps-architecture.md`](cps-architecture.md) | Codeplug/CPS architecture (proposed `.rtxc` format) |
| [`cps-release-plan.md`](cps-release-plan.md) | CPS release sequencing |
| [`cps-storage-budget.md`](cps-storage-budget.md) | Codeplug storage budget |
| [`chirp-cps-feasibility.md`](chirp-cps-feasibility.md) | CHIRP/qdmr codeplug feasibility, P25 channel data |
| [`ptscd-notes.md`](ptscd-notes.md) | PTS CD / provider firmware notes |
| [`radiofoundry-ideas.md`](radiofoundry-ideas.md) | Codeplug tooling ideas |

`Using the CS7000 M17 Radio.pdf` is reference material and is not tracked.
