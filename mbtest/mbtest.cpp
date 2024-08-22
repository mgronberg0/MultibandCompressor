#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "daisy_pod.h"
#include "daisysp.h"
#include "butterworth.h"
#include "dev\oled_ssd130x.h"

namespace test {
#include "printf.h"
#include "printf.c"
};

using namespace daisy;
using namespace daisysp;
//using namespace butterworth;

// Hardware vars
DaisyPod  hw;
Parameter p_knob1, p_knob2, fc1Param, fc2Param;
float oldk1, oldk2, k1, k2;
int b2_toogle_state;
int b2_num_states;

SSD130xI2CTransport::Config oledTransConfig;
OledDisplay<SSD130xI2c128x64Driver>::Config display_cfg;
OledDisplay<SSD130xI2c128x64Driver> display;


// Audio vars
float sample_rate;

// Component Vars
Butterworth lpf_1a,lpf_1b;
Butterworth hpf_2a,hpf_2b, lpf_2a,lpf_2b;
Butterworth hpf_3a,hpf_3b;
FilterType filterType;
float fc1,fc2;

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
        float y1, y2;
        if (b2_toogle_state == 0){  // Low band only
            y1 = lpf_1a.Process(in[0][i]);
            y2 = lpf_1b.Process(y1);

            out[0][i] = y2;
            out[1][i] = out[0][i];		
        }
        if (b2_toogle_state == 1){  // middle band only
            y1 = hpf_2a.Process(in[0][i]);
            y2 = hpf_2b.Process(y1);
            // Reuse y2
            y1 = lpf_2a.Process(y2);
            y2 = lpf_2b.Process(y1);


            out[0][i] = y2;
            out[1][i] = out[0][i];
        }
        if (b2_toogle_state == 2){  // High band only
            y1 = hpf_3a.Process(in[0][i]);
            y2 = hpf_3b.Process(y1);

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
    b2_num_states = 3;
    // USB stuff
    hw.usb_handle.Init(UsbHandle::FS_INTERNAL);
    // I2C stuff
    display_cfg.driver_config.transport_config.i2c_address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_100KHZ;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.scl = seed::D11;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.sda = seed::D12;
    // oledTransConfig.Defaults();
    // oledTransConfig.i2c_address = 0x3D;
    // oledTransConfig.i2c_config.pin_config.scl = {DSY_GPIOB, 11};
    // oledTransConfig.i2c_config.pin_config.sda = {DSY_GPIOB, 12};
    // oledTransConfig.i2c_config.speed = I2CHandle::Config::Speed::I2C_100KHZ;
    // display_cfg.transport_config = oledTransConfig;
    display.Init(display_cfg);

    display.Fill(true);
    display.Update();


    /* char buff[2048];

    hw.usb_handle.TransmitInternal((uint8_t*)buff, strlen(buff));
    System::Delay(500);
    hw.usb_handle.SetReceiveCallback(UsbCallback, UsbHandle::FS_INTERNAL); */
    
    p_knob1.Init(hw.knob1, 0, 1, Parameter::LINEAR);
    p_knob2.Init(hw.knob2, 0, 1, Parameter::LINEAR);
    fc1Param.Init(hw.knob1,10,18000,Parameter::LOGARITHMIC);
    fc1Param.Init(hw.knob2,10,18000,Parameter::LOGARITHMIC);

    // set up butterworth filter DO NOT FORGET THIS lol
	lpf_1a.Init(sample_rate);
    lpf_1b.Init(sample_rate);
    hpf_2a.Init(sample_rate);
    hpf_2b.Init(sample_rate);
    lpf_2a.Init(sample_rate);
    lpf_2b.Init(sample_rate);
    hpf_3a.Init(sample_rate);
    hpf_3b.Init(sample_rate);
    //bqtest.Init(sample_rate);
	fc1 = 200.0;
    fc2 = 2000.0;
	filterType =FilterType::LOWPASS;
    {  // Low Band Filter Defs
        lpf_1a.SetCutoff(fc1);
        lpf_1a.SetFilterType(FilterType::LOWPASS);
        lpf_1b.SetCutoff(fc1);
        lpf_1b.SetFilterType(FilterType::LOWPASS);
    }	
    {  // Middle band Filter Defs
        hpf_2a.SetCutoff(fc1);
        hpf_2a.SetFilterType(FilterType::HIGHPASS);
        hpf_2b.SetCutoff(fc1);
        hpf_2b.SetFilterType(FilterType::HIGHPASS);
        lpf_2a.SetCutoff(fc2);
        lpf_2a.SetFilterType(FilterType::LOWPASS);
        lpf_2b.SetCutoff(fc2);
        lpf_2b.SetFilterType(FilterType::LOWPASS);
    }
    {  // High band Filter Defs
        hpf_3a.SetCutoff(fc2);
        hpf_3a.SetFilterType(FilterType::HIGHPASS);
        hpf_3b.SetCutoff(fc2);
        hpf_3b.SetFilterType(FilterType::HIGHPASS);
    }

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        System::Delay(50);
        Controls();

    }
}

