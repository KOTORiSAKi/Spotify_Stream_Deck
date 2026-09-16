#include "SpotifyClient.h"
#include <mbedtls/base64.h>

SpotifyClient::SpotifyClient(const char *clientId, const char *clientSecret, const char *refreshToken)
    : _clientId(clientId), _clientSecret(clientSecret), _refreshToken(refreshToken)
{
    _apiClient.setInsecure();
    _apiHttp.setReuse(true);
    _apiHttp.setTimeout(4000); // Fast 4-second timeout for snappy response
}

SpotifyClient::~SpotifyClient()
{
    _apiHttp.end();
    _apiClient.stop();
}

String SpotifyClient::_encodeBasicAuth(const char *id, const char *secret)
{
    String creds = String(id) + ":" + String(secret);
    size_t outputLength = 0;
    mbedtls_base64_encode(nullptr, 0, &outputLength, (const unsigned char *)creds.c_str(), creds.length());

    unsigned char *output = new unsigned char[outputLength + 1];
    mbedtls_base64_encode(output, outputLength + 1, &outputLength, (const unsigned char *)creds.c_str(), creds.length());
    output[outputLength] = '\0';

    String encoded = String((char *)output);
    delete[] output;
    return encoded;
}

bool SpotifyClient::isTokenValid() const
{
    if (_accessToken.length() == 0)
    {
        return false;
    }
    // Check if within expiration time
    return (long)(_tokenExpiresAt - millis()) > 0;
}

bool SpotifyClient::begin()
{
    return refreshAccessToken();
}

bool SpotifyClient::refreshAccessToken()
{
    Serial.println("[Spotify] Refreshing Access Token...");

    if (String(_clientId).indexOf("YOUR_") >= 0 || String(_refreshToken).indexOf("YOUR_") >= 0)
    {
        Serial.println("[Spotify] ERROR: Credentials not configured! Please update include/config.h");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate validation for simplicity

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(client, "https://accounts.spotify.com/api/token"))
    {
        Serial.println("[Spotify] Failed to connect to Spotify Accounts endpoint");
        return false;
    }

    String basicAuth = _encodeBasicAuth(_clientId, _clientSecret);
    http.addHeader("Authorization", "Basic " + basicAuth);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String requestBody = "grant_type=refresh_token&refresh_token=" + String(_refreshToken);

    int httpCode = http.POST(requestBody);

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err)
        {
            Serial.printf("[Spotify] Failed to parse token response JSON: %s\n", err.c_str());
            http.end();
            return false;
        }

        _accessToken = doc["access_token"].as<String>();
        long expiresIn = doc["expires_in"] | 3600;

        // Refresh 2 minutes before it expires
        unsigned long bufferMs = 120000UL;
        _tokenExpiresAt = millis() + (expiresIn * 1000UL > bufferMs ? (expiresIn * 1000UL - bufferMs) : (expiresIn * 1000UL));

        Serial.printf("[Spotify] Access Token refreshed successfully! (Expires in %ld seconds)\n", expiresIn);
        http.end();
        client.stop();
        return true;
    }
    else
    {
        Serial.printf("[Spotify] Token refresh failed! HTTP Code: %d\n", httpCode);
        if (httpCode > 0)
        {
            String errResponse = http.getString();
            Serial.printf("[Spotify] Response: %s\n", errResponse.c_str());
        }
        http.end();
        client.stop();
        return false;
    }
}

int SpotifyClient::getCurrentlyPlaying(SpotifyTrack &track)
{
    if (!isTokenValid())
    {
        if (!refreshAccessToken())
        {
            return -1;
        }
    }

    String url = "https://api.spotify.com/v1/me/player/currently-playing";

    // Try up to 2 attempts: attempt 0 reuses existing keep-alive socket; if expired, attempt 1 reconnects cleanly
    for (int attempt = 0; attempt < 2; attempt++)
    {
        _apiHttp.begin(_apiClient, url);
        _apiHttp.addHeader("Authorization", "Bearer " + _accessToken);
        _apiHttp.addHeader("Connection", "keep-alive");

        int httpCode = _apiHttp.GET();

        if (httpCode == HTTP_CODE_OK)
        { // 200 OK
            String payload = _apiHttp.getString();
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);

            if (err)
            {
                Serial.printf("[Spotify] JSON deserialize error: %s\n", err.c_str());
                _apiHttp.end();
                return -1;
            }

            track.hasTrack = true;
            track.isPlaying = doc["is_playing"] | false;
            track.progressMs = doc["progress_ms"] | 0;

            JsonObject item = doc["item"];
            if (!item.isNull())
            {
                track.title = item["name"] | "Unknown Title";
                track.durationMs = item["duration_ms"] | 0;
                track.trackUri = item["uri"] | "";

                // Join artists
                JsonArray artists = item["artists"];
                String artistStr = "";
                for (size_t i = 0; i < artists.size(); i++)
                {
                    if (i > 0)
                        artistStr += ", ";
                    artistStr += artists[i]["name"] | "";
                }
                track.artist = artistStr;

                // Album
                JsonObject album = item["album"];
                if (!album.isNull())
                {
                    track.album = album["name"] | "Unknown Album";
                    JsonArray images = album["images"];
                    if (images.size() > 0)
                    {
                        track.albumArtUrl = images[images.size() - 1]["url"] | "";
                    }
                }
            }

            _apiHttp.end(); // Retains TCP/TLS socket for reuse
            return 1;
        }
        else if (httpCode == 204)
        { // 204 No Content - Spotify is open but inactive or nothing playing
            track.hasTrack = false;
            track.isPlaying = false;
            _apiHttp.end();
            return 0;
        }
        else if (httpCode == 401)
        { // 401 Unauthorized - Token expired
            Serial.println("[Spotify] Received HTTP 401 Unauthorized. Retrying with new token...");
            _accessToken = "";
            _apiHttp.end();
            _apiClient.stop();
            if (refreshAccessToken())
            {
                continue; // Retry once with new token
            }
            return -1;
        }
        else if (httpCode < 0 && attempt == 0)
        {
            // Keep-alive socket closed by server during idle, reconnect and retry immediately
            _apiHttp.end();
            _apiClient.stop();
            continue;
        }
        else
        {
            Serial.printf("[Spotify] HTTP Error: %d\n", httpCode);
            _apiHttp.end();
            _apiClient.stop();
            return -1;
        }
    }
    return -1;
}

