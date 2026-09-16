#pragma once

// ================= PIN MAPPING CONFIGURATION =================
// 1. OLED Display (I2C)
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22

// 2. Ultrasonic Sensor
#define PIN_ULTRASONIC_TRIG 18 // Changed from 5 to 18 (Safe Output)
#define PIN_ULTRASONIC_ECHO 34 // Changed from 18 to 34 (Input Only pin)

// 3. Push Buttons (Active LOW - Use INPUT_PULLUP)
#define PIN_BTN_PREV 17
#define PIN_BTN_PLAY_PAUSE 16
#define PIN_BTN_NEXT 4

// 4. DRV8833 Motor Driver (PWM)
#define PIN_MOTOR_IN1 19
#define PIN_MOTOR_IN2 23

// 5. N20 Rotary Encoder
#define PIN_ENCODER_A 25
#define PIN_ENCODER_B 26

// 6. WS2812B RGB LED
#define PIN_NEOPIXEL 13 // Changed from 15 to 13 (Safe Output, avoids MTDO conflict)
#define NUM_LEDS 15     // Number of LEDs in strip

// 7. MAX98357A I2S Audio DAC
#define PIN_I2S_LRC 27  // LR Clock / Word Select (WS)
#define PIN_I2S_BCLK 32 // Bit Clock (BCLK / SCK)
#define PIN_I2S_DOUT 33 // Data In (DIN / SD)
