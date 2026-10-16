# MultibandCompressor — Codebase Analysis & Path Forward

Date: 2026-10-16

**Update:** the rewrite described in section 2 has been implemented on the
`multiband-compressor-field` branch. See [TESTING.md](TESTING.md) for how
it was verified (real firmware build + numerical DSP checks) and a manual
checklist for once it's on real hardware. The analysis below is left as
originally written, for the record.

## 1. What's actually in the repo

```
MultibandCompressor.cpp      <- main/entry point
src/butterworth.h/.cpp       <- 2nd-order Butterworth filter (LP/HP/AP)
src/butterworth_test.cpp     <- standalone PC test harness (not part of firmware build)
src/compressor.h/.cpp        <- EMPTY (0 bytes)
src/waveshaper.h/.cpp        <- EMPTY (0 bytes)
src/AudioFile.h               <- third-party header-only WAV reader (used only by the PC test)
Makefile                      <- builds MultibandCompressor.cpp + butterworth.cpp + compressor.cpp
```

The roadmap comment at the top of `MultibandCompressor.cpp` is accurate — this is still step 0:

1. ~~implement Linkwitz-Riley filter~~ — not done (only a plain 2nd-order Butterworth exists)
2. parameters to control crossover points — not done
3. prove 3-band split is artifact-free — not done
4. per-band compressor parameters — not done (compressor.cpp is empty)
5. remaining 2 compressors — not done
6. mute/solo/bypass per band — not done
7. input/output gain — not done

### Hardware mismatch (important)
`MultibandCompressor.cpp` includes `daisy_pod.h` and instantiates `DaisyPod hw`. **The Pod only has 2 knobs, 2 buttons, and 2 LEDs — no display.** Since the target is a **Daisy Field** (8 knobs, 2 switches, 4 CV ins, SSD1306 OLED, RGB LED ring via PCA9685), the entry point needs to be rewritten against `daisy_field.h` / `DaisyField hw` regardless of anything else. None of the current control-handling code (`Controls()`, `UpdateKnobs()`, `UpdateLeds()`) will carry over as-is.

### Concrete bugs found

- **`butterworth.cpp:13` — `K2 = exp2(K)` should be `K2 = K * K`.** `exp2()` computes 2^K, not K². This silently breaks every coefficient derived from it (`b0`, `a1`, `a2` for all three filter types). The filter currently does not implement the transfer function derived in the comments at the top of `MultibandCompressor.cpp` — this is almost certainly why "most of it doesn't work the way you expect."
- **`butterworth_test.cpp:27`** — `std::string filePath = std::string "C:\...";` is invalid C++ (missing parens/assignment), won't compile. This file is a desktop-only sanity-check tool (per its own g++ command in the comments) and isn't part of the firmware `Makefile`'s `CPP_SOURCES`, so it doesn't block firmware builds, but it's currently dead/broken code.
- **Makefile** references `$(SRC_DIR)/compressor.cpp` which is a 0-byte file — compiles only because an empty .cpp is still valid, but it contributes nothing.
- Only a single Butterworth lowpass is ever instantiated and applied identically to both channels (`lpf.Process(in[0][i])` / `lpf.Process(in[1][i])` using the **same filter instance** for L and R) — that's a mono filter state being reused for two independent channels, which is also wrong for stereo (channel crosstalk in the filter's internal state... actually here it's worse: L and R literally share one state, so they contaminate each other's `xnm1/xnm2/ynm1/ynm2` history).
- No multiband split exists at all yet — the signal path today is just: mono-shared 2nd-order LPF → out. No compression, no waveshaping, nothing band-related.

