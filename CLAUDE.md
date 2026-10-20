# MultibandCompressor (Daisy Field)

Stereo 3-band end-of-chain processor for an Electrosmith Daisy Field
(STM32H750, 128 KB flash, 128x64 OLED, 8 knobs, 2 switches, 16-key keybed).
It sits at the end of a live set whose tracks are mixed in the box on an
Elektron Digitakt II. The goal is to even out loudness/tone differences
between tracks and give coarse live controls, without squashing the mix.

## Start here

- [ROADMAP.md](ROADMAP.md): current status, order of work, and the
  concrete plans for the offline test harness and the display simulator.
  **Read this first and keep it updated.**
- [DSP_ARCHITECTURE.md](DSP_ARCHITECTURE.md): proposed move from downward
  multiband compression to up/down leveling toward per-band targets. Open
  decisions are listed at the end and are the user's to make.
- [PHASE2_PLAN.md](PHASE2_PLAN.md): UI paging/keybed/meter plan. On hold
  pending the DSP direction.
- [OPERATIONS.md](OPERATIONS.md): toolchain setup (Windows and Linux),
  build, flash, controls.
- [TESTING.md](TESTING.md), [ANALYSIS.md](ANALYSIS.md): how the DSP was
  verified, and the audit of the original code.

## Build

- Firmware: `make` (ARM GCC 10-2020-q4). The Makefile finds
  libDaisy/DaisySP either two levels up (Windows layout) or as siblings
  (`~/Dev` on Linux). Flash: DFU, `make program-dfu`; the "get_status"
  error after a successful download is expected (see OPERATIONS.md §3).
- Flash is tight (86.9% on libDaisy v9). Check the memory-usage table
  after changes.
- Host-side code (planned `tools/`, `sim/`) uses the host `g++`, built
  separately from the firmware Makefile.

## Conventions

- DSP code lives in `src/` and must stay hardware-independent so it
  compiles on the host too (no `daisy_*.h` includes there; the stray
  `daisy_pod.h` include in `src/butterworth.cpp` is a known leftover to
  remove).
- The GitHub repo is `mgronberg0/MultibandCompressor`. On the Windows
  machine it's a subtree of a larger `MyProjects/` repo and pushes go
  through `git subtree push` (OPERATIONS.md §0); on Linux it's a plain
  clone pushed directly.
