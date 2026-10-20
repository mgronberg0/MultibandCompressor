# Roadmap

Status and order of work. Keep this updated as things move. See
[CLAUDE.md](CLAUDE.md) for the project overview and conventions.

## Where things stand (2026-10-20)

- **DSP direction is decided.** See [DSP_ARCHITECTURE.md](DSP_ARCHITECTURE.md)
  for the full proposal; its three open decisions are now resolved:
  1. **Wideband leveling first**, per-band leveling as a second pass once
     wideband is proven.
  2. **Gentle upward range**, capped around +6dB -- leveler behavior, not
     OTT-style density.
  3. **Limiter lookahead of 1-2ms is acceptable.**
- **PHASE2_PLAN.md's UI is superseded, not current.** The firmware
  currently flashed implements Phase 2's per-band threshold/ratio/attack/
  release knob map (built and hardware-tested across several rounds of
  feedback -- see TESTING.md/OPERATIONS.md for that history). That UI
  assumed downward-only compression and gets rewritten once the new DSP
  stages land; nothing on the device right now is final.
- **Two environments are in play.** Windows (this checkout): ARM
  toolchain, DFU flashing, `git subtree push` to the scoped GitHub repo
  (OPERATIONS.md §0). Linux (`~/Dev` layout): has a native `g++`, so it's
  where the host-side DSP test harness and any display simulator actually
  get built and run, and where this repo pushes directly (no subtree
  needed -- it's a plain clone there). Keep that split in mind when
  picking up a task: don't assume host-buildable code can be verified from
  the Windows side.
- **Known gaps between CLAUDE.md's description and this checkout**,
  surfaced while reading these docs in on 2026-10-20, not yet resolved:
  - The Makefile here only has the Windows `../../libDaisy/` /
    `../../DaisySP/` layout -- no Linux sibling-path detection yet. Needs
    building (and testing) from the Linux side, not guessed at blind from
    here.
  - Flash usage measured here is **92.16%** of 128KB on libDaisy
    `v5.4.0-22-g85172e2b`, not the 86.9%/"libDaisy v9" figure in
    `CLAUDE.md`. Likely a different machine/checkout's measurement --
    worth reconciling once both environments are building the same
    revision, rather than trusting either number blindly.
  - `sim/` and most of `tools/` (beyond `dsp_verification.py`,
    `butterworth_test.cpp`, `AudioFile.h`) don't exist yet -- the host
    harness and simulator below are planned, not built.
  - Fixed while reading these docs: `src/butterworth.cpp` had a stray,
    unused `#include "daisy_pod.h"` (a hardware BSP header, which would
    have broken a host build) -- removed.

## Order of work

1. **Offline WAV test harness** (Linux session -- needs a host compiler).
   - New host-portable DSP code in `src/` (or a new file there): the
     wideband leveler (slow RMS detector, gated below a floor, holds gain
     instead of chasing silence/tails, ±range clamp) and a lookahead
     limiter/soft clip to replace the current hard `fclamp(-1,1)`.
   - While touching this code: fix the two smaller issues
     DSP_ARCHITECTURE.md flags -- stereo-link detection should use
     `max(|L|,|R|)` everywhere (currently `0.5*(|L|+|R|)` in
     `src/compressor.cpp`'s `BandCompressor::ProcessBlock`, which reads a
     hard-panned signal 6dB quiet), and reconsider `AutoMakeup(true)`'s
     fixed gain addition now that leveling is doing the heavy lifting.
   - A `tools/` harness loads WAV exports of two mismatched Digitakt
     tracks (`tools/AudioFile.h` is already here), runs them through the
     new stages, and compares per-track level before/after -- tune the
     leveler against real material without flashing anything.
   - `tools/dsp_verification.py`'s approach (numerically verify the math)
     is the fallback if a host C++ build genuinely isn't practical even on
     Linux; prefer a real host binary first since it can run actual audio
     through the code, not just check coefficients.
2. **Integrate into firmware**, minimally at first: input trim -> new
   wideband leveler -> existing LR4 crossover -> existing per-band
   compressor (unchanged for now) -> sum -> new lookahead limiter ->
   output trim. This validates the new stages on real hardware/real sets
   without also rewriting the per-band UI yet.
3. **Second pass: per-band leveling.** Add the per-band up/down-to-target
   stage, with the relative-to-full-band gating DSP_ARCHITECTURE.md
   describes (so a deliberately-empty band during a breakdown doesn't get
   pulled up). Only after wideband leveling has been used on real sets and
   proven out.
4. **Rewrite PHASE2_PLAN.md** around the final knob/page map once the
   above is real: per-band targets (KNOB_3-5), Amount/Speed macros
   (KNOB_6-7), output/ceiling (KNOB_8), Learn/Hold keys, boost-and-cut
   metering (centered on 0dB, not the current downward-only GR bars).
   Mute/bypass keys carry over from the current Phase 2 keybed layout.
5. **UI refactor + simulator.** Pull the OLED drawing into a `UiState` /
   `DrawUi` split that doesn't depend on `daisy_field.h`, so new pages get
   designed and iterated on in a host-side simulator (`sim/`) instead of
   by flash-and-look-at-hardware every time.

## Related docs
- [DSP_ARCHITECTURE.md](DSP_ARCHITECTURE.md) -- the proposal above is based on
- [PHASE2_PLAN.md](PHASE2_PLAN.md) -- the UI plan being superseded (step 4)
- [OPERATIONS.md](OPERATIONS.md) -- build/flash/push, both environments
- [TESTING.md](TESTING.md) -- how the current DSP was verified
- [ANALYSIS.md](ANALYSIS.md) -- audit of the original hand-written code
