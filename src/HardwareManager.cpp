#include "HardwareManager.h"

#define MOTOR_PWM_CHANNEL 0
#define MOTOR_PWM_FREQ 1000
#define MOTOR_PWM_RES 8

volatile long HardwareManager::_encoderCount = 0;
volatile long HardwareManager::_lastEncoderCount = 0;

void IRAM_ATTR HardwareManager::handleEncoderISR()
{
    int stateA = digitalRead(PIN_ENCODER_A);
    int stateB = digitalRead(PIN_ENCODER_B);
    if (stateA == stateB)
    {
        _encoderCount++;
    }
    else
    {
        _encoderCount--;
    }
}

HardwareManager::HardwareManager()
    : _display(128, 64, &Wire, -1),
      _strip(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800) {}

void HardwareManager::init()
{
    Serial.println("[Hardware] Initializing all hardware components...");

    _initButtons();
    _initMotor();
    _initEncoder();
    _initUltrasonic();
    _initNeoPixel();
    _initDisplay();

    Serial.println("[Hardware] Hardware initialization complete!");
}

void HardwareManager::_initButtons()
{
    pinMode(PIN_BTN_PREV, INPUT_PULLUP);
    pinMode(PIN_BTN_PLAY_PAUSE, INPUT_PULLUP);
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    Serial.println("[Hardware] Push Buttons initialized (Pins 17, 16, 4)");
}

void HardwareManager::_initMotor()
{
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    digitalWrite(PIN_MOTOR_IN2, LOW);

    ledcSetup(MOTOR_PWM_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
    ledcAttachPin(PIN_MOTOR_IN1, MOTOR_PWM_CHANNEL);
    ledcWrite(MOTOR_PWM_CHANNEL, 0);
    Serial.println("[Hardware] DRV8833 Motor initialized (Pins 19, 23)");
}

void HardwareManager::_initEncoder()
{
    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), handleEncoderISR, CHANGE);
    Serial.println("[Hardware] Rotary Encoder initialized (Pins 25, 26)");
}

void HardwareManager::_initUltrasonic()
{
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    Serial.printf("[Hardware] Ultrasonic sensor initialized (TRIG=%d, ECHO=%d)\n", PIN_ULTRASONIC_TRIG, PIN_ULTRASONIC_ECHO);
}

void HardwareManager::_initNeoPixel()
{
    _strip.begin();
    _strip.setBrightness(40);
    _strip.clear();
    // Quick startup LED sweep
    for (int i = 0; i < NUM_LEDS; i++)
    {
        _strip.setPixelColor(i, _strip.Color(0, 150, 255));
        _strip.show();
        delay(20);
    }
    _strip.clear();
    _strip.show();
    Serial.printf("[Hardware] WS2812B NeoPixel initialized (DATA=%d, %d LEDs)\n", PIN_NEOPIXEL, NUM_LEDS);
}

void HardwareManager::_initDisplay()
{
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setClock(400000); // 400kHz Fast I2C mode for smooth 25 FPS display
    if (_display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        _display.clearDisplay();
        _display.setTextWrap(false);
        _display.setTextColor(SSD1306_WHITE);
        _display.setTextSize(1);
        _display.setCursor(10, 15);
        _display.println("Spotify Stream Deck");
        _display.setCursor(20, 35);
        _display.println("Connecting...");
        _display.display();
        Serial.println("[Hardware] SSD1306 OLED Display initialized (0x3C)");
    }
    else
    {
        Serial.println("[Hardware] Warning: OLED Display not detected at 0x3C (Check wiring or address)");
    }
}

void HardwareManager::setMotorRunning(bool run)
{
    if (run && !_isScrubbing)
    {
        ledcWrite(MOTOR_PWM_CHANNEL, 130); // ~50% duty cycle for turntable rotation
    }
    else
    {
        ledcWrite(MOTOR_PWM_CHANNEL, 0);
    }
}

float HardwareManager::_measureDistanceCm()
{
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    long duration = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, 25000); // 25ms timeout (~4.3m)
    if (duration == 0)
        return -1.0f;
    return (duration * 0.0343f) / 2.0f;
}

ButtonEvent HardwareManager::readButtons()
{
    ButtonEvent event = BTN_NONE;
    unsigned long now = millis();

    // 1. Previous Button (Pin 17)
    int prevVal = digitalRead(PIN_BTN_PREV);
    if (prevVal != _lastPrevState && (now - _lastPrevDebounce > 50))
    {
        _lastPrevDebounce = now;
        _lastPrevState = prevVal;
        if (prevVal == LOW)
        {
            event = BTN_PREV_PRESSED;
        }
    }

    // 2. Play/Pause Button (Pin 16)
    int playVal = digitalRead(PIN_BTN_PLAY_PAUSE);
    if (playVal != _lastPlayPauseState && (now - _lastPlayPauseDebounce > 50))
    {
        _lastPlayPauseDebounce = now;
        _lastPlayPauseState = playVal;
        if (playVal == LOW)
        {
            event = BTN_PLAY_PAUSE_PRESSED;
        }
    }

    // 3. Next Button (Pin 4)
    int nextVal = digitalRead(PIN_BTN_NEXT);
    if (nextVal != _lastNextState && (now - _lastNextDebounce > 50))
    {
        _lastNextDebounce = now;
        _lastNextState = nextVal;
        if (nextVal == LOW)
        {
            event = BTN_NEXT_PRESSED;
        }
    }

    return event;
}

