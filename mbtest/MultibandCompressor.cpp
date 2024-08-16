/* Roadmap:
1) implement linkwitz riley filter
2) create parameters to control where splits happen 
3) prove that splitting into 3 bands doesn't produce artifacts
4) create audio parameters for the 3 compressor bands
5) add 2 remaning compressors
6) ability to mute solo bypass individual compressors
7) add input and output gain to offset changes in output level
8) delete these comments
*/

/*  How to derive butterworth filter coefficients: from the s domain transfer function:
https://stackoverflow.com/questions/20924868/calculate-coefficients-of-2nd-order-butterworth-low-pass-filter?rq=3

starting with the transferfuction for the butterworth filter
G(s) = wc^2 / (s^2 + s*sqrt(2)*wc + wc^2)

where "wc" is the cutoff frequency, apply the biliniar z-transform, i.e. substitute s = 2/T*(1-z^-1)/(1+z^-1)
where T is the sampling period

thus our transfer function in z domain is:
g(z) = wc^2 / ((2/T*(1-z^-1)/(1+z^-1))^2 + (2/T*(1-z^-1)/(1+z^-1))*sqrt(2)*wc + wc^2)

the cutoff frequency needs to be pre-warmped to compensate for the non-lienar relation between analog and digital frequency introduced by the z-transfrom

wc = 2/T * tan(wd*T/2)
where "wd" is the "desired" cutoff frequency in [rads/s]

let C = tan(wd*T/2), for convenience, so taht wc = 2/T*C

substituting this into the equation, the 2/T factor drops out:

G(z) = C^2 / ((1-z^-1)/(1+z^-1))^2 + (1-z^-1)/(1+z^-1)*sqrt(2)*C + C^2)

multiply the numerator and denominator by (1+z^-1)^2 and expand, which yields:

G(z) = C^2*(1 + 2*z^-1 + z^-2) / (1 + sqrt(2)*C + C^2 + 2*(C^2-1)*z^-1 + (1-sqrt(2)*C+C^2)*z^-2')

Now, divide both numerator and denominator by the constant term from the denominator. For convenience, let D = 1 + sqrt(2)*C + C^2

G(z) = C^2/D*(1 + 2*z^-1 + z^-2) / (1 + 2*(C^2-1)/D*z^-1 + (1-sqrt(2)*C+C^2)/D*z^-2')

This form is equivalent to the one we are looking for:

G(z) = (b0 + b1*z^-1 + b2*z^-1) / (1 + a1*z^-1 +a2*z^-2)

So we get the coefficients by equating them:

a0 = 1

a1 = 2*(C^2-1)/D

a2 = (1-sqrt(2)*C+C^2)/D

b0 = C^2/D

b1 = 2*b0

b2 = b0

where, again, D = 1 + sqrt(2)*C + C^2, C = tan(wd*T/2), wd is the desired cutoff frequency [rad/s], T is the sampling period [s].



FYI If you need a high pass filter coefs, all you have to do is use the same code:

const double ita =1.0/ tan(M_PI*ff);
const double q=sqrt(2.0);
b0 = 1.0 / (1.0 + q*ita + ita*ita);
b1= 2*b0;
b2= b0;
a1 = 2.0 * (ita*ita - 1.0) * b0;
a2 = -(1.0 - q*ita + ita*ita) * b0;

but then after multiply all your b terms by ita^2 and negate b1

b0 = b0*ita*ita;
b1 = -b1*ita*ita;
b2 = b2*ita*ita;

now you have a 2nd order high pass filter

*/

#include "daisy_pod.h"
#include "daisysp.h"
#include "src/butterworth.h"

using namespace daisy;
using namespace daisysp;
using namespace butterworth;



static DaisyPod hw;

float sample_rate;
FilterType filterType;
float fc;
static Butterworth lpf;

static Parameter fcParam, p_knob1;
float oldk1, oldk2, k1, k2;

//Helper functions
void Controls();
void ConditionalParameter(float  oldVal,
                          float  newVal,
                          float &param,
                          float  update);
void UpdateKnobs();
void UpdateLeds();


void AudioCallback(AudioHandle::InputBuffer in, 
				   AudioHandle::OutputBuffer out, 
				   size_t size)
{
	Controls();

	for (size_t i = 0; i < size; i++)
	{
		out[0][i] = lpf.Process(in[0][i]);
		out[1][i] = lpf.Process(in[1][i]);
	}
}

int main(void)
{
	hw.Init();
	hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	sample_rate = hw.AudioSampleRate();

	// set up butterworth filter
	lpf.Init(sample_rate);
	fc = 4000.0f;
	filterType = butterworth::FilterType::LOWPASS;
	lpf.SetCutoff(fc);
	lpf.SetFilterType(filterType);

	// set parameters
	fcParam.Init(hw.knob1, 100, 20000, fcParam.LOGARITHMIC);
	p_knob1.Init(hw.knob1, 0, 1, Parameter::LINEAR);

	hw.led1.Set(1,0,0);
	
	hw.StartAdc();
	hw.StartAudio(AudioCallback);
	while(1) {}
}

void Controls()
{
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();

    UpdateKnobs();
	UpdateLeds();
}
void UpdateKnobs()
{
    k1 = hw.knob1.Process();
    k2 = hw.knob2.Process();

	ConditionalParameter(oldk1, k1, fc, fcParam.Process());

	lpf.SetCutoff(fc);
	oldk1 = k1;
    oldk2 = k2;
}
void UpdateLeds()
{
	float r = p_knob1.Process();
	hw.led1.Set(r,0,0);
}

//Updates values if knob had changed
void ConditionalParameter(float  oldVal,
                          float  newVal,
                          float &param,
                          float  update)
{
    if(abs(oldVal - newVal) > 0.00005)
    {
        param = update;
    }
}
