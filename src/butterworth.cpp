#include "butterworth.h"
#include <math.h>
#include "daisysp.h" // for TWOPI_F only -- this header must stay hardware-independent

using namespace butterworth;

void Butterworth::Reset()
{
    float omega_c   = cutoff_ * two_pi_d_sr_;
    float K = tanf(omega_c/2);
    float K2 = K * K; // NOTE: this was previously `exp2(K)` (2^K), which is not
                       // K-squared and silently broke every coefficient derived
                       // from it. See ANALYSIS.md.

    if (filter_type_ == butterworth::FilterType::ALLPASS){
        a0_ = 1.0;
        a1_ = 2 * (K2 - 1) / (1 + M_SQRT2 * K + K2);
        a2_ = (1 - M_SQRT2 * K + K2) / (1 + M_SQRT2 * K + K2);

        b0_ = a2_;
        b1_ = a1_;
        b2_ = a0_;
    } else{
        b0_ = K2 / (1 + M_SQRT2 * K + K2);
        b1_ = 2 * b0_;
        b2_ = b0_;

        a0_ = 1.0;
        a1_ = 2.0 * (K2 - 1.0) / (1.0 + M_SQRT2 * K + K2);
        a2_ = (1 - M_SQRT2 * K + K2) / (1 + M_SQRT2 * K + K2);

        if(filter_type_ == butterworth::FilterType::HIGHPASS){
            b0_ = 1.0 / (1 + M_SQRT2 * K + K2);
            b1_ = -2.0 * b0_;
            b2_ = b0_;
        }
    }
}

void Butterworth::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    two_pi_d_sr_ = TWOPI_F / sample_rate_;

    cutoff_ = 500.f;
    filter_type_    = butterworth::FilterType::LOWPASS;

    Reset();

    xnm1_ = xnm2_ = ynm1_ = ynm2_ = 0.0f;
}

float Butterworth::Process(float in)
{
    float xn, yn;
    float a1 = a1_, a2 = a2_; // a0_ is always 1.0 and unused in the recurrence below
    float b0 = b0_, b1 = b1_, b2 = b2_;
    float xnm1 = xnm1_, xnm2 = xnm2_, ynm1 = ynm1_, ynm2 = ynm2_;

    xn   = in;
    yn   = (b0 * xn + b1 * xnm1 + b2 * xnm2 - a1 * ynm1 - a2 * ynm2);
    xnm2 = xnm1;
    xnm1 = xn;
    ynm2 = ynm1;
    ynm1 = yn;

    xnm1_ = xnm1;
    xnm2_ = xnm2;
    ynm1_ = ynm1;
    ynm2_ = ynm2;

    return yn;
}