### What's good / reusable
- `butterworth.h/.cpp`'s structure (Init/Process/SetCutoff/SetFilterType) is a reasonable shape and, once the `K2` bug is fixed, the LP/HP math is correct RBJ-style bilinear-transform Butterworth. It's a fine building block for a Linkwitz-Riley crossover (LR4 = two cascaded, matched Butterworth-2 sections per band, with the classic phase-inversion-and-sum trick to keep bands phase-coherent).
- **DaisySP already ships a full-featured `daisysp::Compressor`** (`DaisySP/DaisySP-LGPL/Source/Dynamics/compressor.h`) with attack/release/ratio/threshold/auto-makeup and even sidechain input support. There is no need to hand-roll a compressor in `src/compressor.cpp` — wrapping this per-band, per-channel (or linked stereo) is much less risky than writing one from scratch, and matches "mixed success" noted in your other project's commit history for a hand-written compressor.
- `daisy_field.h` confirms the hardware: `KNOB_1..KNOB_8`, `SW_1/SW_2`, `CV_1..CV_4`, an `OledDisplay<SSD130x4WireSpi128x64Driver> display`, and an 8-RGB-LED `LedDriverPca9685<2,true>`. Sibling example `field/chorus/chorus.cpp` is a good minimal template for the `hw.ProcessAllControls()` / `hw.knob[n].Process()` / `hw.display.*` pattern to copy.

## 2. Suggested path forward

Given a Daisy Field target and a mastering-chain use case, I'd suggest rebuilding in this order rather than patching the current file in place:

1. **Rewrite the entry point on `daisy_field.h`.** Drop `daisy_pod.h` entirely. Get `hw.Init()`, `ProcessAllControls()`, and the OLED showing *something* (even just "Hello") before touching DSP — this de-risks the hardware/display/knob plumbing early.
2. **Fix and promote the Butterworth filter, build an LR4 crossover.** Fix the `K2` bug, give each channel/band its own filter *instance* (no shared state), and implement a proper 4th-order Linkwitz-Riley split (cascade two matched 2nd-order Butterworths per cutoff; sum low+high branches with correct polarity to confirm a flat, artifact-free recombination — this satisfies roadmap step 3 as an explicit bypass-test mode).
3. **3-band split → `daisysp::Compressor` per band, per stereo pair, with stereo-linked gain detection** (compute one gain-reduction value from the max/sum of L+R per band so the stereo image doesn't shift under compression — important for a mastering tool). Expose threshold/ratio/attack/release per band.
4. **Control mapping across the 8 knobs + 2 switches**, e.g.:
   - 2 knobs: crossover frequencies (low/mid and mid/high split points)
   - knob per band (or paged) for threshold + ratio, with switches to page between bands / toggle mute-solo-bypass
   - reserve a knob for input or output trim (roadmap step 7)
5. **Display**: live view of band gain-reduction (bar meters), current crossover points, and which band is selected/muted/soloed — this is where the "Weird Peak Follower" style metering from your other project could be reused conceptually.
6. **Output/input trim + limiter-ish ceiling** as a final stage, since this sits at the end of a live chain — a stray hot signal shouldn't clip the Field's DAC.
7. Leave `waveshaper.h/.cpp` out of the critical path for now — nothing in the roadmap calls for it yet; revisit only if you want saturation/character per band later.
8. Delete or relocate `butterworth_test.cpp` + `AudioFile.h` out of `src/` (or into a `tools/` dir excluded from the Makefile's `CPP_SOURCES`) since they're a PC-side test utility, not firmware, and currently don't even compile.

I'll hold off on writing any of this until you give the go-ahead, per your note that implementation is a separate task.

## 3. Git status

- This folder **is** inside a git repo — but the repo root is one level up, at `MyProjects/` (shared across sibling projects like `DubDelay`, `mbtest`, etc.), not scoped to `MultibandCompressor/` alone.
- Current branch: `master`.
- **`MultibandCompressor/` itself has a clean working tree** — everything currently in this folder (including the empty stub files and the `K2` bug) is already committed, as of:
  - `6a8e25a togglable bands`
  - `9a71d65 initial commit`
- The dirty/untracked state visible in `git status` (`DubDelay`, `mbtest.cpp`, `DubDelayTEMP/`, a stray oddly-named file) belongs to **sibling projects, not this one** — I left those untouched.
- Since there's nothing uncommitted here to lose, you can safely branch now:
  ```
  git checkout -b multiband-compressor-field master
  ```
  and do the Daisy Field rewrite there, keeping `master` as the known-building (if non-functional) fallback. No commit of "current work" is needed first since there's nothing pending in this directory — just cut the branch and start.
