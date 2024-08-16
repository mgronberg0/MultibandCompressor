#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "daisy_pod.h"
#include "daisysp.h"
#include "butterworth.h"

namespace test {
#include "printf.h"
#include "printf.c"
};

using namespace daisy;
using namespace daisysp;
//using namespace butterworth;

// Hardware vars
DaisyPod  hw;
Parameter p_knob1, p_knob2, fcParam;
float oldk1, oldk2, k1, k2;
int b2_toogle_state;
int b2_num_states;
// Audio vars
float sample_rate;

// Component Vars
Butterworth lpf_1,lpf_2;
FilterType filterType;
float fc;

//Helper functions
uint8_t sumbuff[1024];

// Function Protos
void UsbCallback(uint8_t* buf, uint32_t* len)
{
    for(size_t i = 0; i < *len; i++)
    {
        sumbuff[i] = buf[i];
    }
}
void PrintCoeffs(char buffer[]);
void Controls();
bool ConditionalParameter(float  oldVal,
                          float  newVal,
                          float &param,
                          float  update);
void UpdateKnobs();
void UpdateButtons();
void UpdateLeds();

void AudioCallback(AudioHandle::InputBuffer in, 
				   AudioHandle::OutputBuffer out, 
				   size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
        if (b2_toogle_state == 0){
            out[0][i] = lpf_1.Process(in[0][i]);

            out[1][i] = out[0][i];		
        }
        if (b2_toogle_state == 1){  // 4th order
            float y1 = lpf_1.Process(in[0][i]);
            float y2 = lpf_2.Process(y1);

            out[0][i] = y2;
            out[1][i] = out[0][i];
        }        
	}
}

int main(void)
{
    //hw.Configure();
    hw.Init();
    sample_rate = 48000;
    oldk1 = oldk2 = 0;
    k1 = k2 = 0;
    b2_toogle_state = 0;
    b2_num_states = 2;
    // USB stuff
    hw.usb_handle.Init(UsbHandle::FS_INTERNAL);

    /* char buff[2048];

    hw.usb_handle.TransmitInternal((uint8_t*)buff, strlen(buff));
    System::Delay(500);
    hw.usb_handle.SetReceiveCallback(UsbCallback, UsbHandle::FS_INTERNAL); */
    
    p_knob1.Init(hw.knob1, 0, 1, Parameter::LINEAR);
    p_knob2.Init(hw.knob2, 0, 1, Parameter::LINEAR);
    fcParam.Init(hw.knob1,10,18000,Parameter::LOGARITHMIC);

    // set up butterworth filter
	lpf_1.Init(sample_rate);
    lpf_2.Init(sample_rate);
    //bqtest.Init(sample_rate);
	fc = 200.0f;
	filterType =FilterType::LOWPASS;
	lpf_1.SetCutoff(fc);
	lpf_1.SetFilterType(filterType);
    lpf_2.SetCutoff(fc);
	lpf_2.SetFilterType(filterType);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        System::Delay(10);
        Controls();

    }
}

// slash r slash n does a clean newline
void PrintCoeffs(Butterworth filter){
    char buffer[2048];
    daisysp::FilterCoeffs coeffs = filter.GetCoefficients();
        test::sprintf_(buffer,"a0: %f  a1: %f  a2: %f  b0: %f  b1: %f  b2: %f\r\n"
                        "fc: %f omega_c: %f  K: %f  K2: %f\r\n"
                        "b2_state: %i\r\n\r\n",
            coeffs.a0,
            coeffs.a1,
            coeffs.a2,
            coeffs.b0,
            coeffs.b1,
            coeffs.b2,
            fc,
            filter.omega_c,
            filter.K,
            filter.K2,
            b2_toogle_state);
            hw.usb_handle.TransmitInternal((uint8_t*)buffer, strlen(buffer));

}

//Updates values if knob had changed
bool ConditionalParameter(float  oldVal,
                          float  newVal,
                          float &param,
                          float  update)
{
    if(abs(oldVal - newVal) > 0.0005)
    {
        param = update;
        return 1;
    }
    return 0;
}
void Controls()
{
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();

    UpdateKnobs();
    UpdateButtons();
	UpdateLeds();
}

void UpdateKnobs()
{
    
    k1 = p_knob1.Process();
    k2 = p_knob2.Process();
    //fc = fcParam.Process();
    if (ConditionalParameter(oldk1, k1, fc, fcParam.Process())){
        lpf_1.SetCutoff(fc);
        lpf_2.SetCutoff(fc);
    };
    
    oldk1 = k1;    
}

void UpdateButtons()
{
    if(hw.button1.RisingEdge())
    {
        PrintCoeffs(lpf_1);
    }

    if(hw.button2.RisingEdge())
    {
        b2_toogle_state += 1;
        b2_toogle_state = b2_toogle_state % b2_num_states;
        PrintCoeffs(lpf_1);
    }
}
void UpdateLeds()
{
    float r = 0, g = 0, b = 0;
    switch (b2_toogle_state)
    {
        case 0:
            hw.led2.Set(0, 0, 1);
            break;
        case 1:
            hw.led2.Set(0, 1, 0);
            break;
        case 2:
            hw.led2.Set(1, 0, 0);
            break;
        case 3:
            hw.led2.Set(.7, .7, .7);
            break;
        default: break;
    }
    hw.UpdateLeds();
}