// slash r slash n does a clean newline
void PrintCoeffs(Butterworth filter){
    char buffer[2048];
    daisysp::FilterCoeffs coeffs = filter.GetCoefficients();
        test::sprintf_(buffer,"a0: %f  a1: %f  a2: %f  b0: %f  b1: %f  b2: %f\r\n"
                        "fc: %f omega_c: %f  K: %f  K2: %f FS: %f\r\n"
                        "b2_state: %i\r\n\r\n",
            coeffs.a0,
            coeffs.a1,
            coeffs.a2,
            coeffs.b0,
            coeffs.b1,
            coeffs.b2,
            filter.cutoff_,
            filter.omega_c,
            filter.K,
            filter.K2,
            filter.sample_rate_,
            b2_toogle_state);
            hw.usb_handle.TransmitInternal((uint8_t*)buffer, strlen(buffer));

}

// void PrintI2CInfo(){
//     char buffer[2048];
//     test::sprintf_(buffer,"SCL pin: %f  SDA pin: %f add: %X"
//     hw.usb_handle.TransmitInternal((uint8_t*)buffer, strlen(buffer));
// }

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
    //fc1 = fc1Param.Process();
    if (ConditionalParameter(oldk1, k1, fc1, fc1Param.Process())){
        lpf_1a.SetCutoff(fc1);
        lpf_1b.SetCutoff(fc1);
        hpf_2a.SetCutoff(fc1);
        hpf_2b.SetCutoff(fc1);
    };
    if (ConditionalParameter(oldk2, k2, fc2, fc2Param.Process())){
        lpf_2a.SetCutoff(fc2);
        lpf_2b.SetCutoff(fc2);
        hpf_3a.SetCutoff(fc2);
        hpf_3b.SetCutoff(fc2);
    };
    
    oldk1 = k1;    
    oldk2 = k2;    
}

void UpdateButtons()
{
    if(hw.button1.RisingEdge())
    {
        PrintCoeffs(lpf_1a);
    }

    if(hw.button2.RisingEdge())
    {
        b2_toogle_state += 1;
        b2_toogle_state = b2_toogle_state % b2_num_states;
        
        switch (b2_toogle_state)
        {
            case 0:
                PrintCoeffs(lpf_1a);
                break;
            case 1:
                PrintCoeffs(lpf_2a);
                System::Delay(70);
                PrintCoeffs(hpf_2a);
                break;
            case 2:
                PrintCoeffs(hpf_3a);
                break;
        }
    }
}
void UpdateLeds()
{
    float r = 0, g = 0, b = 0;
    switch (b2_toogle_state)
    {
        case 0:
            hw.led2.Set(1, 0, 0);
            break;
        case 1:
            hw.led2.Set(1, 1, 0);
            break;
        case 2:
            hw.led2.Set(0, 1, 0);
            break;
        case 3:
            hw.led2.Set(.7, .7, .7);
            break;
        default: break;
    }
    hw.UpdateLeds();
}

