#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct SpotifyTrack
{
    bool hasTrack = false;   // true if a track is loaded/active
    bool isPlaying = false;  // true if actively playing, false if paused
    String title;            // Song title
    String artist;           // Artist name(s)
    String album;            // Album title
    String trackUri;         // Spotify track URI
    String albumArtUrl;      // Smallest/Thumbnail album artwork URL
    uint32_t progressMs = 0; // Current playback position in ms
    uint32_t durationMs = 0; // Total track duration in ms
};

enum SpotifyCmdType
{
    CMD_NONE = 0,
    CMD_PLAY,
    CMD_PAUSE,
    CMD_TOGGLE_PLAY,
    CMD_NEXT,
    CMD_PREV,
    CMD_SET_VOLUME,
    CMD_SEEK
};

struct SpotifyCommand
{
    SpotifyCmdType type;
    int32_t value; // e.g. volume (0..100) or seek (positionMs)
};

class SpotifyClient
{
public:
    SpotifyClient(const char *clientId, const char *clientSecret, const char *refreshToken);
    ~SpotifyClient();

    // Initial check and fetch of access token
    bool begin();

    // Refresh access token using the stored refresh_token
    bool refreshAccessToken();

    // Fetch the currently playing track from Spotify
    // Returns:
    //  1 : Successfully retrieved track info (HTTP 200)
    //  0 : Nothing playing / player idle (HTTP 204)
    // -1 : Network or API error
    int getCurrentlyPlaying(SpotifyTrack &track);

    // Playback control functions
    bool play();
    bool pause();
    bool nextTrack();
    bool previousTrack();
    bool setVolume(int volumePercent);
    bool seek(uint32_t positionMs);

    // Check if access token is valid and not expired
    bool isTokenValid() const;

    // Get current access token
    String getAccessToken() const { return _accessToken; }

private:
    const char *_clientId;
    const char *_clientSecret;
    const char *_refreshToken;

    String _accessToken;
    unsigned long _tokenExpiresAt = 0; // millis() timestamp when token expires

    // HTTP Keep-Alive Persistent Connection for zero-delay player commands
    WiFiClientSecure _apiClient;
    HTTPClient _apiHttp;

    String _encodeBasicAuth(const char *id, const char *secret);
    bool _sendPlayerCommand(const char *endpoint, const char *method, const String &payload = "");
};
