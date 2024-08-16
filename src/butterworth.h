#pragma once
#ifndef BUTTERWORTH_H
#define BUTTERWORTH_H

#include <stdint.h>
#ifdef __cplusplus

namespace butterworth
{
    enum class FilterType {
    LOWPASS,
    HIGHPASS,
    ALLPASS
};
/** Butterworth Second Order Filter */
class Butterworth
{
  public:
    Butterworth() {}
    ~Butterworth() {}
    /** Initializes the butterworth module.
        \param sample_rate - The sample rate of the audio engine being run. 
    */
    void Init(float sample_rate);


    /** Filters the input signal
        \return filtered output
    */
    float Process(float in);


    /** Sets filter type
        \param ft : Set filter type
    */
    inline void SetFilterType(FilterType ft)
    {
        filter_type_ = ft;
        Reset();
    }


    /** Sets filter cutoff in Hz
        \param cutoff : Set filter cutoff.
    */
    inline void SetCutoff(float cutoff)
    {
        cutoff_ = cutoff;
        Reset();
    }

  private:
    float sample_rate_, cutoff_, b0_, b1_, b2_, a0_, a1_, a2_,
        two_pi_d_sr_, xnm1_, xnm2_, ynm1_, ynm2_;
    FilterType filter_type_;
    void Reset();
};
} // namespace daisysp
#endif
#endif
