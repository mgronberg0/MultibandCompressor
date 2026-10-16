#pragma once
#ifndef CROSSOVER_H
#define CROSSOVER_H

#include "butterworth.h"

namespace multiband
{
/** 4th-order Linkwitz-Riley crossover (LR4).

    Splits a single signal into a lowpass branch and a highpass branch
    built from two cascaded, identically-tuned 2nd-order Butterworth
    sections each (4th order total, -24dB/oct). This is the classic
    Linkwitz-Riley construction: unlike a single 2nd-order (LR2) split,
    LR4's low and high branches stay exactly in phase WITH EACH OTHER at
    every frequency, so low+high sums back to a perfectly flat, unity-gain
    response with no polarity flip required and no magnitude dip/notch at
    the crossover -- that's what keeps a multiband split from introducing
    comb-filtering artifacts when the bands are recombined.

    Note this does NOT mean the recombined signal is phase-identical to
    the input: there's a well-known, bounded, frequency-dependent phase
    rotation through the crossover band (0 degrees far from it, sweeping
    smoothly up to exactly 180 degrees right at the crossover frequency
    itself, then back down on the other side). That phase rotation is the
    textbook LR-crossover signature and doesn't cause audible artifacts on
    its own; it's the flat magnitude that matters, and that's what this
    class guarantees. Verified numerically for this implementation --
    see the DSP checks referenced in ANALYSIS.md/TESTING.md.

    Each instance holds its own filter state, so use one instance per
    audio channel (L/R) per crossover point -- never share one across
    channels.
*/
class LR4Crossover
{
  public:
    LR4Crossover() {}
    ~LR4Crossover() {}

    /** Initializes all internal filter stages.
        \param sample_rate - sample rate of the audio engine being run.
    */
    void Init(float sample_rate);

    /** Sets the crossover frequency in Hz for both branches. */
    void SetCutoff(float freq_hz);

    /** Splits `in` into a lowpass and highpass branch.
        \param in audio input sample
        \param low  output: content below the crossover frequency
        \param high output: content above the crossover frequency
    */
    void Process(float in, float *low, float *high);

  private:
    butterworth::Butterworth lp1_, lp2_, hp1_, hp2_;
};

} // namespace multiband

#endif
