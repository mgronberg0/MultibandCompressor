# Operations — build, flash, and run the Daisy Field compressor

Quick-reference runbook for building firmware, getting it onto the Field,
and operating it. See [ANALYSIS.md](ANALYSIS.md) for the original codebase
audit and [TESTING.md](TESTING.md) for how the DSP/firmware was verified.

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
is currently ~81% of 128KB (see TESTING.md for why, and a lead on recovering
some of it if it ever gets tight).

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

### Signal path
```
in -> input trim -> LR4 crossover @ freq1 -> LOW
                          \-> LR4 crossover @ freq2 -> MID, HIGH
LOW/MID/HIGH -> per-band stereo-linked compressor (mute/bypass aware)
-> sum -> output trim -> safety clamp -> out
```

### Controls

| Control | Function | Range |
|---|---|---|
| KNOB_1 | Crossover 1 (Low/Mid split) | ~60Hz - 1.5kHz, log |
| KNOB_2 | Crossover 2 (Mid/High split) | ~300Hz - 12kHz, log (auto-clamped >= 1.25x KNOB_1) |
| KNOB_3 | Threshold of the **selected** band | 0dB - -48dB |
| KNOB_4 | Ratio of the **selected** band | 1:1 - 20:1 |
| KNOB_5 | Attack of the **selected** band | 1ms - 250ms, log |
| KNOB_6 | Release of the **selected** band | 20ms - 1s, log |
| KNOB_7 | Input trim | -24dB - +24dB |
| KNOB_8 | Output trim | -24dB - +24dB |
| SW_1 short press | Cycle selected band: Low -> Mid -> High | -- |
| SW_2 short press | Toggle Mute on the selected band | -- |
| SW_2 long press (~500ms+) | Toggle Bypass on the selected band | -- |

**Soloing a band:** there's no dedicated solo -- mute the other two bands
instead. Bypass plays a band unprocessed but still audible/meterable, which
is handy for A/B-ing "with vs. without compression" on one band without
losing the others.

**Knob pickup/catch on KNOB_3-6:** these four are shared across all three
bands. Switching the selected band (SW_1) does **not** jump that band's
threshold/ratio/attack/release to wherever the knob physically sits --
each knob stays inert until you move it back near the value it was at when
you last left that band, then it "catches" and takes live control again.
The per-band LEDs above KNOB_3-6 are dim while a knob is waiting to catch
and full brightness once it's live -- watch those if a knob seems
unresponsive right after switching bands, that's what's happening.

### OLED layout
- Row 1: both crossover frequencies (`X1:.. X2:..Hz`)
- Row 2: input/output trim (`IN:..dB OUT:..dB`)
- Row 3: three band labels (LOW/MID/HGH), `>` marks the selected one, a
  trailing `M` or `B` marks muted/bypassed
- Row 4: the selected band's threshold/ratio/attack/release
- Bottom: one vertical gain-reduction meter per band (fills downward as
  that band compresses harder, 0 to -24dB range)

### LEDs
- LED_KNOB_3-8 (above KNOB_3-6): pickup/catch state per knob (see above)
- LED_SW_1: brightness steps with which band is selected
- LED_SW_2: bright if the selected band is muted, half if bypassed, dim otherwise

## 5. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `dfu-util -l` finds nothing | Board not in DFU mode, or not plugged in. Windows can show a cached/ghost DFU device entry for a board that's since been unplugged -- check `Present` via `Get-PnpDevice`, don't trust the entry alone. |
| `make program-dfu` errors with "get_status" after a successful download | Benign, see section 3. |
| Build fails referencing `libdaisy.a`/`libdaisysp.a` | Those libraries aren't built yet; build them in their own directories first. |
| A knob does nothing right after switching bands | Pickup/catch -- move the knob back toward where it was for that band (watch the LED). |
| No sound at all | Check KNOB_7/8 (input/output trim) aren't buried at -24dB, and that the selected/other bands aren't all muted. |
