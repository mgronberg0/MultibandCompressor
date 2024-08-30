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

    void MeasureLevelPeak(float in);

    /** Applies compression based on existing internal values for level etc. Does not apply inputgain or output gain
        \return compressed sample output
    */
    float Process(float in);


    /** Applies input gain, measures input, Calculates/applies gain and output gain, returns y[n]
        \return compressed sample output
    */
    float ProcessFull(float in);


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

    inline void SetRmsWindowSec(float rms_wind_sec){
        rms_window_sec_ = rms_wind_sec;
        Reset();
    }

    inline void SetInputGainLin(float inGain_dB){
        input_gain_lin_ = dB2Linear(inGain_dB);
    }

    inline void SetOutputGainLin(float outGain_dB){
        output_gain_lin_ = dB2Linear(outGain_dB);
    }

    inline float ApplyInputGainLin(float in){
        return in * input_gain_lin_;
    }

    inline float ApplyOutputGainLin(float in){
        return in * output_gain_lin_;
    }

    inline float ApplyAllGain(float in){
        return in * input_gain_lin_ * env_gain_lin_ * output_gain_lin_;
    }

    inline static float dB2Linear(float dB){
        return powf(10.0f, dB / 20.0f);
    }

    inline static float linear2dB(float lin){
        return 20*log10f(lin);
    }

  //private
  public:
    // Arguments
    double sample_rate_, sample_period_, thresh_dB_, ratio_, attack_sec_, release_sec_,rms_window_sec_;
    double input_gain_dB_, output_gain_dB_;
    LevelFollowerType levelFollowerType_;
    // Derived
    double at_,rt_,rms_smoothing_factor_,slope_,thresh_lin_;
    double input_gain_lin_, output_gain_lin_;
    // State memory / buffers
    double xRMS_,xSqr_m1_, xPeak_, env_gain_lin_;
    double truePeak_dB_;
    
    void Reset();
};
} // namespace daisysp
#endif
#endif
