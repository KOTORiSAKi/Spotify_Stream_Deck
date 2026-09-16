"""
Spotify Refresh Token Generator for ESP32 Spotify Stream Deck
This script uses only Python standard libraries (no pip dependencies required).

Usage:
    python tools/get_spotify_token.py
"""

import base64
import http.server
import json
import os
import sys
import urllib.parse
import urllib.request
import webbrowser

PORT = 8888
REDIRECT_URI = f"http://127.0.0.1:{PORT}/callback"
SCOPES = "user-read-playback-state user-read-currently-playing user-modify-playback-state"

auth_code = None


class OAuthCallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global auth_code
        parsed_path = urllib.parse.urlparse(self.path)
        query_params = urllib.parse.parse_qs(parsed_path.query)

        if parsed_path.path == "/callback":
            if "code" in query_params:
                auth_code = query_params["code"][0]
                self.send_response(200)
                self.send_header("Content-type", "text/html; charset=utf-8")
                self.end_headers()
                html = """
                <html>
                <head><title>Spotify Authorization Success</title></head>
                <body style="font-family: Arial, sans-serif; text-align: center; padding-top: 50px; background-color: #121212; color: #1DB954;">
                    <h1>Authorized Successfully!</h1>
                    <p style="color: #FFFFFF;">You can close this tab and return to your terminal.</p>
                </body>
                </html>
                """
                self.wfile.write(html.encode("utf-8"))
            elif "error" in query_params:
                error = query_params["error"][0]
                self.send_response(400)
                self.send_header("Content-type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(f"Authorization Failed: {error}".encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        # Suppress default server log output
        return


def main():
    print("=" * 60)
    print(" Spotify OAuth 2.0 Token Helper")
    print("=" * 60)
    print(f"Make sure you added '{REDIRECT_URI}' to 'Redirect URIs'")
    print("in your Spotify Developer Dashboard App settings!\n")

    # Read from existing include/config.h if available
    default_client_id = ""
    default_client_secret = ""
    config_path = os.path.join(os.path.dirname(__file__), "..", "include", "config.h")

    if os.path.exists(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                for line in f:
                    if "SPOTIFY_CLIENT_ID" in line and '"' in line:
                        val = line.split('"')[1]
                        if val != "YOUR_SPOTIFY_CLIENT_ID":
                            default_client_id = val
                    if "SPOTIFY_CLIENT_SECRET" in line and '"' in line:
                        val = line.split('"')[1]
                        if val != "YOUR_SPOTIFY_CLIENT_SECRET":
                            default_client_secret = val
        except Exception:
            pass

    client_id = input(f"Enter Spotify Client ID [{default_client_id}]: ").strip() or default_client_id
    client_secret = input(f"Enter Spotify Client Secret [{default_client_secret}]: ").strip() or default_client_secret

    if not client_id or not client_secret:
        print("\nError: Client ID and Client Secret are required!")
        sys.exit(1)

    auth_params = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
    }
    auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(auth_params)

    server = http.server.HTTPServer(("127.0.0.1", PORT), OAuthCallbackHandler)
    print(f"\n1. Opening browser for authorization:\n   {auth_url}\n")
    webbrowser.open(auth_url)

    print(f"2. Waiting for callback on {REDIRECT_URI} ...")
    while not auth_code:
        server.handle_request()

    server.server_close()
    print("3. Received authorization code! Exchanging for tokens...")

    # Exchange authorization code for refresh token
    token_url = "https://accounts.spotify.com/api/token"
    auth_header_val = base64.b64encode(f"{client_id}:{client_secret}".encode("utf-8")).decode("utf-8")

    req_data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": auth_code,
        "redirect_uri": REDIRECT_URI,
    }).encode("utf-8")

    req = urllib.request.Request(token_url, data=req_data, method="POST")
    req.add_header("Authorization", f"Basic {auth_header_val}")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")

    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            refresh_token = data.get("refresh_token")
            access_token = data.get("access_token")

            print("\n" + "=" * 60)
            print(" Authorization SUCCESSFUL!")
            print("=" * 60)
            print(f"\nREFRESH_TOKEN:\n{refresh_token}\n")

            if os.path.exists(config_path):
                save_choice = input("Would you like to automatically update include/config.h? (y/n): ").strip().lower()
                if save_choice == "y":
                    with open(config_path, "r", encoding="utf-8") as f:
                        content = f.read()

                    # Update CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN
                    import re
                    content = re.sub(r'#define\s+SPOTIFY_CLIENT_ID\s+".*?"', f'#define SPOTIFY_CLIENT_ID     "{client_id}"', content)
                    content = re.sub(r'#define\s+SPOTIFY_CLIENT_SECRET\s+".*?"', f'#define SPOTIFY_CLIENT_SECRET "{client_secret}"', content)
                    content = re.sub(r'#define\s+SPOTIFY_REFRESH_TOKEN\s+".*?"', f'#define SPOTIFY_REFRESH_TOKEN "{refresh_token}"', content)

                    with open(config_path, "w", encoding="utf-8") as f:
                        f.write(content)
                    print("[OK] include/config.h has been updated successfully!")
                else:
                    print("Please manually copy the REFRESH_TOKEN above into include/config.h")
            else:
                print("Please copy the REFRESH_TOKEN into include/config.h")

    except urllib.error.HTTPError as e:
        err_msg = e.read().decode("utf-8")
        print(f"\n[ERROR] Token exchange failed (HTTP {e.code}): {err_msg}")
    except Exception as e:
        print(f"\n[ERROR] An error occurred: {e}")


if __name__ == "__main__":
    main()

