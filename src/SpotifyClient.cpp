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

#include <JPEGDEC.h>

struct ColorAccumulator
{
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    uint32_t count = 0;

    uint32_t fallback_r = 0;
    uint32_t fallback_g = 0;
    uint32_t fallback_b = 0;
    uint32_t fallback_count = 0;
};

static ColorAccumulator *g_acc = nullptr;

int JPEGDrawCallback(JPEGDRAW *pDraw)
{
    if (!g_acc)
        return 1;

    int numPixels = pDraw->iWidth * pDraw->iHeight;
    for (int i = 0; i < numPixels; i++)
    {
        uint16_t color = pDraw->pPixels[i];
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;

        uint8_t maxC = r;
        if (g > maxC)
            maxC = g;
        if (b > maxC)
            maxC = b;

        uint8_t minC = r;
        if (g < minC)
            minC = g;
        if (b < minC)
            minC = b;

        // Is the pixel colorful? (difference between highest and lowest RGB channel is large enough)
        bool isVibrant = (maxC - minC > 35) && (maxC > 50);

        if (isVibrant)
        {
            g_acc->r += r;
            g_acc->g += g;
            g_acc->b += b;
            g_acc->count++;
        }

        // Keep a fallback of non-dark pixels in case the album is totally grayscale
        if (maxC > 40)
        {
            g_acc->fallback_r += r;
            g_acc->fallback_g += g;
            g_acc->fallback_b += b;
            g_acc->fallback_count++;
        }
    }
    return 1;
}

static uint8_t *g_jpegBuffer = nullptr;
static WiFiClientSecure *g_imageClient = nullptr;
static HTTPClient *g_imageHttp = nullptr;

uint32_t SpotifyClient::getAverageAlbumColor(const String &url)
{
    if (url.length() == 0)
        return 0xFFFFFF; // Default white

    // Allocate statically ONCE to completely eliminate heap fragmentation
    if (!g_jpegBuffer)
    {
        g_jpegBuffer = (uint8_t *)malloc(40000);
        g_imageClient = new WiFiClientSecure();
        if (g_imageClient)
            g_imageClient->setInsecure();
        g_imageHttp = new HTTPClient();
        
        if (!g_jpegBuffer || !g_imageClient || !g_imageHttp)
        {
            Serial.println("[Spotify] Fatal: Could not allocate static image buffers!");
            return 0xFFFFFF;
        }
    }

    g_imageHttp->begin(*g_imageClient, url);
    int httpCode = g_imageHttp->GET();
    uint32_t finalColor = 0xFFFFFF; // Default white

    if (httpCode == HTTP_CODE_OK)
    {
        int len = g_imageHttp->getSize();
        if (len > 0 && len <= 40000)
        {
            WiFiClient *stream = g_imageHttp->getStreamPtr();
            if (stream)
            {
                size_t bytesRead = 0;
                unsigned long startWait = millis();
                while (g_imageHttp->connected() && bytesRead < len)
                {
                    size_t available = stream->available();
                    if (available)
                    {
                        size_t toRead = len - bytesRead;
                        if (available < toRead)
                            toRead = available;
                        int c = stream->readBytes(g_jpegBuffer + bytesRead, toRead);
                        if (c > 0)
                        {
                            bytesRead += c;
                            startWait = millis();
                        }
                    }
                    else
                    {
                        if (millis() - startWait > 3000)
                        {
                            Serial.println("[Spotify] Image download timeout!");
                            break;
                        }
                    }
                    delay(1);
                }

                if (bytesRead > 0)
                {
                    ColorAccumulator acc;
                    g_acc = &acc;
                    JPEGDEC *jpeg = new JPEGDEC();
                    if (jpeg)
                    {
                        if (jpeg->openRAM(g_jpegBuffer, bytesRead, JPEGDrawCallback))
                        {
                            jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
                            jpeg->decode(0, 0, 0); // Decode entire image without scaling
                        }
                        delete jpeg;

                        if (acc.count > 0)
                        {
                            uint8_t avgR = acc.r / acc.count;
                            uint8_t avgG = acc.g / acc.count;
                            uint8_t avgB = acc.b / acc.count;

                            // Boost saturation/brightness to make the color pop on NeoPixels
                            uint8_t aMax = avgR;
                            if (avgG > aMax)
                                aMax = avgG;
                            if (avgB > aMax)
                                aMax = avgB;

                            if (aMax > 0 && aMax < 255)
                            {
                                // Scale up so the brightest channel is close to 255
                                float scale = 255.0f / aMax;
                                // Soften the scale slightly so it's not overly blown out
                                scale = 1.0f + (scale - 1.0f) * 0.8f;
                                avgR = (uint8_t)(avgR * scale > 255 ? 255 : avgR * scale);
                                avgG = (uint8_t)(avgG * scale > 255 ? 255 : avgG * scale);
                                avgB = (uint8_t)(avgB * scale > 255 ? 255 : avgB * scale);
                            }

                            finalColor = ((uint32_t)avgR << 16) | ((uint32_t)avgG << 8) | avgB;
                        }
                        else if (acc.fallback_count > 0)
                        {
                            // If album is grayscale/black & white, fallback to just averaging bright pixels
                            uint8_t avgR = acc.fallback_r / acc.fallback_count;
                            uint8_t avgG = acc.fallback_g / acc.fallback_count;
                            uint8_t avgB = acc.fallback_b / acc.fallback_count;
                            finalColor = ((uint32_t)avgR << 16) | ((uint32_t)avgG << 8) | avgB;
                        }
                        else
                        {
                            finalColor = 0xFFFFFF; // Absolute fallback
                        }
                    }
                    g_acc = nullptr;
                }
            }
        }
        else
        {
            Serial.println("[Spotify] Image too large or invalid length!");
        }
    }
    
    g_imageHttp->end();
    return finalColor;
}

