# Operations — build, flash, and run the Daisy Field compressor

Quick-reference runbook for building firmware, getting it onto the Field,
and operating it. See [ANALYSIS.md](ANALYSIS.md) for the original codebase
audit and [TESTING.md](TESTING.md) for how the DSP/firmware was verified.

## 0. Pushing to GitHub

The local git repo root is actually `MyProjects/` (one level up), shared
with unrelated sibling projects (DubDelay, mbtest, etc.). The GitHub repo
at `github.com/mgronberg0/MultibandCompressor` is scoped to just this
project, so pushes go through `git subtree`, not a plain `git push`, to
avoid leaking sibling-project history/files into it.

From `MyProjects/` (not from inside `MultibandCompressor/`):
```
git subtree push --prefix=MultibandCompressor multibandcompressor <local-branch>:main
```
`multibandcompressor` is the remote name already configured locally for
that URL. This computes a synthetic history containing only commits that
touched `MultibandCompressor/` and pushes it as `main` on the remote --
it does not rewrite anything in the shared `MyProjects` repo, so it's safe
to run repeatedly as work continues.

## 1. Toolchain

Everything lives under the Daisy toolchain install:

```
C:\Program Files\DaisyToolchain\bin
```

It has `arm-none-eabi-gcc/g++` (firmware compiler), `make`, `dfu-util`, and
`openocd`. Put it on `PATH` for the session before building/flashing:

```
export PATH="/c/Program Files/DaisyToolchain/bin:$PATH"   # git-bash
$env:PATH = "C:\Program Files\DaisyToolchain\bin;$env:PATH" # PowerShell
```

There is **no native host C/C++ compiler** on this machine -- only the ARM
cross toolchain. That's why `tools/dsp_verification.py` exists as a Python
stand-in for C++ unit tests (see TESTING.md); it can't build/run ordinary
host test binaries here.

## 2. Build

From `MultibandCompressor/`:

```
make clean   # optional, but recommended after pulling changes
make -j4
```

Expected output: a clean build (0 errors/warnings) ending in a memory-usage
table and producing `build/MultibandCompressor.{elf,hex,bin}`. Flash usage
is currently ~92% of 128KB -- it jumped from ~81% once `-Wl,-u,_printf_float`
was added (see TESTING.md for why that flag is there) to make the OLED's
numbers actually render. That's genuinely tight now (~9.6KB free): the next
feature that needs meaningful flash should come with a look at replacing
the float `snprintf` calls in `UpdateDisplay()` with manual int/fixed-point
formatting to recover some of it, not just be added on top.

If `make` fails immediately with "No such file or directory" on
`libdaisy.a` or `libdaisysp.a`, libDaisy/DaisySP themselves haven't been
built -- build those first from `../../libDaisy` and `../../DaisySP`.

## 3. Flash (DFU)

1. Connect the Field to this machine over USB.
2. Put it in DFU mode: hold **BOOT**, tap **RESET**, release **BOOT** (or
   hold BOOT while plugging in USB).
3. Confirm it's visible:
   ```
   dfu-util -l
   ```
   You should see two `Found DFU: [0483:df11] ...` lines (Option Bytes +
   Internal Flash). If you see nothing, the board isn't in DFU mode (or
   isn't connected) -- redo step 2. A device that shows up in Windows'
   device list but not here usually just means it unplugged/rebooted since
   the last DFU session; physically re-enter DFU mode.
4. Flash it:
   ```
   make program-dfu
   ```

**Known false error:** after a fully successful erase+download (you'll see
`File downloaded successfully`), `dfu-util` often prints
`Error during download get_status` and `make` reports exit code 74. This is
expected and harmless -- the `:leave` flag tells the chip to reset straight
into the new firmware, and it reboots before answering dfu-util's final
status poll. The flash already succeeded by that point. Trust the
`File downloaded successfully` line, not the exit code. The only way to
confirm the new firmware is actually running is to look at the hardware
(OLED should show the compressor UI described below).

If you instead get a real failure (erase or download doesn't complete, or
no `File downloaded successfully` line at all), re-check DFU mode and retry
-- that's a real problem, not the benign one above.

## 4. Operating it

This is the Phase 2 control scheme (see PHASE2_PLAN.md for how it got here
from the original single-page version). The OLED now has three pages,
cycled by one keybed key, with a meter strip that's always visible
regardless of page.

### Signal path
```
in -> per-channel input trim (global +/- balance) ->
LR4 crossover @ freq1 -> LOW
     \-> LR4 crossover @ freq2 -> MID, HIGH
LOW/MID/HIGH -> per-band stereo-linked compressor (mute/bypass aware)
-> sum -> per-channel output trim (global +/- balance) ->
safety clamp -> out
```

### Controls

