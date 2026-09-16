#pragma once

#include <Arduino.h>
#include "driver/i2s.h"
#include "pin_config.h"

class SoundEffects
{
public:
    static void init();

    // Play vinyl record scratch / needle drag sound (called when turntable encoder moves)
    static void playVinylScratch(int deltaPulses);

    // Play feedback tone corresponding to volume level (300Hz - 1200Hz)
    static void playVolumeTone(int volumePercent);

    // General purpose tone
    static void playTone(int freqHz, int durationMs, float amplitude = 0.5f);
};
