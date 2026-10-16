# Phase 2 plan — hardware-test feedback

Scope agreed after the first hardware test. Not yet implemented -- this is
the plan to review before I start building. Decisions already locked in
from your answers are marked (confirmed); a couple of spots where I had to
make a judgment call to produce a concrete plan are marked (proposed) --
flag those if you want something different before I start.

## 0. Bug fix: blank numbers on the OLED

**Root cause:** the firmware links with `--specs=nano.specs` (newlib-nano),
which strips floating-point support out of `printf`/`snprintf` unless you
explicitly pull it back in with `-Wl,-u,_printf_float`. Every number on the
display is written with `%.0f`/`%+.0f`, so each one silently renders as
nothing -- exactly the blank gaps next to `IN:`, `OUT:`, `X1:`, `X2:`, etc.

**Fix:** add one line to `Makefile`, after the `include $(SYSTEM_FILES_DIR)/Makefile`:
```
LDFLAGS += -Wl,-u,_printf_float
```
Low-risk, well-understood, first thing I'll do in the implementation pass.

## 1. UI paging (confirmed: dedicated keybed key)

One of the 16 keybed keys becomes a **page-cycle** button. Each press
advances to the next page, wrapping around. Three pages:

- **Home** -- crossover frequencies (KNOB_1/2), global input/output trim
  (KNOB_7/8), per-bus output gain readout.
- **Band** -- selected band's threshold/ratio/attack/release (KNOB_3-6,
  same pickup/catch behavior as today), mute state.
- **Trim** -- see point 3 below (proposed: input/output L-R balance).

The gain-reduction/level meters are **not** page content -- see point 4,
they're a persistent strip at the bottom of the screen on every page, so
you always have visual feedback regardless of which page you're editing.

KNOB_1-6 stay "hot" regardless of which page is displayed (only the display
changes, not what the knobs control) -- only KNOB_7/8's meaning changes
between Home and Trim pages, since there aren't enough knobs for every
parameter to get its own permanent control.

## 2. Per-channel input/output gain (proposed: balance knobs, not independent L/R)

Point 3 asked for per-channel gain in addition to global. There are only 8
knobs total and they're already spoken for, so I'm proposing **balance**
knobs rather than fully-independent L/R knobs, on the Trim page:

- KNOB_7 (Trim page) = **Input balance**: centered = no offset; turning it
  raises one channel and lowers the other by the same amount relative to
  the global input trim on the Home page.
- KNOB_8 (Trim page) = **Output balance**, same idea, relative to the
  global output trim.

This gets you independent per-channel control with only one extra page and
no extra knobs, and "trim relative to a master level" is a standard
mixing-console idiom. The alternative is fully independent L/R gain knobs,
which would need a 4th page (In-L/In-R, Out-L/Out-R can't both fit on one
2-knob page). **Flag if you want the independent-knobs version instead** --
it's more pages/knob-juggling but gives you non-relative control.

## 3. 16-key band select + mute (confirmed: 3 select + 3 mute, no solo)

- 3 keys: select Low/Mid/High (lit bright = selected, dim = not).
- 3 keys: toggle mute for that band directly (lit = muted, dim = unmuted).
- Soloing a band = manually mute the other two. No separate solo logic,
  no overlay/restore state to maintain -- simplest possible implementation,
  matches your answer.
- 1 key: page cycle (point 1).
- 7 of 16 keys used; **9 free** for later (presets, global bypass, tap
  tempo, etc. -- not scoping those now).

**Open implementation detail (not a decision for you, just flagging it):**
`daisy_field.h`'s `LED_KEY_*` enum and the keyboard-scan index order aren't
necessarily the same ordering -- I'll need to check `daisy_field.cpp`'s
scan code against the LED enum to make sure "key 3" and "LED above key 3"
actually refer to the same physical key before wiring this up, rather than
assume the enum order matches.

Bypass (currently on SW_2 long-press) stays, since nothing in your feedback
removed it -- **proposed**: since SW_2 is no longer doing mute duty, it
becomes a plain short-press bypass toggle for the selected band. SW_1
becomes the meter-mode cycle button (point 4). Flag if you'd rather SW_2
stay a dead/spare button instead of inheriting bypass.

## 4. Meter redesign (confirmed: all 3 bars same mode, cycled by a button)

- **Orientation**: horizontal, one full-width bar per band, stacked (3
  rows) instead of 3 narrow vertical columns -- roughly 120px of resolution
  per bar instead of ~22px.
- **Mode**: Input level / Output level / Gain reduction. All three bars
  always show the *same* mode at once. SW_1 short-press cycles mode
  (confirmed above); a small label shows which mode is active.
- **Range**: GR mode 0 to -24dB (unchanged); level modes -48dBFS to 0dBFS
  (proposed range, since gain-reduction's 24dB window doesn't fit signal
  level dynamics -- flag if you want a different window).
- **Peak hold**: a trailing 1px marker that jumps instantly to a new peak
  and decays at a fixed rate (proposed: ~20dB/sec fall, standard VU-style
  peak behavior) rather than just tracking the live level.
- Persistent across pages (point 1), not something you toggle on/off per
  page.

## Implementation order

1. `-Wl,-u,_printf_float` fix (point 0) -- quick, isolated, verify numbers
   render on actual hardware before anything else.
2. Keybed wiring: confirm key-index <-> LED-index mapping against
   `daisy_field.cpp`, implement page-cycle + band-select + mute keys.
3. Page system in `UpdateDisplay()`: persistent meter strip + swappable
   Home/Band/Trim content above it.
4. Meter redesign: horizontal bars, mode cycling on SW_1, peak-hold.
5. Trim page + balance knobs; wire KNOB_7/8's dual meaning (Home vs Trim).
6. Re-run `tools/dsp_verification.py` (nothing in this phase touches the
   DSP math, so it should be unaffected, but cheap to confirm) and do a
   full `make` + flash + a pass through the manual checklist in
   `TESTING.md`/`OPERATIONS.md`, updated for the new controls.

## Decisions I made that are worth double-checking

- Balance knobs instead of independent L/R gain knobs (point 2).
- SW_2 inherits bypass as a plain short-press now that it's free of mute
  duty; SW_1 becomes meter-mode cycle.
- Level-meter range of -48dBFS to 0dBFS.
- Peak-hold fall rate of ~20dB/sec.

Everything else above came directly from your answers. Let me know if
these look right, or if you want changes before I start building.