| Control | Function | Range |
|---|---|---|
| KNOB_1 | Crossover 1 (Low/Mid split) | ~60Hz - 1.5kHz, log |
| KNOB_2 | Crossover 2 (Mid/High split) | ~300Hz - 12kHz, log (auto-clamped >= 1.25x KNOB_1) |
| KNOB_3 | Threshold of the **selected** band (Band page) | 0dB - -48dB |
| KNOB_4 | Ratio of the **selected** band (Band page) | 1:1 - 20:1 |
| KNOB_5 | Attack of the **selected** band (Band page) | 1ms - 250ms, log |
| KNOB_6 | Release of the **selected** band (Band page) | 20ms - 1s, log |
| KNOB_7 | Home/Band page: global input trim. Trim page: input L/R balance | -24dB..+24dB (global) / -12dB..+12dB (balance) |
| KNOB_8 | Home/Band page: global output trim. Trim page: output L/R balance | same ranges as KNOB_7 |
| Keybed key 0/1/2 | Select band: Low / Mid / High | -- |
| Keybed key 3/4/5 | Toggle mute on Low / Mid / High directly | -- |
| Keybed key 6 | Cycle OLED page: Home -> Band -> Trim -> Home | -- |
| Keybed keys 7-15 | Unused, reserved for future use | -- |
| SW_1 short press | Cycle meter mode: Input -> Output -> Gain Reduction -> back | -- |
| SW_2 short press | Toggle Bypass on the selected band | -- |

**Soloing a band:** there's no dedicated solo -- mute the other two bands
directly with their keybed keys instead. Bypass plays a band unprocessed
but still audible/meterable, handy for A/B-ing "with vs. without
compression" on one band without losing the others.

**Knob pickup/catch on KNOB_3-6, and on KNOB_7/8 when crossing into/out of
the Trim page:** switching what a knob controls (which band, or
Home/Band-vs-Trim) never jumps that parameter to wherever the knob
physically sits -- it only takes live control again once moved back near
where it was left for that context. The first time ANY context is ever
touched (a band you've never selected before, right after boot, etc.) it's
live immediately -- there's nothing to protect yet. Two places show this
state:
- The per-band LEDs above KNOB_3-6 are dim while waiting to catch, full
  brightness once live.
- On the **Band** page, a parameter waiting to catch gets a trailing `~`
  right after its number (e.g. `T-12~`) -- this is the one to actually
  watch; it's easy to miss a dim knob LED while looking at the screen, and
  without this marker a knob that's correctly waiting for pickup looks
  indistinguishable from broken.

### OLED pages
Every page shares the same bottom meter strip (see below); only the top
~27px of content differs:

- **Home**: crossover frequencies (`X1:.. X2:..Hz`), global input/output
  trim (`IN:..dB OUT:..dB`). Deliberately no per-band numeric readout here
  -- the meters already cover it, and text at that refresh rate just
  flickers illegibly (see "Meter strip" below for why the bars themselves
  read fine at the same rate).
- **Band**: `Band: LOW/MID/HGH` + mute/bypass flag, then threshold/ratio
  and attack/release for the selected band, each with the `~` pickup
  marker described above.
- **Trim**: `IN BAL:` / `OUT BAL:` in dB -- positive means Left is louder,
  relative to the Home page's global trim.

### Meter strip (persistent, every page)
Three horizontal bars, one per band (Low/Mid/High, top to bottom), each
spanning most of the display's width for resolution. All three always show
the *same* mode at once -- Input level, Output level, or Gain Reduction --
labeled at the top of the strip and cycled by SW_1. A thin vertical tick on
each bar is the peak-hold marker: it snaps instantly to a new peak and
decays at a fixed rate, independent of the live bar fill.

The bars refresh every pass through the main loop (no artificial frame
limiter), driven by a fresh block-peak/gain-reduction value computed every
1ms audio block -- that's what makes them track live audio essentially in
real time rather than looking like a softened/averaged meter.

### LEDs
- LED_KNOB_3-6: pickup/catch state per knob (dim = waiting, bright = live)
- Keybed keys 0-2 (select): bright = that band is selected
- Keybed keys 3-5 (mute): bright = that band is muted
- Keybed key 6 (page): brightness steps with which page is showing
- LED_SW_1: brightness steps with the current meter mode
- LED_SW_2: bright if the selected band is bypassed, dim otherwise

Keybed key-to-LED mapping assumes keyboard-scan index N lines up with
`LED_KEY_B(N+1)` for N in 0-7 (confirmed against `daisy_field.cpp`'s scan
order, which is why only that half of the 16-key grid is used so far --
the other half's LED enum ordering is reversed and wasn't worth untangling
for keys nothing currently uses).

## 5. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `dfu-util -l` finds nothing | Board not in DFU mode, or not plugged in. Windows can show a cached/ghost DFU device entry for a board that's since been unplugged -- check `Present` via `Get-PnpDevice`, don't trust the entry alone. |
| `make program-dfu` errors with "get_status" after a successful download | Benign, see section 3. |
| Build fails referencing `libdaisy.a`/`libdaisysp.a` | Those libraries aren't built yet; build them in their own directories first. |
| A Band-page value shows a trailing `~` and its knob does nothing | Pickup/catch waiting -- move the knob back toward the value shown (watch LED_KNOB_3-6 too). |
| Turning KNOB_1/2/7/8 shows no change on screen | Check which page is showing -- those are only displayed on Home or Trim, not Band. The values are still updating even if the current page doesn't show them. |
| No sound at all | Check KNOB_7/8 (global trim) aren't buried at -24dB, and that bands aren't all muted via keybed keys 3-5. |
| `make` build is uncomfortably close to the 128KB flash limit | Expected right now (~92%, see section 2) -- recover headroom by replacing `UpdateDisplay()`'s float `snprintf` calls with manual formatting before adding more display content. |
