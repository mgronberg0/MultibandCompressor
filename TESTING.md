# Testing notes — 3-band mastering compressor rewrite

This machine has the `arm-none-eabi` cross toolchain (used to build real
firmware) but **no native host C/C++ compiler**, so true C++ unit tests
can't be built/run here. Two tracks of verification were used instead:

## 1. Real firmware build (the authoritative test)

```
make clean && make
```

Builds clean: **0 errors, 0 warnings**, producing `build/MultibandCompressor.elf/.hex/.bin`.
Flash usage: 105852 / 131072 B (80.76%). That's higher than you might expect
for the amount of DSP code here — most of it is almost certainly pulled in
by `snprintf`'s float formatting (`%.0f` etc. in `UpdateDisplay()`) dragging
in newlib's full floating-point printf machinery under `--specs=nano.specs`.
It still fits comfortably under the 128KB budget, so this wasn't chased
further, but if flash ever gets tight, replacing the `snprintf` float
formatting in `UpdateDisplay()` with manual int/fixed-point formatting would
likely recover a meaningful chunk of it.

This validates: API usage against libDaisy/DaisySP, correct types, correct
linking against the real `daisy_field.h` hardware target, and that nothing
in the control/DSP code trips the compiler's `-Wall`.

**What it can't validate** (no hardware on hand to flash/run): whether the
knob ranges feel musically right by ear, whether the OLED layout is legible/
fits (text/meter positions were sized by hand against the 128x64 SSD1306
geometry and font metrics, not rendered and visually checked), whether the
mute/bypass long-press timing feels right, or basic "does audio come out
the jacks" sanity. See the manual checklist at the bottom of this file.

## 2. DSP math verification (`tools/dsp_verification.py`)

Since the formulas can't be unit-tested in C++ here, the exact same math
was ported to Python and checked against known-correct, analytically
derived behavior:

```
python tools/dsp_verification.py
```

All checks pass. Specifically it verifies:

1. **The Butterworth fix is real and correct.** For both LOWPASS and
   HIGHPASS, at several cutoff frequencies, the analytic magnitude response
   lands at exactly -3.01dB at the configured cutoff (the defining property
   of a 2nd-order Butterworth filter). It also reconstructs the *old*
   `exp2(K)` formula and shows it does **not** hit -3dB (comes out ~0dB,
   i.e. barely filtering at all) — concrete confirmation the original bug
   was real and that the fix (`K2 = K * K`) resolves it.

2. **LR4Crossover sums flat with no cancellation notch.** Swept two
   crossover frequencies (180Hz, 2500Hz) across 13 points each from deep in
   the low band to deep in the high band. `|low + high|` deviates from
   unity by `0.00000dB` everywhere tested, and the low/high branches are
   phase-matched to each other to `0.00000deg` at every point. This is the
   property that actually matters for "no artifacts when the bands are
   recombined."

   Worth noting since it wasn't obvious going in: the first draft of this
   check asserted the *recombined signal* should stay near 0deg phase
   relative to the input, and that failed loudly (180deg everywhere). That
   led to tracing per-branch phase directly (see the trace output) and
   confirming the real, textbook LR4 property is a *flat magnitude* sum via
   matched branch phases — the recombined signal's phase relative to the
   input legitimately sweeps from 0deg (far from the crossover) through
   exactly -180deg (right at the crossover frequency) and back. That's
   normal/expected for any Linkwitz-Riley crossover and isn't a defect;
   `src/crossover.h`'s doc comment was corrected to describe this precisely
   instead of overclaiming "phase-matched to the input."

3. **BandCompressor's gain-reduction behavior is sane and stereo-safe.** A
   -30dBFS tone against a -18dB threshold settles at a small *positive*
   gain (just the auto-makeup, no reduction). A -6dBFS tone against the
   same threshold settles at a clearly negative gain (compression is
   happening). And feeding one compressor a stereo-linked key signal
   derived from two channels 6dB apart in level preserves that 6dB balance
   exactly after compression — confirming L/R image stability, the whole
   point of driving `daisysp::Compressor::ProcessBlock`'s sidechain/key
   input with a link signal instead of running two independent compressors.

## Manual checklist for once this is flashed to real hardware

- [ ] Audio passes through cleanly with all bands set to ratio 1:1 (no
      audible crossover artifacts — the main thing this rewrite set out to
      fix vs. the original hand-written filter).
- [ ] Sweeping KNOB_1/KNOB_2 moves the two crossover points without clicks/
      pops, and KNOB_2 can't be dragged below KNOB_1 (clamped in `Controls()`).
- [ ] SW_1 cycles LOW -> MID -> HIGH and the OLED band row / knob LEDs
      reflect the change.
- [ ] Turning KNOB_3..6 right after a band switch does nothing until it's
      moved back near where it was left (pickup/catch) — confirm this isn't
      annoying in practice; it's the first thing to simplify away if so.
- [ ] SW_2 short-press mutes/unmutes the selected band; long-press (~500ms)
      bypasses it instead (listen for the compression dropping out while
      the band stays audible).
- [ ] Gain-reduction meter bars on the OLED move with program material and
      roughly agree with what you're hearing.
- [ ] KNOB_7/8 (input/output trim) behave sensibly across the full +/-24dB
      range without the safety clamp in `AudioCallback` audibly kicking in
      under normal gain-staging.
