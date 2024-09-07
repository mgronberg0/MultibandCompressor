#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "daisy_pod.h"
#include "daisysp.h"
#include "dev\oled_ssd130x.h"

using namespace daisy;
using namespace daisysp;

DaisyPod hw;

SSD130xI2CTransport::Config oledTransConfig;
OledDisplay<SSD130xI2c128x64Driver>::Config display_cfg;
OledDisplay<SSD130xI2c128x64Driver> display;
#define DISPLAY_HEIGTH 64
#define DISPLAY_WIDTH 128

enum Slashes{
	FRWD  = 0,  // '/'
	BKWD  = 1,  // '\'
	EMTY  = 2,	// '-'
	FLEX  = 3	// 'O'
};

		// '000/\0000/\000000'
		// '00/-/\00/\-\/\000'
		// '0/\--/\/\---\/\00'
		// '/--/--/-/\-\/-/\0'
		// '\-/-\/\/\-\/\---\'
		// '0\--/--\-\/--\/-/'
		// '00\/\-\-\/\-\-\/0'
		// '00000\-\/00\--/00'
		// '000000\/0000\/000'
const int logo_W = 17;
const int logo_H = 9;
Slashes benthosLogo[logo_H][logo_W] = {
        {FLEX, FLEX, FLEX, FRWD, BKWD, FLEX, FLEX, FLEX, FLEX, FRWD, BKWD, EMTY, EMTY, EMTY, EMTY, EMTY, EMTY},
        {FLEX, FLEX, FRWD, EMTY, BKWD, FLEX, FLEX, FRWD, BKWD, EMTY, BKWD, FRWD, BKWD, EMTY, EMTY, EMTY, EMTY},
        {EMTY, FRWD, EMTY, EMTY, BKWD, EMTY, BKWD, FRWD, EMTY, EMTY, EMTY, BKWD, FRWD, EMTY, EMTY, EMTY, EMTY},
        {FRWD, EMTY, EMTY, FRWD, EMTY, EMTY, FRWD, EMTY, BKWD, EMTY, FRWD, EMTY, BKWD, FRWD, EMTY, EMTY, EMTY},
        {BKWD, EMTY, FRWD, EMTY, BKWD, FRWD, EMTY, BKWD, FRWD, EMTY, BKWD, FRWD, EMTY, EMTY, EMTY, EMTY, BKWD},
        {EMTY, BKWD, EMTY, EMTY, FRWD, EMTY, EMTY, BKWD, FRWD, EMTY, EMTY, FRWD, EMTY, EMTY, BKWD, FRWD, EMTY},
        {EMTY, EMTY, FRWD, BKWD, EMTY, EMTY, FRWD, BKWD, EMTY, EMTY, FRWD, EMTY, EMTY, FRWD, BKWD, EMTY, EMTY},
        {EMTY, EMTY, EMTY, EMTY, EMTY, BKWD, FRWD, EMTY, EMTY, EMTY, FRWD, EMTY, EMTY, BKWD, FRWD, EMTY, EMTY},
        {EMTY, EMTY, EMTY, EMTY, EMTY, EMTY, EMTY, FRWD, EMTY, EMTY, EMTY, EMTY, FRWD, EMTY, EMTY, EMTY, EMTY}
    };


void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
	hw.ProcessAllControls();
	for (size_t i = 0; i < size; i++)
	{
		out[0][i] = in[0][i];
		out[1][i] = in[1][i];
	}
}

void DrawSlash(int xPos, int yPos, int ForB);

int main(void)
{
	hw.Init();
	hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	hw.StartAdc();
	hw.StartAudio(AudioCallback);

	hw.usb_handle.Init(UsbHandle::FS_INTERNAL);
    // I2C stuff
    display_cfg.driver_config.transport_config.i2c_address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.address = 0x3D;
    display_cfg.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_1MHZ;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.scl = seed::D11;
    display_cfg.driver_config.transport_config.i2c_config.pin_config.sda = seed::D12;

    display.Init(display_cfg);

    display.Fill(false);
	int forward_or_backward = 1;
	int maze[DISPLAY_WIDTH/4][DISPLAY_HEIGTH/4]; 
	FixedCapStr<32> str("");
	for(int y = 0; y<DISPLAY_HEIGTH/4;y++){
		for(int x = 0; x<DISPLAY_WIDTH/4; x++)
		{
			if ((x>5 && (x<logo_W+5))&&(y>5 && y<(logo_H+5))){
				maze[x][y] = (int)benthosLogo[x][y];
				DrawSlash(x,y,(int)benthosLogo[x][y]);
			}
			else{
				forward_or_backward = (bool)round(Random::GetFloat(0.0,1.0));
			//forward_or_backward = y%2;
			maze[x][y] = forward_or_backward;
			DrawSlash(x,y,forward_or_backward);
			}
         
		}
	}
	display.Update();
	// while(1) {
	// 	for(int i = 0; i<10;i++){
	// 		int x = round(Random::GetFloat(0.0,15.0));
	// 		int y = round(Random::GetFloat(0.0,31.0));
	// 		maze[x][y] =  !maze[x][y];
	// 		DrawSlash(x,y,maze[x][y]);
	// 	}
	// 	display.Update();

	// }
}



void DrawSlash(int xPos, int yPos, int ForB){
	// Displays a 4x4 pixel slash at the desired location, considering the display space
	// as a 32 x 16 subdisplay, hence max x is 15 and max y is 31
	// ForB = 1, forward slash '/'  ForB == 0, backslash '\'
	// Erase the previous content by drawing a filled rectangle with the background color
    //display.DrawRect(xPos * 4, yPos * 4, 4, 4, false);
	int x = xPos*4;
	int y = yPos*4;
	switch (ForB){
		case Slashes::FRWD:
			display.DrawPixel(x+3,y+0,false);
			display.DrawPixel(x+2,y+1,false);
			display.DrawPixel(x+1,y+2,false);
			display.DrawPixel(x+0,y+3,false);
			display.DrawPixel(x+0,y+0,true);
			display.DrawPixel(x+1,y+1,true);
			display.DrawPixel(x+2,y+2,true);
			display.DrawPixel(x+3,y+3,true);
			break;
		case Slashes::BKWD:
			display.DrawPixel(x+0,y+0,false);
			display.DrawPixel(x+1,y+1,false);
			display.DrawPixel(x+2,y+2,false);
			display.DrawPixel(x+3,y+3,false);
			display.DrawPixel(x+3,y+0,true);
			display.DrawPixel(x+2,y+1,true);
			display.DrawPixel(x+1,y+2,true);
			display.DrawPixel(x+0,y+3,true);
			break;
		case Slashes:::
			display.DrawPixel(x+3,y+0,false);
			display.DrawPixel(x+2,y+1,false);
			display.DrawPixel(x+1,y+2,false);
			display.DrawPixel(x+0,y+3,false);
			display.DrawPixel(x+0,y+0,true);
			display.DrawPixel(x+1,y+1,true);
			display.DrawPixel(x+2,y+2,true);
			display.DrawPixel(x+3,y+3,true);
			break;
		case Slashes::BKWD:
			display.DrawPixel(x+0,y+0,false);
			display.DrawPixel(x+1,y+1,false);
			display.DrawPixel(x+2,y+2,false);
			display.DrawPixel(x+3,y+3,false);
			display.DrawPixel(x+3,y+0,true);
			display.DrawPixel(x+2,y+1,true);
			display.DrawPixel(x+1,y+2,true);
			display.DrawPixel(x+0,y+3,true);
			break;	
	}
}