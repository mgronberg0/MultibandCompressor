#include "compressor.h"
#include <cmath>

using namespace multiband;

void BandCompressor::Init(float sample_rate)
{
    comp_.Init(sample_rate);
    comp_.AutoMakeup(true);
    muted_    = false;
    bypassed_ = false;
}

void BandCompressor::ProcessBlock(float *inL,
                                   float *inR,
                                   float *outL,
                                   float *outR,
                                   size_t size)
{
    if(size > kMaxBlockSize)
        size = kMaxBlockSize; // safety clamp; configure the audio block size <= kMaxBlockSize

    if(muted_)
    {
        for(size_t i = 0; i < size; i++)
        {
            outL[i] = 0.f;
            outR[i] = 0.f;
        }
        return;
    }

    if(bypassed_)
    {
        for(size_t i = 0; i < size; i++)
        {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }

    // Stereo-linked detection: feed the compressor's sidechain/key input
    // with a signal derived from both channels so the gain reduction
    // computed is identical for L and R (keeps the stereo image stable).
    for(size_t i = 0; i < size; i++)
        key_[i] = 0.5f * (fabsf(inL[i]) + fabsf(inR[i]));

    float *in[2]  = {inL, inR};
    float *out[2] = {outL, outR};
    comp_.ProcessBlock(in, out, key_, 2, size);
}