void HardwareManager::showNeoPixelVolume(int volumePercent)
{
    int numLedsLit = (volumePercent * NUM_LEDS) / 100;
    _strip.clear();
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (i < numLedsLit)
        {
            // Gradient from Green (low vol) -> Yellow (mid) -> Red (high vol)
            if (i < NUM_LEDS / 2)
            {
                _strip.setPixelColor(i, _strip.Color(0, 200, 0));
            }
            else if (i < (NUM_LEDS * 3) / 4)
            {
                _strip.setPixelColor(i, _strip.Color(200, 180, 0));
            }
            else
            {
                _strip.setPixelColor(i, _strip.Color(255, 30, 0));
            }
        }
    }
    _strip.show();
}

void HardwareManager::setNeoPixelStatus(bool isPlaying, bool isConnected)
{
    if (_volumeAdjusting)
        return; // Prioritize volume display

    static uint8_t breath = 50;
    static int8_t dir = 2;
    breath += dir;
    if (breath > 120 || breath < 30)
        dir = -dir;

    _strip.clear();
    if (!isConnected)
    {
        // Blue pulsing when not connected
        for (int i = 0; i < NUM_LEDS; i++)
        {
            _strip.setPixelColor(i, _strip.Color(0, 0, breath));
        }
    }
    else if (isPlaying)
    {
        // Spotify Green rotating dot / breathing
        static int dotPos = 0;
        static unsigned long lastDotMove = 0;
        if (millis() - lastDotMove > 80)
        {
            lastDotMove = millis();
            dotPos = (dotPos + 1) % NUM_LEDS;
        }
        for (int i = 0; i < NUM_LEDS; i++)
        {
            if (i == dotPos)
            {
                _strip.setPixelColor(i, _strip.Color(30, 255, 80));
            }
            else
            {
                _strip.setPixelColor(i, _strip.Color(5, 40, 10));
            }
        }
    }
    else
    {
        // Amber/Orange when paused
        for (int i = 0; i < NUM_LEDS; i++)
        {
            _strip.setPixelColor(i, _strip.Color(80, 40, 0));
        }
    }
    _strip.show();
}

void HardwareManager::_drawMarquee(int y, const String &text, unsigned long changeTime)
{
    if (text.length() == 0)
        return;

    _display.setTextWrap(false);
    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);

    // Each character is 6 pixels wide (5px font + 1px space)
    int textPixelWidth = text.length() * 6;

    // If text fits comfortably on 128px screen, draw statically
    if (textPixelWidth <= 126)
    {
        _display.setCursor(0, y);
        _display.print(text);
        return;
    }

    // Long text: Continuous loop with separator
    String loopText = text + "    -    ";
    int loopPixelWidth = loopText.length() * 6;

    unsigned long elapsed = millis() - changeTime;
    int pixelOffset = 0;

    // Pause for 1500 ms when new song starts so user can read first part
    if (elapsed > 1500)
    {
        pixelOffset = ((elapsed - 1500) / 35) % loopPixelWidth; // 1 pixel every 35ms (~28 px/s)
    }

    int x1 = -pixelOffset;
    _display.setCursor(x1, y);
    _display.print(loopText);

    // If gap appears on right, draw seamless second copy
    if (x1 + loopPixelWidth < 128)
    {
        _display.setCursor(x1 + loopPixelWidth, y);
        _display.print(loopText);
    }
}

