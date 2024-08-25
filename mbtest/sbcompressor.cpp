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
    attack_sec_ = 0.200;
    release_sec_ = 0.200;

    levelFollowerType_ = LevelFollowerType::RMS;

    xRMS_ = xPeak_ = 0.0;
    
    xRMS_m1_ = 0.0;
    xPeak_m1_ = 0.0;

    Reset();
    
}

void SBCompressor::Reset()
{
    at_ = 1.0 - exp((-2.2*sample_period_)/attack_sec_);
    rt_ = 1.0 - exp((-2.2*sample_period_)/release_sec_);

    float thresh_linear = pow10f(thresh_dB_/20.0);
    thres_log2_ = 0.5*log2(thresh_linear);
    slope_ = 1.0 - (1.0/ratio_);


    // self.AT = 1 - np.exp((-2.2*self.Ts)/self.attack)
    // self.RT = 1 - np.exp((-2.2*self.Ts)/self.release)
    // self.TAV = self.AT

    // thresh_lin = np.power(10,self.thresh_dB/20)
    // self.threshLog2 = 0.5*np.log2(thresh_lin)
    // self.slope = 1-(1/self.ratio)   
}

void SBCompressor::MeasureLevelRMS(float in){
    float x_n_squared = powf(in,2.0);
    if (x_n_squared > xRMS_m1_){
        xRMS_ = (1.0-at_)*xRMS_m1_ + at_*x_n_squared;
    }else{
        xRMS_ = (1.0-rt_)*x_n_squared;
    }
    xRMS_m1_ = xRMS_;   // storing intermediate RMS value
    xRMS_ = sqrt(xRMS_);
}

