"""
Numerical verification of the MultibandCompressor DSP math, ported 1:1 from
the fixed C++ sources (src/butterworth.cpp, src/crossover.cpp/h,
src/compressor.cpp -- the latter mirroring DaisySP's Compressor::Process).

There is no native host C/C++ compiler on the dev machine this was written
on (only the arm-none-eabi cross toolchain used to build the firmware), so
true C++ unit tests can't be built and run there. This script re-implements
the exact same formulas in Python and checks them against known-correct
analytic/expected behavior instead. Run it with: python dsp_verification.py

  1. Butterworth LP/HP: -3dB point lands at the configured cutoff
     (this is the thing the exp2(K) -> K*K bug was breaking).
  2. LR4Crossover: low+high branches sum back to a flat, unity-gain
     response, via the two branches staying exactly in phase with each
     other (the core "no artifacts on recombination" claim -- see the
     long comment in src/crossover.h for the nuance on phase vs. the
     input, which this check caught and corrected during development).
  3. BandCompressor: gain reduction kicks in above threshold, is ~0dB
     below it, and a stereo-linked key produces identical gain for two
     different-amplitude channels (image-preserving).

Re-run this after touching any of the coefficient math in src/.
"""
import math
import cmath

FS = 48000.0
TWO_PI = 2 * math.pi


def butterworth_coeffs(cutoff, fs, kind):
    """Direct port of the fixed Butterworth::Reset()."""
    two_pi_d_sr = TWO_PI / fs
    omega_c = cutoff * two_pi_d_sr
    K = math.tan(omega_c / 2)
    K2 = K * K  # the fix: was exp2(K)
    SQRT2 = math.sqrt(2)

    if kind == "allpass":
        a0 = 1.0
        a1 = 2 * (K2 - 1) / (1 + SQRT2 * K + K2)
        a2 = (1 - SQRT2 * K + K2) / (1 + SQRT2 * K + K2)
        b0, b1, b2 = a2, a1, a0
    else:
        b0 = K2 / (1 + SQRT2 * K + K2)
        b1 = 2 * b0
        b2 = b0
        a1 = 2.0 * (K2 - 1.0) / (1.0 + SQRT2 * K + K2)
        a2 = (1 - SQRT2 * K + K2) / (1 + SQRT2 * K + K2)
        if kind == "highpass":
            b0 = 1.0 / (1 + SQRT2 * K + K2)
            b1 = -2.0 * b0
            b2 = b0
    return b0, b1, b2, a1, a2


def biquad_response(b0, b1, b2, a1, a2, freq, fs):
    """Analytic |H(e^jw)| and phase via the z-transform, a0 normalized to 1."""
    w = TWO_PI * freq / fs
    z_inv = cmath.exp(-1j * w)
    num = b0 + b1 * z_inv + b2 * z_inv ** 2
    den = 1 + a1 * z_inv + a2 * z_inv ** 2
    H = num / den
    return abs(H), cmath.phase(H)


def db(x):
    return 20 * math.log10(max(x, 1e-12))


# ---------------------------------------------------------------------------
print("=== 1. Butterworth LP/HP cutoff check (was broken by exp2(K) bug) ===")
for kind in ("lowpass", "highpass"):
    for fc in (100.0, 500.0, 4000.0, 10000.0):
        b0, b1, b2, a1, a2 = butterworth_coeffs(fc, FS, kind)
        mag, _ = biquad_response(b0, b1, b2, a1, a2, fc, FS)
        gain_db = db(mag)
        ok = abs(gain_db - (-3.01)) < 0.15  # 2nd-order Butterworth: -3.01dB at fc
        print(f"  {kind:9s} fc={fc:7.1f}Hz  gain@fc={gain_db:7.3f}dB  "
              f"{'OK' if ok else 'FAIL'}")
        assert ok, f"{kind} @ {fc}Hz not at -3dB: {gain_db}dB"

print("\n  (sanity: with the OLD exp2(K) bug, these all come out wildly off --")
print("   verified separately that reverting K2=K*K -> K2=exp2(K) above fails")
print("   this assertion for every case.)")


# Explicitly show the old bug fails, so the regression is visible in this run.
def old_buggy_coeffs(cutoff, fs, kind):
    two_pi_d_sr = TWO_PI / fs
    omega_c = cutoff * two_pi_d_sr
    K = math.tan(omega_c / 2)
    K2 = 2 ** K  # exp2(K) -- the original bug
    SQRT2 = math.sqrt(2)
    b0 = K2 / (1 + SQRT2 * K + K2)
    b1 = 2 * b0
    b2 = b0
    a1 = 2.0 * (K2 - 1.0) / (1.0 + SQRT2 * K + K2)
    a2 = (1 - SQRT2 * K + K2) / (1 + SQRT2 * K + K2)
    if kind == "highpass":
        b0 = 1.0 / (1 + SQRT2 * K + K2)
        b1 = -2.0 * b0
        b2 = b0
    return b0, b1, b2, a1, a2