void HardwareManager::updateDisplay(const SpotifyTrack &track, const char *statusMsg)
{
    _display.clearDisplay();
    _display.setTextWrap(false);

    // Top status line
    _display.setTextSize(1);
    _display.setCursor(0, 0);
    if (statusMsg != nullptr)
    {
        _display.print(statusMsg);
    }
    else
    {
        _display.print(track.isPlaying ? "PLAYING [>]" : "PAUSED  [||]");
        _display.setCursor(85, 0);
        _display.printf("Vol:%d%%", _currentVolume);
    }

    _display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Track Title change detection to restart pause
    if (track.title != _lastTrackTitle)
    {
        _lastTrackTitle = track.title;
        _trackChangeTime = millis();
    }

    // Track Title (Line 2) with smooth Marquee scrolling
    String titleDisplay = track.title.length() > 0 ? track.title : "No Track Loaded";
    _drawMarquee(15, titleDisplay, _trackChangeTime);

    // Artist Name (Line 3) with smooth Marquee scrolling
    String artistDisplay = track.artist.length() > 0 ? track.artist : "Spotify Idle";
    _drawMarquee(28, artistDisplay, _trackChangeTime);

    // Progress bar (Line 4)
    int barY = 43;
    _display.drawRect(0, barY, 128, 6, SSD1306_WHITE);
    if (track.durationMs > 0)
    {
        int progressWidth = (track.progressMs * 124) / track.durationMs;
        if (progressWidth > 124)
            progressWidth = 124;
        _display.fillRect(2, barY + 2, progressWidth, 2, SSD1306_WHITE);
    }

    // Time text (Line 5)
    _display.setCursor(0, 54);
    uint32_t pSec = track.progressMs / 1000;
    uint32_t dSec = track.durationMs / 1000;
    _display.printf("%02u:%02u / %02u:%02u", pSec / 60, pSec % 60, dSec / 60, dSec % 60);

    _display.display();
}

void HardwareManager::drawVolumeOverlay(int volumePercent, float distanceCm)
{
    _display.clearDisplay();
    _display.setTextWrap(false);

    // Top Header
    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);
    _display.setCursor(18, 0);
    _display.println("VOLUME CONTROL");
    _display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Big Volume Number in center
    _display.setTextSize(2);
    _display.setCursor(42, 16);
    _display.printf("%3d%%", volumePercent);

    // Progress Bar Frame
    _display.drawRect(8, 36, 112, 12, SSD1306_WHITE);
    int barWidth = (volumePercent * 108) / 100;
    if (barWidth > 108)
        barWidth = 108;
    if (barWidth > 0)
    {
        _display.fillRect(10, 38, barWidth, 8, SSD1306_WHITE);
    }

    // Distance in cm
    _display.setTextSize(1);
    _display.setCursor(16, 52);
    _display.printf("Distance: %.1f cm", distanceCm);

    _display.display();
}

void HardwareManager::_postCommand(SpotifyCmdType type, int32_t value)
{
    if (_commandQueue != NULL)
    {
        SpotifyCommand cmd = {type, value};
        xQueueSend(_commandQueue, &cmd, 0); // Non-blocking, instant 0ms!
    }
}

