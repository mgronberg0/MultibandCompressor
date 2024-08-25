#pragma once
#ifndef SBCOMPRESSOR_H
#define SBCOMPRESSOR_H

#include <stdint.h>
#ifdef __cplusplus

namespace daisysp
{
    enum class LevelFollowerType {
    RMS,
    PEAK
};

/** Dynamic Range Compressor */
class SBCompressor
{
  public:
    SBCompressor() {}
    ~SBCompressor() {}
    /** Initializes the Compressor module.
        \param sample_rate - The sample rate of the audio engine being run. 
    */
    void Init(float sample_rate);

    void MeasureLevelRMS(float in);


    /** Filters the input signal
        \return filtered output
    */
    float Process(float in);


    /** Sets level follower type
        \param lf : Set level follower type
    */
    inline void SetLevelFollowerType(LevelFollowerType lf)
    {
        levelFollowerType_ = lf;
        Reset();
    }


    /** Sets threshold in dB
        \param thresh : Sets threshold in dB
    */
    inline void SetThresholdDB(float thresh)
    {
        thresh_dB_ = thresh;
        Reset();
    }

    inline void SetRatio(float ratio)
    {
        ratio_ = ratio;
        Reset();
    }

    inline void SetAttackSec(float atk){
        attack_sec_ = atk;
        Reset();
    }

    inline void SetReleaseSec(float rls){
        release_sec_ = rls;
        Reset();
    }

  //private
  public:
    double sample_rate_, sample_period_, thresh_dB_, ratio_, attack_sec_, release_sec_;
    double at_,rt_,slope_,thres_log2_;
    double xRMS_, xPeak_, xPeak_m1_,xRMS_m1_;
    LevelFollowerType levelFollowerType_;
    void Reset();
};
} // namespace daisysp
#endif
#endif
