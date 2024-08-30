#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "daisy_pod.h"
#include "daisysp.h"
#include "butterworth.h"
#include "sbcompressor.h"
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
Parameter inGain_param, outGain_param;
Parameter attack_param, release_param;
Parameter ratio_param, thresh_param;
float oldk1, oldk2, k1, k2;
int b2_toogle_state;
int b2_num_states;

SSD130xI2CTransport::Config oledTransConfig;
OledDisplay<SSD130xI2c128x64Driver>::Config display_cfg;
OledDisplay<SSD130xI2c128x64Driver> display;
#define DISPLAY_HEIGTH 64
#define DISPLAY_WIDTH 128


// Audio vars
float sample_rate;

// Component Vars
Butterworth lpf_1a,lpf_1b;
Butterworth hpf_2a,hpf_2b, lpf_2a,lpf_2b;
Butterworth hpf_3a,hpf_3b;
FilterType filterType;
float fc1,fc2;
float inGain, outGain;
float atk, rls;
float ratio, thresh;

SBCompressor comp1;

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
        out[0][i] = comp1.ProcessFull(in[0][i]);
        out[1][i] = comp1.ApplyAllGain(in[1][i]);        
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
    b2_num_states = 3;  //0 is in out gain, 1 is atk rls, 2 is fc, 3 is ratio thresh 
    // USB stuff
    hw.usb_handle.Init(UsbHandle::FS_INTERNAL);
    // I2C stuff
    display_cfg.driver_config.transport_config.i2c_address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_1MHZ;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.scl = seed::D11;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.sda = seed::D12;

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

    inGain_param.Init(hw.knob1,-40.0,40.0,Parameter::LINEAR);
    outGain_param.Init(hw.knob2,-40.0,40.0,Parameter::LINEAR);
    attack_param.Init(hw.knob1,0.001,.3,Parameter::LINEAR);
    release_param.Init(hw.knob2,0.001,1.0,Parameter::LINEAR);
    ratio_param.Init(hw.knob2,1.0,12.0,Parameter::LINEAR);
    thresh_param.Init(hw.knob2,-60.0,0.0,Parameter::LINEAR);


    // set up butterworth filter DO NOT FORGET THIS lol
    {
	lpf_1a.Init(sample_rate);
    lpf_1b.Init(sample_rate);
    hpf_2a.Init(sample_rate);
    hpf_2b.Init(sample_rate);
    lpf_2a.Init(sample_rate);
    lpf_2b.Init(sample_rate);
    hpf_3a.Init(sample_rate);
    hpf_3b.Init(sample_rate);
    }
    //bqtest.Init(sample_rate);
	fc1 = 200.0;
    fc2 = 2000.0;
    inGain, outGain = 0.0f;
    atk, rls = 0.0f;
    ratio, thresh = 0.0f;
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

    comp1.Init(sample_rate);
    comp1.SetLevelFollowerType(LevelFollowerType::PEAK);
    comp1.SetAttackSec(.1);
    comp1.SetReleaseSec(.3);
    

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    /** Clear the display */
    display.Fill(false);
    display.Update();

    FixedCapStr<32> str("");
    
    str = "";
    display.Fill(false);
    display.Update(); // Update the display to show the changes
    System::Delay(500);
    while(1)
    {
        System::Delay(10);
        display.Fill(false);
        display.SetCursor(4, 4);
        str = "RMS : ";
        str.AppendFloat(SBCompressor::linear2dB(comp1.xRMS_),5);
        display.WriteString(str, Font_6x8, true);
        display.SetCursor(4, 4+(10*1));
        str = "Peak: ";
        str.AppendFloat(SBCompressor::linear2dB(comp1.xPeak_),5);
        display.WriteString(str, Font_6x8, true);
        //0 is in out gain, 1 is atk rls, 2 is ratio thresh 3 is fc,
        switch(b2_toogle_state){
        case 0:
            display.SetCursor(4, 4+(10*2));
            str = "inGain : ";
            str.AppendFloat(inGain,2);
            display.WriteString(str, Font_6x8, true);
            display.SetCursor(4, 4+(10*3));
            str = "outGain: ";
            str.AppendFloat(outGain,2);
            display.WriteString(str, Font_6x8, true);
            display.Update();
            break;
        case 1:
            display.SetCursor(4, 4+(10*2));
            str = "atk: ";
            str.AppendFloat(atk,2);
            display.WriteString(str, Font_6x8, true);
            display.SetCursor(4, 4+(10*3));
            str = "rls: ";
            str.AppendFloat(rls,2);
            display.WriteString(str, Font_6x8, true);
            display.Update();
            break;
        case 2:
            display.SetCursor(4, 4+(10*2));
            str = "ratio : ";
            str.AppendFloat(ratio,2);
            display.WriteString(str, Font_6x8, true);
            display.SetCursor(4, 4+(10*3));
            str = "thresh: ";
            str.AppendFloat(thresh,2);
            display.WriteString(str, Font_6x8, true);
            display.Update();
            break;
        case 3:
            display.SetCursor(4, 4+(10*2));
            str = "fc1 : ";
            str.AppendFloat(fc1,2);
            display.WriteString(str, Font_6x8, true);
            display.SetCursor(4, 4+(10*3));
            str = "fc2: ";
            str.AppendFloat(fc2,2);
            display.WriteString(str, Font_6x8, true);
            display.Update();
            break;
        }
        
        //levelHistoryArr[currentIndex] = Random::GetFloat(0,3*(DISPLAY_HEIGTH/4));
        //levelHistoryArr[currentIndex] = comp1.xRMS_;
        //display.Fill(false); // Clear the display
        // for(int i = 0; i<DISPLAY_WIDTH; i++){
        //     System::Delay(5);
        //     display.DrawLine(i,0,i,DISPLAY_HEIGTH,draw);
        //     display.Update(); 
        // }
        // draw = !draw;
        // currentIndex = (currentIndex+1) % DISPLAY_WIDTH;
        

        // display.SetCursor(4, 4);
        // str = "";
        // str.AppendFloat(float(comp1.xRMS_),5);
        // display.WriteString(str, Font_6x8, true);
         // Update the display to show the changes
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

void PrintCompState(SBCompressor compressor){
    char buffer[2048];
    test::sprintf_(buffer,"TS: %f  thresh: %f  ratio: %f  atk: %f  rls: %f  slope: %f\r\n"
                        "tLin: %f xRMS_: %f  xPeak_: %f  xPeak_m1_: %f RMS_SF: %f xSqr: %f\r\n\n",
            compressor.sample_period_,
            compressor.thresh_dB_, 
            compressor.ratio_, 
            compressor.attack_sec_, 
            compressor.release_sec_,
            compressor.at_,
            compressor.rt_,
            compressor.slope_,
            compressor.thresh_lin_,
            compressor.xRMS_, 
            compressor.xPeak_, 
            compressor.rms_smoothing_factor_,
            compressor.xSqr_m1_);
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
    //fc1 = fc1Param.Process();
    switch (b2_toogle_state)
    {
        //0 is in out gain, 1 is atk rls, 2 is ratio thresh 3 is fc,
        case 0:
            
            if (ConditionalParameter(oldk1, k1, inGain, inGain_param.Process())){
                comp1.SetInputGainLin(inGain);
            }
            if (ConditionalParameter(oldk2, k2, outGain, outGain_param.Process())){
                comp1.SetInputGainLin(outGain);
            }
            break;
        case 1:
            
            if (ConditionalParameter(oldk1, k1, atk, attack_param.Process())){
                comp1.SetInputGainLin(atk);
            }
            if (ConditionalParameter(oldk2, k2, rls, release_param.Process())){
                comp1.SetInputGainLin(rls);
            }
            break;
        case 2:
            
            if (ConditionalParameter(oldk1, k1, ratio, ratio_param.Process())){
                comp1.SetRatio(ratio);
            }
            if (ConditionalParameter(oldk2, k2, thresh, thresh_param.Process())){
                comp1.SetThresholdDB(thresh);
            }
            break;
        case 3:
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
            break;
    }
    // if (ConditionalParameter(oldk1, k1, fc1, fc1Param.Process())){
    //     lpf_1a.SetCutoff(fc1);
    //     lpf_1b.SetCutoff(fc1);
    //     hpf_2a.SetCutoff(fc1);
    //     hpf_2b.SetCutoff(fc1);
    // };
    // if (ConditionalParameter(oldk2, k2, fc2, fc2Param.Process())){
    //     lpf_2a.SetCutoff(fc2);
    //     lpf_2b.SetCutoff(fc2);
    //     hpf_3a.SetCutoff(fc2);
    //     hpf_3b.SetCutoff(fc2);
    // };
    
    oldk1 = k1;    
    oldk2 = k2;    
}

void UpdateButtons()
{
    if(hw.button1.RisingEdge())
    {
        PrintCompState(comp1);
    }

    if(hw.button2.RisingEdge())
    {
        b2_toogle_state += 1;
        b2_toogle_state = b2_toogle_state % b2_num_states;
        
        // switch (b2_toogle_state)
        // {
        //     case 0:
        //         PrintCoeffs(lpf_1a);
        //         break;
        //     case 1:
        //         PrintCoeffs(lpf_2a);
        //         System::Delay(70);
        //         PrintCoeffs(hpf_2a);
        //         break;
        //     case 2:
        //         PrintCoeffs(hpf_3a);
        //         break;
        // }
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
            hw.led2.Set(0, 1, 1);
            break;
        default: break;
    }
    hw.UpdateLeds();
}

