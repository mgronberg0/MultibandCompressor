# DSP architecture proposal — leveling, not just compression

Status: **proposal, not implemented.** Supersedes the DSP assumptions behind
[PHASE2_PLAN.md](PHASE2_PLAN.md) (per-band threshold/ratio/attack/release on
KNOB_3-6). If this direction is accepted, PHASE2_PLAN.md's pages and knob
map get rewritten around it before any UI/simulator work.

## The goal

This is the last thing in a live chain. Each track in the set is mixed in
the box on the Digitakt II, so kicks, snares and samples only roughly match
from one track to the next in level, intensity and tone. The processor
should:

1. **Even out the differences between tracks**, so the set holds together
   without every track having to be matched carefully ahead of time.
2. **Give immediate, coarse controls** for adjusting on the spot.
3. **Not squash the mix.** Breakdowns, drops and transients should keep
   their impact.

## Why the current design falls short

The current firmware is a 3-band **downward-only** compressor
(`daisysp::Compressor` per band, peak-style detector, short times,
auto-makeup). There are two problems with that:

- **It only works at one speed.** Track-to-track mismatch happens over
  seconds to minutes; punch and glue happen over milliseconds. A fast
  compressor asked to fix both has to work hard all the time, and that is
  what flattens the mix.
- **It only turns things down.** A track that arrives quiet, or thin in
  one band, can't be helped.

Smaller issues in the current code:

- `AutoMakeup(true)` adds a fixed gain derived from threshold and ratio.
  With a low threshold and a high ratio, that's a large static boost on
  top of the compression.
- Stereo-link detection uses `0.5 * (|L| + |R|)`, so a hard-panned sound
  reads 6 dB quieter than it is. Taking `max(|L|, |R|)` is the standard
  approach.
- The output stage is a hard clamp to ±1, not a limiter.

## Separate the problem by time scale

| Problem | Time scale | Tool |
|---|---|---|
| Track-to-track loudness/tone mismatch | seconds to minutes | slow **leveling** (automatic gain riding), wideband and per band |
| Within-track dynamics: transients, punch, glue | milliseconds | gentle fast compression, then a limiter |

Broadcast processors (Orban Optimod, Omnia) solve the same problem, with
songs mastered by different people played back to back, using this chain:

```
slow wideband AGC -> slow multiband leveler -> light fast multiband compression -> limiter
```

Most of the work happens in the slow stages, which ride gain gradually the
way an engineer rides faders. The fast stages only shape the sound.

## Upward + downward toward a target

Each band gets a **target level**, and its curve works in both directions
around it:

```
 out dB
   │                         ╱ above ceiling: limiter catches it
   │                   ____╱
   │             ____╱        <- downward: louder than target -> pull down
   │        ═══════           <- deadband: near target -> leave alone
   │      ╱                   <- upward: quieter than target -> lift (max +N dB)
   │    ╱
   │  ╱  <- below gate: no boost (silence, tails, breakdowns, noise)
   └──────────────────────── in dB
     gate      target
```

Three details decide whether this sounds musical or like OTT:

1. **Range limits.** Cap the boost and cut, e.g. ±6 dB per band. This is
   what nudges mismatched tracks together without making everything the
   same level.
2. **A gate under the upward part.** Below the gate, the stage **holds**
   its current gain instead of chasing the signal. Otherwise breakdowns,
   reverb tails, the silence between tracks and the noise floor all get
   pulled up.
3. **Slow, RMS-based detection for leveling.** Use several hundred ms to a
   few seconds, and measure the average level rather than peaks, so it
   responds to how loud a track sounds, not to individual kick hits. Peak
   detection with short times stays in the fast compression and limiter
   stages.

### Automatic tonal matching

Per-band leveling also matches tone between tracks: if track B has a weaker
low end than track A, the low band's leveler lifts it toward the same
target.

**The risk** is an arrangement where a band is meant to be empty, such as a
breakdown with the kick muted. The low band would try to fill the gap.
**The fix** is the range cap plus gating each band **relative to the
full-band level**: only adjust a band's balance while the whole mix is
actually playing.

## Proposed signal path

```
in -> input trim
   -> [1] wideband leveler     slow (1-5 s), gated, ±range        fixes overall track loudness
   -> LR4 crossover (3 bands)                                     (unchanged)
   -> [2] per-band leveler     up+down to target, 0.3-2 s, ±6 dB  fixes tonal balance between tracks
   -> [3] per-band compressor  gentle downward, fast, light ratio punch/glue (optional, subtle)
   -> sum
   -> [4] limiter / soft clip  ceiling, ~1-2 ms lookahead         safety; replaces the hard clamp
   -> output trim
```

Implementation notes:

- Stages 2 and 3 can be **one gain computer per band with two envelopes**:
  a slow RMS envelope drives the leveling curve and a fast peak envelope
  drives the compression curve. Their gains (in dB) add.
- Writing the gain computer ourselves, instead of using
  `daisysp::Compressor`, gives full control over the curve and may use
  less flash. Flash use is 86.9% of 128 KB on libDaisy v9, so that
  matters.
- Stereo-linked detection uses `max(|L|, |R|)` everywhere.
- Leveler gain can be computed at block rate (1 kHz), interpolating
  per-sample to avoid zipper noise. That makes it cheap.
- The H750 has plenty of CPU for all of this.

## Coarse live controls

Thinking in terms of targets instead of per-band threshold, ratio, attack
and release gives controls you can use mid-set.

| Control | Function |
|---|---|
| KNOB_1-2 | Crossover frequencies (as today) |
| KNOB_3-5 | **Low / Mid / High target**: the tonal balance everything is pulled toward. Works like a three-band tilt EQ that actively holds the balance. |
| KNOB_6 | **Amount**: one macro for range and ratios, from "barely touching" to "glued" |
| KNOB_7 | **Speed**: a macro for leveler and compressor times |
| KNOB_8 | **Output / ceiling** |
| Key: **Learn** | Capture the current band levels as the new targets when a track sounds right |
| Key: **Hold** | Freeze all leveler gains (e.g. during a transition or breakdown) so nothing drifts |
| Keys: per-band bypass/mute | Kept from PHASE2_PLAN.md |

Side effects:

- The macros mostly remove the per-band knob pickup/catch problem, since
  you'd rarely edit one band's internals live. Detailed per-band settings
  can move to a secondary page.
- Metering has to show **boost and cut**: a per-band bar centered on 0 dB,
  growing either way, replacing the downward-only gain-reduction bars.

## Open decisions

1. **Wideband vs. per-band leveling first.** Wideband only is simplest and
   fixes overall loudness. Per-band from the start also fixes tonal
   mismatch, but needs the relative gating described above.
2. **How much upward compression.** Gentle (around +6 dB maximum boost,
   leveler behavior), or should more aggressive OTT-style density be
   available at the top of the Amount knob?
3. **Limiter lookahead.** Is 1-2 ms acceptable in the live setup? It's
   inaudible as delay but matters if anything is routed in parallel.

## Suggested next steps

1. **Offline WAV test harness on the host** (Linux has `g++`; see
   OPERATIONS.md). Run the DSP code over exports of two mismatched
   Digitakt tracks and compare levels per band before and after, so the
   leveler can be tuned without flashing. `tools/AudioFile.h` is already
   in the repo.
2. Resolve the open decisions above, then rewrite PHASE2_PLAN.md around
   this architecture: knob map, pages, keybed and metering.
3. Then the UI refactor (`UiState` / `DrawUi`) and the simulator, so the
   new pages get designed in the simulator rather than on hardware.
