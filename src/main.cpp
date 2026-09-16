#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "pin_config.h"
#include "SpotifyClient.h"
#include "SoundEffects.h"
#include "HardwareManager.h"

SpotifyClient spotify(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN);
HardwareManager hardware;
SpotifyTrack currentTrack;

// FreeRTOS Queues and Mutexes for Dual-Core execution
QueueHandle_t spotifyQueue = NULL;
SemaphoreHandle_t trackMutex = NULL;

unsigned long lastLocalProgressTime = 0;
bool spotifyInitialized = false;

const char *getWiFiStatusName(wl_status_t status)
{
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID_AVAILABLE (Check 2.4GHz)";
    case WL_SCAN_COMPLETED:
        return "SCAN_COMPLETED";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED (Incorrect password)";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

void connectWiFi()
{
    Serial.println();
    Serial.printf("[WiFi] Connecting to '%s' ...\n", WIFI_SSID);

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(500);
        Serial.print(".");
        attempts++;
        if (attempts % 20 == 0)
        {
            Serial.println();
        }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("[WiFi] Connected successfully!");
        Serial.print("[WiFi] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("[WiFi] Signal Strength (RSSI): %d dBm\n", WiFi.RSSI());

        // Configure Google DNS fallback
        IPAddress dns(8, 8, 8, 8);
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns);
    }
    else
    {
        Serial.printf("[WiFi] Connection failed! Status: %s\n", getWiFiStatusName(WiFi.status()));
        Serial.println("[WiFi] Note: ESP32 only supports 2.4GHz Wi-Fi.");
    }
}