bool SpotifyClient::_sendPlayerCommand(const char *endpoint, const char *method, const String &payload)
{
    if (!isTokenValid())
    {
        if (!refreshAccessToken())
        {
            return false;
        }
    }

    String url = "https://api.spotify.com/v1/me/player/" + String(endpoint);

    // Fast-path: Reuses existing open TLS connection. Fallback: Reconnects if idle-timed out.
    for (int attempt = 0; attempt < 2; attempt++)
    {
        _apiHttp.begin(_apiClient, url);
        _apiHttp.addHeader("Authorization", "Bearer " + _accessToken);
        _apiHttp.addHeader("Connection", "keep-alive");
        if (payload.length() > 0)
        {
            _apiHttp.addHeader("Content-Type", "application/json");
            _apiHttp.addHeader("Content-Length", String(payload.length()));
        }
        else
        {
            _apiHttp.addHeader("Content-Length", "0");
        }

        int httpCode = 0;
        if (strcmp(method, "PUT") == 0)
        {
            httpCode = (payload.length() > 0) ? _apiHttp.PUT(payload) : _apiHttp.PUT((uint8_t *)NULL, 0);
        }
        else if (strcmp(method, "POST") == 0)
        {
            httpCode = (payload.length() > 0) ? _apiHttp.POST(payload) : _apiHttp.POST((uint8_t *)NULL, 0);
        }

        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NO_CONTENT)
        {
            Serial.printf("[Spotify API] Command %s %s SUCCESS (HTTP %d, Fast Keep-Alive)\n", method, endpoint, httpCode);
            _apiHttp.end(); // Socket remains connected!
            return true;
        }
        else if (httpCode == 403)
        {
            String errResponse = _apiHttp.getString();
            if (errResponse.indexOf("Restriction violated") >= 0)
            {
                Serial.printf("[Spotify API] Player already in requested state (%s %s)\n", method, endpoint);
                _apiHttp.end();
                return true; // Already in desired state
            }
            Serial.printf("[Spotify API] Command %s %s forbidden (HTTP 403): %s\n", method, endpoint, errResponse.c_str());
            _apiHttp.end();
            return false;
        }
        else if (httpCode == 401)
        {
            Serial.println("[Spotify] Token expired during command, refreshing...");
            _accessToken = "";
            _apiHttp.end();
            _apiClient.stop();
            if (refreshAccessToken())
            {
                continue;
            }
            return false;
        }
        else if (httpCode < 0 && attempt == 0)
        {
            // Server closed idle socket, reconnect cleanly and retry
            _apiHttp.end();
            _apiClient.stop();
            continue;
        }
        else
        {
            Serial.printf("[Spotify] Command %s %s failed (HTTP %d)\n", method, endpoint, httpCode);
            if (httpCode > 0)
            {
                String errResponse = _apiHttp.getString();
                Serial.printf("[Spotify] Response: %s\n", errResponse.c_str());
            }
            _apiHttp.end();
            _apiClient.stop();
            return false;
        }
    }
    return false;
}

bool SpotifyClient::play()
{
    Serial.println("[Spotify API] Sending Play command");
    return _sendPlayerCommand("play", "PUT");
}

bool SpotifyClient::pause()
{
    Serial.println("[Spotify API] Sending Pause command");
    return _sendPlayerCommand("pause", "PUT");
}

bool SpotifyClient::nextTrack()
{
    Serial.println("[Spotify API] Sending Next command");
    return _sendPlayerCommand("next", "POST");
}

bool SpotifyClient::previousTrack()
{
    Serial.println("[Spotify API] Sending Previous command");
    return _sendPlayerCommand("previous", "POST");
}

bool SpotifyClient::setVolume(int volumePercent)
{
    if (volumePercent < 0)
        volumePercent = 0;
    if (volumePercent > 100)
        volumePercent = 100;
    Serial.printf("[Spotify API] Setting Volume to %d%%\n", volumePercent);
    String endpoint = "volume?volume_percent=" + String(volumePercent);
    return _sendPlayerCommand(endpoint.c_str(), "PUT");
}

bool SpotifyClient::seek(uint32_t positionMs)
{
    Serial.printf("[Spotify API] Seeking to %u ms\n", positionMs);
    String endpoint = "seek?position_ms=" + String(positionMs);
    return _sendPlayerCommand(endpoint.c_str(), "PUT");
}
