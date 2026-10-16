#include "crossover.h"

using namespace multiband;
using namespace butterworth;

void LR4Crossover::Init(float sample_rate)
{
    lp1_.Init(sample_rate);
    lp2_.Init(sample_rate);
    hp1_.Init(sample_rate);
    hp2_.Init(sample_rate);

    lp1_.SetFilterType(FilterType::LOWPASS);
    lp2_.SetFilterType(FilterType::LOWPASS);
    hp1_.SetFilterType(FilterType::HIGHPASS);
    hp2_.SetFilterType(FilterType::HIGHPASS);
}

void LR4Crossover::SetCutoff(float freq_hz)
{
    lp1_.SetCutoff(freq_hz);
    lp2_.SetCutoff(freq_hz);
    hp1_.SetCutoff(freq_hz);
    hp2_.SetCutoff(freq_hz);
}

void LR4Crossover::Process(float in, float *low, float *high)
{
    *low  = lp2_.Process(lp1_.Process(in));
    *high = hp2_.Process(hp1_.Process(in));
}
