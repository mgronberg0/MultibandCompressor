#pragma once
#ifndef BAND_COMPRESSOR_H
#define BAND_COMPRESSOR_H

#include <cstddef>
#include "daisysp.h"

namespace multiband
{
/** Stereo, link-detected compressor for a single frequency band.

    Wraps daisysp::Compressor's existing sidechain/multichannel support
    so that ONE gain-reduction value -- derived from both channels --
    is applied to L and R. Without this, each channel would compute its
    own independent envelope and the stereo image could shift under
    compression, which matters a lot for a mastering-chain tool.

    Also owns this band's mute/bypass state, since those are musically
    tied to "how this band's compressor behaves" rather than anything
    the crossover needs to know about.
*/
class BandCompressor
{
  public:
    BandCompressor() {}
    ~BandCompressor() {}

    /** Initializes the compressor for this band.
        \param sample_rate - sample rate of the audio engine being run.
    */
    void Init(float sample_rate);

    /** Processes one block of stereo audio for this band.
        If muted, output is silence. If bypassed, output equals input
        (unprocessed passthrough) so the band can still be heard/metered
        without compression applied.
        \param inL/inR   input buffers, `size` samples each
        \param outL/outR output buffers, `size` samples each (may alias input)
        \param size      number of samples in the block (must be <= kMaxBlockSize)
    */
    void ProcessBlock(float *inL, float *inR, float *outL, float *outR, size_t size);

    /** Sets the compression threshold in dB (expects 0 -> -80). */
    void SetThreshold(float db) { comp_.SetThreshold(db); }
    /** Sets the compression ratio (expects 1 -> 40). */
    void SetRatio(float ratio) { comp_.SetRatio(ratio); }
    /** Sets the attack time in seconds (expects 0.001 -> 10). */
    void SetAttack(float seconds) { comp_.SetAttack(seconds); }
    /** Sets the release time in seconds (expects 0.001 -> 10). */
    void SetRelease(float seconds) { comp_.SetRelease(seconds); }

    // Not const: daisysp::Compressor's own getters aren't const-qualified.
    float GetThreshold() { return comp_.GetThreshold(); }
    float GetRatio() { return comp_.GetRatio(); }
    float GetAttack() { return comp_.GetAttack(); }
    float GetRelease() { return comp_.GetRelease(); }

    void SetMuted(bool muted) { muted_ = muted; }
    bool GetMuted() const { return muted_; }

    void SetBypassed(bool bypassed) { bypassed_ = bypassed; }
    bool GetBypassed() const { return bypassed_; }

    /** Current gain reduction in dB (<= 0; more negative = more reduction).
        Reflects the last sample processed in the most recent block --
        fine for metering at block/display rate.
    */
    float GetGainReductionDb() { return comp_.GetGain(); }

  private:
    static const size_t kMaxBlockSize = 64;

    daisysp::Compressor comp_;
    bool                 muted_    = false;
    bool                 bypassed_ = false;
    float                key_[kMaxBlockSize];
};

} // namespace multiband

#endif