void HardwareManager::update(SpotifyTrack &currentTrack)
{
    unsigned long now = millis();

    // 1. Check Button Events with INSTANT visual feedback & FreeRTOS queue dispatch
    ButtonEvent btn = readButtons();
    if (btn == BTN_PREV_PRESSED)
    {
        Serial.println("[Button] PREV pressed -> Queuing CMD_PREV");
        SoundEffects::playTone(440, 80, 0.7f);
        updateDisplay(currentTrack, "[<<] Previous...");
        _postCommand(CMD_PREV);
    }
    else if (btn == BTN_PLAY_PAUSE_PRESSED)
    {
        Serial.println("[Button] PLAY/PAUSE pressed");
        SoundEffects::playTone(660, 80, 0.7f);
        if (currentTrack.isPlaying)
        {
            currentTrack.isPlaying = false;
            setMotorRunning(false);
            updateDisplay(currentTrack, "[||] Paused");
            _postCommand(CMD_PAUSE);
        }
        else
        {
            currentTrack.isPlaying = true;
            setMotorRunning(true);
            updateDisplay(currentTrack, "[>] Playing...");
            _postCommand(CMD_PLAY);
        }
    }
    else if (btn == BTN_NEXT_PRESSED)
    {
        Serial.println("[Button] NEXT pressed -> Queuing CMD_NEXT");
        SoundEffects::playTone(880, 80, 0.7f);
        updateDisplay(currentTrack, "[>>] Next Track...");
        _postCommand(CMD_NEXT);
    }

    // 2. Turntable Scrubbing (Rotary Encoder + Motor + I2S Scratch SFX)
    long encoderDiff = _encoderCount - _lastProcessedEncoder;
    if (encoderDiff != 0)
    {
        _lastProcessedEncoder = _encoderCount;

        if (!_isScrubbing)
        {
            _isScrubbing = true;
            _wasPlayingBeforeScrub = currentTrack.isPlaying;
            Serial.println("[Turntable] Manual rotation detected -> Pausing Spotify & Cutting Motor PWM");
            setMotorRunning(false); // Stop motor immediately (stall protection)
            if (_wasPlayingBeforeScrub)
            {
                _postCommand(CMD_PAUSE); // Pause Spotify playback while touching/scrubbing
            }
            _scrubAccumulatedMs = 0;
        }

        _lastScrubTime = now;
        // Play synthetic vinyl scratch sound on I2S
        SoundEffects::playVinylScratch((int)encoderDiff);

        // Accumulate seek position (approx 600 ms per encoder tick)
        _scrubAccumulatedMs += (encoderDiff * 600);
        Serial.printf("[Turntable] Scratching: delta=%ld, accumulated offset=%ld ms\n", encoderDiff, _scrubAccumulatedMs);
    }

    // If user finished scratching (no new encoder movements for 350 ms)
    if (_isScrubbing && (now - _lastScrubTime > 350))
    {
        _isScrubbing = false;
        long targetMs = (long)currentTrack.progressMs + _scrubAccumulatedMs;
        if (targetMs < 0)
            targetMs = 0;
        if (currentTrack.durationMs > 0 && targetMs > (long)currentTrack.durationMs)
        {
            targetMs = currentTrack.durationMs;
        }

        Serial.printf("[Turntable] Scrub complete! Queuing seek to %ld ms...\n", targetMs);
        _postCommand(CMD_SEEK, (int32_t)targetMs);
        currentTrack.progressMs = (uint32_t)targetMs;

        if (_wasPlayingBeforeScrub)
        {
            _postCommand(CMD_PLAY);
            currentTrack.isPlaying = true;
            setMotorRunning(true);
        }
        _scrubAccumulatedMs = 0;
    }

    // 3. Ultrasonic Distance (EMA Low-Pass Filter + Live OLED Screen + I2S Tone SFX)
    if (now - _lastUltrasonicPoll > 35) // Fast sampling (28 Hz) for silky smooth animation
    {
        _lastUltrasonicPoll = now;
        float rawDist = _measureDistanceCm();

        static unsigned long lastUltraDebug = 0;
        if (now - lastUltraDebug > 1500)
        {
            lastUltraDebug = now;
            if (rawDist > 0)
            {
                Serial.printf("[Ultrasonic] Raw: %.1f cm | Filtered: %.1f cm (Target Vol: %d%%)\n",
                              rawDist, _filteredDistance, _targetVolume);
            }
            else
            {
                Serial.printf("[Ultrasonic] No echo (Timeout). Check TRIG(Pin %d), ECHO(Pin %d) & 5V VCC\n",
                              PIN_ULTRASONIC_TRIG, PIN_ULTRASONIC_ECHO);
            }
        }

        // Active hand distance range: 4 cm to 35 cm
        if (rawDist >= 4.0f && rawDist <= 35.0f)
        {
            // Apply Exponential Moving Average (EMA) low-pass filter to eliminate jitter
            if (!_hasInitialDistance)
            {
                _filteredDistance = rawDist;
                _hasInitialDistance = true;
            }
            else
            {
                const float alpha = 0.35f; // Responsive yet silky smooth glide
                _filteredDistance = (alpha * rawDist) + ((1.0f - alpha) * _filteredDistance);
            }

            // Map smoothly from 5cm (0%) to 30cm (100%)
            int vol = map((long)(_filteredDistance + 0.5f), 5, 30, 0, 100);
            vol = constrain(vol, 0, 100);

            _targetVolume = vol;
            _volumeAdjusting = true;
            _lastVolumeChangeTime = now;

            // Live OLED Volume Overlay update smoothly at high frame rate!
            drawVolumeOverlay(_targetVolume, _filteredDistance);

            // Play acoustic feedback tone corresponding to volume level
            if (now - _lastVolumeToneTime > 120)
            {
                _lastVolumeToneTime = now;
                SoundEffects::playVolumeTone(_targetVolume);
            }

            showNeoPixelVolume(_targetVolume);
            _currentVolume = _targetVolume;
        }
        else
        {
            // Hand moved out of range
            _hasInitialDistance = false;
        }
    }

    // When hand stops or is removed, send volume to Spotify after 200ms cooldown (fast response!)
    if (_volumeAdjusting && (now - _lastVolumeChangeTime > 200))
    {
        _volumeAdjusting = false;
        if (_lastSentVolume != _targetVolume)
        {
            _lastSentVolume = _targetVolume;
            _currentVolume = _targetVolume;
            Serial.printf("[Ultrasonic] Volume stabilized at %d%% -> Queuing CMD_SET_VOLUME (Fast 200ms)\n", _currentVolume);
            _postCommand(CMD_SET_VOLUME, _currentVolume);
        }
        updateDisplay(currentTrack); // Return to standard track screen
    }

    // 4. Update LEDs and Display (when not in volume adjustment mode)
    if (!_volumeAdjusting)
    {
        if (now - _lastLedUpdate > 50)
        {
            _lastLedUpdate = now;
            setNeoPixelStatus(currentTrack.isPlaying, WiFi.status() == WL_CONNECTED);
        }

        if (now - _lastDisplayUpdate > 45) // ~22 FPS for smooth text marquee animation
        {
            _lastDisplayUpdate = now;
            updateDisplay(currentTrack);
        }
    }
}