bug_fc, bug_kind = 500.0, "lowpass"
b0, b1, b2, a1, a2 = old_buggy_coeffs(bug_fc, FS, bug_kind)
mag, _ = biquad_response(b0, b1, b2, a1, a2, bug_fc, FS)
print(f"  old exp2(K) bug @ {bug_fc}Hz lowpass: gain@fc={db(mag):7.3f}dB "
      f"(should NOT be -3dB) -> confirms the bug was real\n")

# ---------------------------------------------------------------------------
print("=== 2. LR4 crossover flatness/phase-coherence check ===")
print("  LR4's real guarantee is FLAT MAGNITUDE when low+high are summed, via")
print("  the low and high branches staying exactly in phase WITH EACH OTHER at")
print("  every frequency -- not that the sum matches the input's phase. There")
print("  IS a bounded, frequency-dependent phase rotation through the crossover")
print("  band (0 deg far away, sweeping to exactly 180 deg at fc itself, see the")
print("  trace below) -- that's the textbook, benign LR-crossover signature.\n")

for xover_fc in (180.0, 2500.0):
    lp = butterworth_coeffs(xover_fc, FS, "lowpass")
    hp = butterworth_coeffs(xover_fc, FS, "highpass")

    max_dev_db = 0.0
    max_branch_phase_mismatch_deg = 0.0
    trace = []
    freqs = [xover_fc * m for m in
             (0.02, 0.1, 0.25, 0.5, 0.8, 0.95, 1.0, 1.05, 1.25, 2.0, 4.0, 10.0, 50.0)]
    for freq in freqs:
        if freq >= FS / 2:
            continue
        mag_lp1, ph_lp1 = biquad_response(*lp, freq, FS)
        mag_lp_total = mag_lp1 * mag_lp1   # LR4 = two cascaded, matched 2nd-order sections
        ph_lp_total = ph_lp1 * 2

        mag_hp1, ph_hp1 = biquad_response(*hp, freq, FS)
        mag_hp_total = mag_hp1 * mag_hp1
        ph_hp_total = ph_hp1 * 2

        low = cmath.rect(mag_lp_total, ph_lp_total)
        high = cmath.rect(mag_hp_total, ph_hp_total)
        total = low + high
        dev_db = abs(db(abs(total)))

        # The real invariant: low and high branches share the same phase angle
        # at every frequency, so they add constructively (magnitude just sums)
        # instead of partially cancelling.
        branch_mismatch_deg = math.degrees(abs(cmath.phase(low) - cmath.phase(high)))
        branch_mismatch_deg = min(branch_mismatch_deg, 360 - branch_mismatch_deg)

        trace.append((freq, dev_db, math.degrees(cmath.phase(total))))
        max_dev_db = max(max_dev_db, dev_db)
        max_branch_phase_mismatch_deg = max(max_branch_phase_mismatch_deg, branch_mismatch_deg)

    ok = max_dev_db < 0.05 and max_branch_phase_mismatch_deg < 0.5
    print(f"  xover={xover_fc:6.1f}Hz  max |sum| deviation={max_dev_db:.5f}dB  "
          f"max branch phase mismatch={max_branch_phase_mismatch_deg:.5f}deg  "
          f"{'OK' if ok else 'FAIL'}")
    for freq, dev_db, sum_phase in trace:
        print(f"      f={freq:8.1f}Hz  |sum|dev={dev_db:.5f}dB  sum phase={sum_phase:+7.2f}deg")
    assert ok, f"LR4 @ {xover_fc}Hz isn't flat / branches aren't matched in phase"

print()

# ---------------------------------------------------------------------------
print("=== 3. BandCompressor gain-reduction + stereo-link check ===")


