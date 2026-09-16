#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include "pin_config.h"
#include "SpotifyClient.h"
#include "SoundEffects.h"

enum ButtonEvent
{
    BTN_NONE = 0,
    BTN_PREV_PRESSED,
    BTN_PLAY_PAUSE_PRESSED,
    BTN_NEXT_PRESSED
};

class HardwareManager
{
public:
    HardwareManager();
    void init();

    // Set FreeRTOS queue for asynchronous zero-latency commands
    void setCommandQueue(QueueHandle_t queue) { _commandQueue = queue; }

    // Update all hardware components (called in Core 1 loop - non-blocking)
    void update(SpotifyTrack &currentTrack);

    // Callbacks / status updates
    void updateDisplay(const SpotifyTrack &track, const char *statusMsg = nullptr);
    void drawVolumeOverlay(int volumePercent, float distanceCm);
    void setNeoPixelStatus(bool isPlaying, bool isConnected);
    void showNeoPixelVolume(int volumePercent);

    // Motor control
    void setMotorRunning(bool run);

    // Check button presses
    ButtonEvent readButtons();

    // Encoder interrupt handler
    static void handleEncoderISR();

private:
    Adafruit_SSD1306 _display;
    Adafruit_NeoPixel _strip;
    QueueHandle_t _commandQueue = NULL;

    // Button states
    bool _lastPrevState = HIGH;
    bool _lastPlayPauseState = HIGH;
    bool _lastNextState = HIGH;
    unsigned long _lastPrevDebounce = 0;
    unsigned long _lastPlayPauseDebounce = 0;
    unsigned long _lastNextDebounce = 0;

    // Ultrasonic states & Exponential Moving Average (EMA) filter
    unsigned long _lastUltrasonicPoll = 0;
    float _filteredDistance = 0.0f;
    bool _hasInitialDistance = false;
    int _currentVolume = 50;
    int _targetVolume = 50;
    int _lastSentVolume = -1;
    bool _volumeAdjusting = false;
    unsigned long _lastVolumeChangeTime = 0;
    unsigned long _lastVolumeToneTime = 0;

    // Turntable / Scrubbing states
    static volatile long _encoderCount;
    static volatile long _lastEncoderCount;
    long _lastProcessedEncoder = 0;
    bool _isScrubbing = false;
    unsigned long _lastScrubTime = 0;
    long _scrubAccumulatedMs = 0;
    bool _wasPlayingBeforeScrub = false;

    // Display update throttle
    unsigned long _lastDisplayUpdate = 0;
    unsigned long _lastLedUpdate = 0;

    // Marquee scrolling states
    String _lastTrackTitle = "";
    unsigned long _trackChangeTime = 0;

    void _initButtons();
    void _initMotor();
    void _initEncoder();
    void _initUltrasonic();
    void _initNeoPixel();
    void _initDisplay();

    float _measureDistanceCm();
    void _postCommand(SpotifyCmdType type, int32_t value = 0);
    void _drawMarquee(int y, const String &text, unsigned long changeTime);
};
