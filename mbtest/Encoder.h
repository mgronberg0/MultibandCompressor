#pragma once
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <i2c.h>
#ifdef __cplusplus

#define SEESAW_ADDRESS (0x49) ///< Default Seesaw I2C address
enum {
  SEESAW_STATUS_BASE = 0x00,
  SEESAW_GPIO_BASE = 0x01,
  SEESAW_SERCOM0_BASE = 0x02,

  SEESAW_TIMER_BASE = 0x08,
  SEESAW_ADC_BASE = 0x09,
  SEESAW_DAC_BASE = 0x0A,
  SEESAW_INTERRUPT_BASE = 0x0B,
  SEESAW_DAP_BASE = 0x0C,
  SEESAW_EEPROM_BASE = 0x0D,
  SEESAW_NEOPIXEL_BASE = 0x0E,
  SEESAW_TOUCH_BASE = 0x0F,
  SEESAW_KEYPAD_BASE = 0x10,
  //SEESAW_ENCODER_BASE = 0x11,
  SEESAW_ENCODER_BASE = 0x49,
  SEESAW_SPECTRUM_BASE = 0x12,
  SEESAW_SOIL_BASE = 0x13,
};
/** encoder module edge definitions
 */
enum {
  SEESAW_ENCODER_STATUS = 0x00,
  SEESAW_ENCODER_INTENSET = 0x10,
  SEESAW_ENCODER_INTENCLR = 0x20,
  SEESAW_ENCODER_POSITION = 0x30,
  SEESAW_ENCODER_DELTA = 0x40,
};

namespace daisy
{   
// Class for interfacing with Adafruit NeoRotary4 Encoder
class Encoder
{
    public:
    Encoder() {}
    ~Encoder() {}

    void Init(I2CHandle::Config i2c_config,uint8_t i2c_address);
    int GetDelta(uint8_t encoder);
    int GetPosition(uint8_t encoder);
    int SetPosition(uint8_t encoder);
    int EnableInterrupt();
    int DisableIntterupt();

    private:
    I2CHandle i2cHandle_;
    uint8_t i2c_address_;
}

} // namespace daisysp
#endif
#endif