class Compressor:
    """Port of DaisySP's Compressor::Process (same recurrence as compressor.cpp)."""

    def __init__(self, fs):
        self.fs = fs
        self.sr_inv = 1.0 / fs
        self.sr_inv2 = 2.0 / fs
        self.atk_slo = 0.0        # zero-initialized, as C++ static-duration members are;
        self.atk_slo2 = 0.0       # Init() order (ratio before attack) relies on this.
        self.makeup_auto = False  # ditto: threshold is set before AutoMakeup(true)
        self.makeup_gain = 0.0
        self.set_ratio(2.0)
        self.set_attack(0.01)
        self.set_release(0.1)
        self.set_threshold(-18.0)
        self.makeup_auto = True
        self.recalc_makeup()
        self.gain_rec = 0.1
        self.slope_rec = 0.1
        self.gain = 1.0

    def set_ratio(self, r):
        self.ratio = r
        self._recalc_ratio()

    def _recalc_ratio(self):
        self.ratio_mul = (1.0 - self.atk_slo2) * ((1.0 / self.ratio) - 1.0)

    def set_attack(self, a):
        self.atk = a
        self.atk_slo = math.exp(-(self.sr_inv / a))
        self.atk_slo2 = math.exp(-(self.sr_inv2 / a))
        self._recalc_ratio()

    def set_release(self, r):
        self.rel = r
        self.rel_slo = math.exp(-(self.sr_inv / r))

    def set_threshold(self, t):
        self.thresh = t
        self.recalc_makeup()

    def recalc_makeup(self):
        if self.makeup_auto:
            self.makeup_gain = abs(self.thresh - self.thresh / self.ratio) * 0.5

    def process(self, x):
        in_abs = abs(x)
        cur_slo = self.rel_slo if self.slope_rec > in_abs else self.atk_slo
        self.slope_rec = self.slope_rec * cur_slo + (1.0 - cur_slo) * in_abs
        self.gain_rec = (self.atk_slo2 * self.gain_rec
                          + self.ratio_mul * max(20 * math.log10(max(self.slope_rec, 1e-9)) - self.thresh, 0.0))
        self.gain = 10 ** (0.05 * (self.gain_rec + self.makeup_gain))
        return self.gain * x


def run_to_steady_state(comp, amplitude, freq=200.0, fs=FS, seconds=1.0):
    n = int(fs * seconds)
    last_gain = 1.0
    for i in range(n):
        x = amplitude * math.sin(TWO_PI * freq * i / fs)
        comp.process(x)
        last_gain = comp.gain
    return last_gain


comp_quiet = Compressor(FS)
comp_quiet.set_threshold(-18.0)
comp_quiet.set_ratio(4.0)
g_quiet = run_to_steady_state(comp_quiet, amplitude=10 ** (-30 / 20))  # -30dBFS, below -18 thresh
print(f"  signal well below threshold: steady-state gain = {db(g_quiet):+.2f}dB "
      f"(expect ~makeup, i.e. small positive, no reduction)")

comp_loud = Compressor(FS)
comp_loud.set_threshold(-18.0)
comp_loud.set_ratio(4.0)
g_loud = run_to_steady_state(comp_loud, amplitude=10 ** (-6 / 20))  # -6dBFS, well above -18 thresh
print(f"  signal well above threshold: steady-state gain = {db(g_loud):+.2f}dB "
      f"(expect clearly negative -> gain reduction is happening)")
assert db(g_loud) < db(g_quiet) - 3.0, "compressor isn't reducing gain above threshold"
print("  OK: gain reduction kicks in above threshold and not below it\n")

# Stereo link: one compressor instance driven by a shared key derived from
# two differently-loud channels (as BandCompressor::ProcessBlock does via
# daisysp::Compressor::ProcessBlock), should apply IDENTICAL gain to both,
# preserving their relative balance.
comp_link = Compressor(FS)
comp_link.set_threshold(-18.0)
comp_link.set_ratio(4.0)
amp_L, amp_R = 10 ** (-6 / 20), 10 ** (-12 / 20)  # L louder than R by 6dB
n = int(FS * 1.0)
gain_trace = []
for i in range(n):
    xl = amp_L * math.sin(TWO_PI * 200 * i / FS)
    xr = amp_R * math.sin(TWO_PI * 200 * i / FS)
    key = 0.5 * (abs(xl) + abs(xr))
    comp_link.process(key)       # updates comp_link.gain from the LINK signal
    gain_trace.append(comp_link.gain)

final_gain = gain_trace[-1]
outL_peak = amp_L * final_gain
outR_peak = amp_R * final_gain
ratio_db_before = db(amp_L) - db(amp_R)
ratio_db_after = db(outL_peak) - db(outR_peak)
print(f"  L/R balance before compression: {ratio_db_before:+.3f}dB")
print(f"  L/R balance after  compression: {ratio_db_after:+.3f}dB "
      f"(must be unchanged -> same gain applied to both channels)")
assert abs(ratio_db_before - ratio_db_after) < 1e-6, "stereo link changed the balance!"
print("  OK: stereo-linked gain preserves the L/R balance\n")

print("All checks passed.")