// Background Worker Task pinned to Core 0 (Handles Spotify Web API & Polling)
void spotifyWorkerTask(void *pvParameters)
{
    Serial.println("[FreeRTOS] Spotify Worker Task running on Core 0");

    unsigned long lastBackgroundPoll = 0;

    while (true)
    {
        // 1. Process incoming commands from UI / Sensors
        SpotifyCommand cmd;
        if (xQueueReceive(spotifyQueue, &cmd, pdMS_TO_TICKS(80)) == pdTRUE)
        {
            Serial.printf("[Worker Core 0] Received command type: %d, value: %d\n", cmd.type, cmd.value);
            bool needRefetch = false;

            switch (cmd.type)
            {
            case CMD_PREV:
                spotify.previousTrack();
                needRefetch = true;
                break;
            case CMD_NEXT:
                spotify.nextTrack();
                needRefetch = true;
                break;
            case CMD_PLAY:
                spotify.play();
                needRefetch = true;
                break;
            case CMD_PAUSE:
                spotify.pause();
                needRefetch = true;
                break;
            case CMD_TOGGLE_PLAY:
                if (currentTrack.isPlaying)
                {
                    spotify.pause();
                }
                else
                {
                    spotify.play();
                }
                needRefetch = true;
                break;
            case CMD_SET_VOLUME:
                spotify.setVolume(cmd.value);
                break;
            case CMD_SEEK:
                spotify.seek((uint32_t)cmd.value);
                break;
            default:
                break;
            }

            if (needRefetch)
            {
                vTaskDelay(pdMS_TO_TICKS(350)); // Allow Spotify backend to advance track
                SpotifyTrack freshTrack;
                if (spotify.getCurrentlyPlaying(freshTrack) == 1)
                {
                    if (xSemaphoreTake(trackMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                    {
                        currentTrack = freshTrack;
                        xSemaphoreGive(trackMutex);
                        hardware.setMotorRunning(currentTrack.isPlaying);
                        hardware.updateDisplay(currentTrack);
                    }
                }
                lastBackgroundPoll = millis();
            }
        }

        // 2. Periodic background poll to keep track data in sync
        unsigned long now = millis();
        if (spotifyInitialized && (now - lastBackgroundPoll >= SPOTIFY_POLL_INTERVAL_MS))
        {
            lastBackgroundPoll = now;

            SpotifyTrack freshTrack;
            int status = spotify.getCurrentlyPlaying(freshTrack);

            if (status == 1)
            {
                if (xSemaphoreTake(trackMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    currentTrack = freshTrack;
                    xSemaphoreGive(trackMutex);
                    hardware.setMotorRunning(currentTrack.isPlaying);
                }
            }
            else if (status == 0)
            {
                if (xSemaphoreTake(trackMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    currentTrack.hasTrack = false;
                    currentTrack.isPlaying = false;
                    xSemaphoreGive(trackMutex);
                    hardware.setMotorRunning(false);
                }
            }
        }

        // Keep watchdog and FreeRTOS happy
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("  ESP32 Spotify Stream Deck - Dual-Core FreeRTOS  ");
    Serial.println("==================================================");

    // 1. Initialize Sound Effects (I2S Audio DAC MAX98357A)
    SoundEffects::init();

    // 2. Initialize Hardware Peripherals (Buttons, Ultrasonic, Motor, Encoder, NeoPixel, OLED)
    hardware.init();

    // 3. Create FreeRTOS Queue & Mutex
    spotifyQueue = xQueueCreate(10, sizeof(SpotifyCommand));
    trackMutex = xSemaphoreCreateMutex();
    hardware.setCommandQueue(spotifyQueue);

    // Play startup chime
    SoundEffects::playTone(523, 80, 0.5f); // C5
    delay(30);
    SoundEffects::playTone(659, 80, 0.5f); // E5
    delay(30);
    SoundEffects::playTone(784, 120, 0.6f); // G5

    // 4. Connect to Wi-Fi
    connectWiFi();

    // 5. Authenticate with Spotify Web API
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0))
    {
        Serial.println("[Setup] Authenticating with Spotify API...");
        if (spotify.begin())
        {
            spotifyInitialized = true;
            Serial.println("[Setup] Spotify authentication successful! Fetching initial state...");
            spotify.getCurrentlyPlaying(currentTrack);
            hardware.setMotorRunning(currentTrack.isPlaying);
            hardware.updateDisplay(currentTrack);
        }
        else
        {
            Serial.println("[Setup] Spotify authentication failed! Verify credentials in include/config.h");
        }
    }

    // 6. Launch Spotify Worker Task on Core 0 (Separated from Core 1 UI loop)
    xTaskCreatePinnedToCore(
        spotifyWorkerTask,
        "SpotifyWorker",
        8192,
        NULL,
        1,
        NULL,
        0 // Pin to Core 0!
    );
}

void loop()
{
    // Core 1 runs this loop exclusively for real-time UI, Sensors, and Audio SFX!
    // No HTTPS delays will ever block this loop!

    // Check Wi-Fi reconnection
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0))
    {
        static unsigned long lastWifiRetry = 0;
        if (millis() - lastWifiRetry > 5000)
        {
            lastWifiRetry = millis();
            Serial.printf("[WiFi] Waiting for Wi-Fi... Current status: %s\n", getWiFiStatusName(WiFi.status()));
            WiFi.reconnect();
        }
    }
    else if (!spotifyInitialized)
    {
        Serial.println("[Setup] Wi-Fi ready! Authenticating with Spotify API...");
        if (spotify.begin())
        {
            spotifyInitialized = true;
            Serial.println("[Setup] Spotify connected!");
        }
    }

    // Update real-time hardware: Ultrasonic EMA filter, OLED volume overlay, buttons, turntable
    hardware.update(currentTrack);

    // Local Progress Interpolation: advance progressMs smoothly while music is playing
    unsigned long now = millis();
    if (currentTrack.isPlaying)
    {
        if (lastLocalProgressTime == 0)
            lastLocalProgressTime = now;
        unsigned long elapsed = now - lastLocalProgressTime;
        if (elapsed >= 500)
        {
            currentTrack.progressMs += elapsed;
            lastLocalProgressTime = now;
            if (currentTrack.durationMs > 0 && currentTrack.progressMs > currentTrack.durationMs)
            {
                currentTrack.progressMs = currentTrack.durationMs;
            }
        }
    }
    else
    {
        lastLocalProgressTime = now;
    }

    delay(5); // Yield CPU to keep Core 1 responsive
}