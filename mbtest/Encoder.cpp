#include "Encoder.h"
#define __BSD_VISIBLE 1
#include <math.h>
#include "daisy_pod.h"
#include "daisysp.h"
#include "Utility/dsp.h"
#include <cmath>
#include <algorithm>

using namespace daisysp;
using namespace daisy;

void daisy::Encoder::Init(I2CHandle::Config i2c_config, uint8_t i2c_address)
{
    // i2c_config_.periph = I2CHandle::Config::Peripheral::I2C_1;
    // i2c_config_.speed  = I2CHandle::Config::Speed::I2C_1MHZ;
    // i2c_config_.mode   = I2CHandle::Config::Mode::I2C_SLAVE;
    // i2c_config_.address = address;
    // i2c_conf.pin_config.scl  = {DSY_GPIOB, 8};
    // i2c_conf.pin_config.sda  = {DSY_GPIOB, 9};
    i2cHandle_.Init(i2c_config);
    i2c_address_ = i2c_address;
}

int daisy::Encoder::GetDelta(uint8_t encoder)
{
    // uint8_t buf[4];
    // this->read(SEESAW_ENCODER_BASE, SEESAW_ENCODER_DELTA + encoder, buf, 4);
    // int32_t ret = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
    //                 ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];

    // return ret;

    // Result daisy::I2CHandle::ReadDataAtAddress 	( 	uint16_t  	address,
	// 	uint16_t  	mem_address,
	// 	uint16_t  	mem_address_size,
	// 	uint8_t *  	data,
	// 	uint16_t  	data_size,
	// 	uint32_t  	timeout 
	// ) 	
    uint8_t buf[4];
    i2cHandle_.TransmitBlocking(i2c_address_, SEESAW_ENCODER_DELTA + &encoder, 1, 1000);  // Send register address
    i2cHandle_.ReceiveBlocking(i2c_address_, buf, 2, 1000);   // Receive the delta data
    int32_t ret = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];

    return ret;
}
