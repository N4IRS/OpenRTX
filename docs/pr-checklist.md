# Upstream PR checklist — OpenRTX

Standing rules for every contribution we send to `OpenRTX/OpenRTX`
(branch `Retevis-c62`). Derived from `CONTRIBUTING.md` and reviewer feedback.

## Before opening a PR

- [ ] This PR is **one logical change**; unrelated fixes are in separate PRs
- [ ] Commits are **tidy**; each one builds, no "fix typo" commits left in history
- [ ] `CONTRIBUTING.md` has been read

## Branch workflow

- Topic branches are cut from `origin/Retevis-c62`, never from `p25-c62`.
- `p25-c62` is the local integration branch (P25 stack + local experiments);
  it is rebased on top of merged topic branches, not submitted directly.
- Keep a backup branch before rebasing (`git branch <name>-backup`).

## Commit rules

- Prefix the summary with the module touched: `ui:`, `drivers:`, `core:`,
  `rtx:`, `m17:`, `platform:`. Multiple modules in one commit = split it.
- Every commit must build and pass tests on its own (branch must bisect).
- No debug instrumentation (`printk` traces, `BAT_DEBUG`, etc.) in submitted
  commits.
- Sign off every commit: `git commit -s` (DCO).
- During review use `fixup!` commits, then `git rebase -i --autosquash`
  before pushing so the merged history has no rework trail.
- Rebase rather than introduce merge commits.
- Formatting-only changes go in their own commit; never mix with logic.
- New files must pass `scripts/clang_format.sh --check` (linux kernel style).

## PR description

- Complete the PR template, including **Testing performed** (what hardware,
  which steps, what was observed).
- State how the change was verified on hardware when applicable.
- Mention the baseline build used to reproduce any bug being fixed.

## Design expectations reviewers look for

- Reuse existing helpers/drivers before adding new ones; generalise the
  primitive, not the feature.
- Hardware facts belong in `hwconfig`/`pinmap`, not drivers.
- Gate optional features behind `CONFIG_*`, with sensible defaults.
- Initialise peripherals explicitly — never lazily on first use.
- Respect the threading model: the rtx thread owns the radio.

## Our published PRs

| PR | Title | Branch |
|---|---|---|
| #548 | c62: turn off keyboard backlight when display backlight is off | `c62-kbd-backlight` |
| #551 | c62: add flash-backed nvmem storage | `c62-nvmem-persistence` |
