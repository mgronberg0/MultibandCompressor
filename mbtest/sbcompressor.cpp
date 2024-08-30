#include "SBcompressor.h"
#define __BSD_VISIBLE 1
#include <math.h>
#include "daisy_pod.h"
#include "daisysp.h"
#include "Utility/dsp.h"
#include <cmath>
#include <algorithm>
//#include "dsp.h"



using namespace daisysp;

void SBCompressor::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    sample_period_ = 1.0 / sample_rate_;
    thresh_dB_ = 5.0;
    ratio_ = 2.0;
    attack_sec_  = 0.200;
    release_sec_ = 0.200;
    input_gain_dB_  = 0, 
    output_gain_dB_ = 0;

    levelFollowerType_ = LevelFollowerType::RMS;

    xRMS_ = xPeak_ = xSqr_m1_ = 0.0;
    env_gain_lin_ = 1.0;
    
    rms_window_sec_ = attack_sec_;

    Reset();
    
}

void SBCompressor::Reset()
{
    at_ = 1.0f - expf(-1.0f / (sample_rate_*attack_sec_));
    rt_ = 1.0f - expf(-1.0f / (sample_rate_*release_sec_));
    rms_smoothing_factor_ = 1.0f - expf(-1.0f / (sample_rate_*rms_window_sec_));

    thresh_lin_ = dB2Linear(thresh_dB_);

    input_gain_lin_  = dB2Linear(input_gain_dB_);
    output_gain_lin_ = dB2Linear(output_gain_dB_);

    slope_ = 1.0 - (1.0/ratio_);
}

float SBCompressor::Process(float in){
    float env, envDelta = 0;

    if (levelFollowerType_ == LevelFollowerType::RMS){
        env = linear2dB(xRMS_);
    }
    else if(levelFollowerType_ == LevelFollowerType::PEAK){
        env = linear2dB(xPeak_);
    }

    if (env > thresh_dB_){
        envDelta = env - thresh_dB_;
        env_gain_lin_ = dB2Linear(envDelta*-slope_);
    }
    return in*env_gain_lin_;
}

float SBCompressor::ProcessFull(float in){
    float yn=0;
    float xn_inputGain = in * input_gain_lin_;
    if(levelFollowerType_ == LevelFollowerType::RMS){
        MeasureLevelRMS(xn_inputGain);
    }
    else if(levelFollowerType_ == LevelFollowerType::PEAK){
        MeasureLevelPeak(xn_inputGain);
    }
    yn = Process(xn_inputGain);
    return yn*output_gain_lin_;
}

void SBCompressor::MeasureLevelRMS(float in){
    float x_n_squared =in*in;
    xSqr_m1_ = (1.0-rms_smoothing_factor_)*xSqr_m1_ + rms_smoothing_factor_*x_n_squared;
    xRMS_ = sqrt(xSqr_m1_);
}

void SBCompressor::MeasureLevelPeak(float in){
    float abs_x = fabsf(in);
    
    if (abs_x > xPeak_){
        xPeak_ += (abs_x-xPeak_) * at_;
    }
    else{
        xPeak_ += (abs_x-xPeak_) * rt_;
    }

}

