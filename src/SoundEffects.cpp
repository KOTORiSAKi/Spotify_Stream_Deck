#include "SoundEffects.h"
#include <math.h>

#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 22050

void SoundEffects::init()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 128,
        .use_apll = false,
        .tx_desc_auto_clear = true};

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRC,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE};

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err == ESP_OK)
    {
        i2s_set_pin(I2S_PORT, &pin_config);
        i2s_zero_dma_buffer(I2S_PORT);
        Serial.printf("[SoundEffects] MAX98357A I2S DAC initialized! (LRC=%d, BCLK=%d, DOUT=%d)\n",
                      PIN_I2S_LRC, PIN_I2S_BCLK, PIN_I2S_DOUT);
    }
    else
    {
        Serial.printf("[SoundEffects] Failed to install I2S driver: %d\n", err);
    }
}

void SoundEffects::playTone(int freqHz, int durationMs, float amplitude)
{
    if (freqHz <= 0 || durationMs <= 0)
        return;
    // Serial.printf("[Audio] Playing tone: %d Hz (%d ms)\n", freqHz, durationMs);
    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    int16_t buffer[256];
    size_t bytesWritten = 0;

    int samplesRemaining = numSamples;
    int sampleIndex = 0;

    while (samplesRemaining > 0)
    {
        int chunk = (samplesRemaining > 128) ? 128 : samplesRemaining;
        for (int i = 0; i < chunk; i++)
        {
            float phase = (float)sampleIndex / (float)SAMPLE_RATE;
            // Anti-pop envelope
            float env = 1.0f;
            if (sampleIndex < 80)
            {
                env = (float)sampleIndex / 80.0f;
            }
            else if (samplesRemaining < 80)
            {
                env = (float)samplesRemaining / 80.0f;
            }

            float val = sinf(2.0f * M_PI * freqHz * phase) * amplitude * env;
            int16_t sample = (int16_t)(val * 32767.0f);
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;

            sampleIndex++;
            samplesRemaining--;
        }
        i2s_write(I2S_PORT, buffer, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
}

void SoundEffects::playVolumeTone(int volumePercent)
{
    if (volumePercent < 0)
        volumePercent = 0;
    if (volumePercent > 100)
        volumePercent = 100;
    // Scale tone from 320 Hz to 1100 Hz
    int freq = 320 + (volumePercent * 780) / 100;
    float amp = 0.25f + ((float)volumePercent / 100.0f) * 0.35f;
    playTone(freq, 45, amp);
}

void SoundEffects::playVinylScratch(int deltaPulses)
{
    int durationMs = 35;
    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    int16_t buffer[256];
    size_t bytesWritten = 0;

    int samplesRemaining = numSamples;
    int sampleIndex = 0;
    float baseFreq = (deltaPulses >= 0) ? 140.0f : 100.0f;

    while (samplesRemaining > 0)
    {
        int chunk = (samplesRemaining > 128) ? 128 : samplesRemaining;
        for (int i = 0; i < chunk; i++)
        {
            float phase = (float)sampleIndex / (float)SAMPLE_RATE;
            float env = 1.0f - ((float)sampleIndex / (float)numSamples);

            // Friction friction tone + crackle
            float sweepFreq = baseFreq * (1.0f + 1.8f * sinf(2.5f * M_PI * phase));
            float tone = sinf(2.0f * M_PI * sweepFreq * phase) * 0.45f;
            float crackle = ((float)(rand() % 2000 - 1000) / 1000.0f) * 0.4f;

            float combined = (tone + crackle) * env * 0.5f;
            int16_t sample = (int16_t)(combined * 32767.0f);

            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;

            sampleIndex++;
            samplesRemaining--;
        }
        i2s_write(I2S_PORT, buffer, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
}
