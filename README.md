# 🎵 ESP32 Spotify Stream Deck

ระบบควบคุมและแสดงผล Spotify แบบตั้งโต๊ะ (Stream Deck) ด้วยไมโครคอนโทรลเลอร์ **ESP32 (DOIT DevKit V1)** เชื่อมต่อโดยตรงกับ **Spotify Web API** ผ่าน Wi-Fi มาพร้อมหน้าจอ OLED แสดงข้อมูลเพลงแบบ Marquee, เซนเซอร์ Ultrasonic ปรับเสียงไร้สัมผัส, ไฟสถานะ NeoPixel, จานหมุนแผ่นเสียงจำลอง (Turntable) และระบบเสียงเอฟเฟกต์สังเคราะห์ผ่าน I2S DAC

---

## 🌟 ฟีเจอร์เด่น (Key Features)

1. **Dual-Core FreeRTOS Architecture:**
   - **Core 0:** จัดการคำขอ HTTPS และเชื่อมต่อกับ Spotify Web API เบื้องหลัง
   - **Core 1:** ขับเคลื่อนการแสดงผลหน้าจอ OLED, สแกนปุ่มกด, เซนเซอร์ Ultrasonic และเสียงเอฟเฟกต์แบบ Real-time (0 ms Latency)
2. **High-Speed HTTP Keep-Alive:**
   - ใช้การเชื่อมต่อแบบ Persistent TLS Connection ไม่ตัดและต่อ Socket ใหม่ทุกครั้ง ทำให้คำสั่งเปลี่ยนเพลง/ปรับเสียงตอบสนองเร็วขึ้นจาก 2–3 วินาที เหลือเพียง **~200 – 350 ms**
3. **Smooth Marquee Scrolling (SSD1306 OLED):**
   - แสดงชื่อเพลงและศิลปินที่ยาวเกิน 128 พิกเซลแบบเลื่อนต่อเนื่องวนลูป (Marquee) พร้อมตัดคำตกบรรทัด ปรับสปีด I2C 400kHz (~22–25 FPS)
4. **Touchless Hand Gesture Volume (Ultrasonic Sensor):**
   - ปรับระดับเสียง 0–100% เพียงเลื่อนมือเข้า-ออกที่ระยะ 5 – 30 ซม.
   - มี **Exponential Moving Average (EMA Low-Pass Filter)** กรองสัญญาณรบกวน ทำให้แถบเสียงบนจอนุ่มนวล ไม่กระตุก
   - หน้าจอเด้งหน้าต่างพิเศษ **VOLUME CONTROL** ขนาดใหญ่แบบ Real-time
5. **I2S Audio Feedback (MAX98357A DAC):**
   - สังเคราะห์เสียง Waveform สดๆ จากชิป ESP32 ส่งออกแอมป์ขยายเสียง
   - มีเสียง Startup Chime (โด-มี-ซอล), เสียงคลิกปุ่มกด, เสียงไล่คีย์ระดับเสียง และเสียงจำลองสแครชแผ่นเสียง (Vinyl Scratch SFX)
6. **Turntable Motor & Scrubbing (DRV8833 + N20 Encoder):**
   - มอเตอร์หมุนจานแผ่นเสียงจำลองเมื่อเล่นเพลง
   - ตรวจจับเมื่อเอามือจับหมุนแผ่นเสียง: หยุดมอเตอร์ทันทีเพื่อป้องกันความร้อน (Stall Protection), หยุดเพลงชั่วคราว, สังเคราะห์เสียงเข็มครูดแผ่นเสียง และสั่ง Seek ตำแหน่งเพลงใหม่เมื่อปล่อยมือ
7. **WS2812B NeoPixel Status LEDs (15 ดวง):**
   - แสดงระดับเสียงแบบไล่เฉดสี (เขียว $\rightarrow$ เหลือง $\rightarrow$ แดง) และแสดงสถานะการเชื่อมต่อ Wi-Fi / เล่นเพลง

---

## 📌 ผังการต่อพิน (Pinout Reference)

> [!IMPORTANT]
> **ข้อควรระวังเรื่อง Strapping Pins ของ ESP32:**
>
> - หลีกเลี่ยงการใช้ขา **GPIO 15** และ **GPIO 5** สำหรับโมดูลที่มีตัวต้านทาน Pull-up/Pull-down (เช่น NeoPixel) เพราะจะทำให้บอร์ดเข้า Download Mode ไม่ได้และอัปโหลดโค้ดไม่เข้า (Error: `The serial TX path seems to be down`)
> - **GPIO 34** เป็นขา Input-Only เหมาะสำหรับการอ่านค่า `ECHO` ของเซนเซอร์ Ultrasonic

| ลำดับ | อุปกรณ์                           |                  ขา ESP32                   | คำอธิบายการเชื่อมต่อ                                                                                                                   |
| :---: | :-------------------------------- | :-----------------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------- |
| **1** | **SSD1306 OLED (0.96" I2C)**      |           `SDA: 21`<br>`SCL: 22`            | ไฟเลี้ยง 3.3V หรือ 5V, GND, I2C Clock 400kHz                                                                                           |
| **2** | **HC-SR04 Ultrasonic Sensor**     |          `TRIG: 18`<br>`ECHO: 34`           | **VCC ต่อไฟ 5V (VIN)**, GND, ขา ECHO เป็น Input-only                                                                                   |
| **3** | **Tactile Push Buttons (3 ปุ่ม)** | `PREV: 17`<br>`PLAY/PAUSE: 16`<br>`NEXT: 4` | สวิตช์ปุ่มกดต่อลง GND (ใช้งาน `INPUT_PULLUP` ภายในบอร์ด)                                                                               |
| **4** | **DRV8833 Motor Driver**          |           `IN1: 19`<br>`IN2: 23`            | IN1 ใช้สัญญาณ PWM (LEDC Channel 0), IN2 ลง GND, VM/VCC ต่อไฟเลี้ยงมอเตอร์                                                              |
| **5** | **N20 Rotary Encoder**            |             `A: 25`<br>`B: 26`              | ขา A ต่อ Interrupt ตรวจจับการหมุน, ขา B ตรวจจับทิศทาง                                                                                  |
| **6** | **WS2812B NeoPixel Strip**        |                 `DATA: 13`                  | จำนวน 15 ดวง (DIN ต่อขา 13, 5V ต่อ VIN, GND ร่วม)                                                                                      |
| **7** | **MAX98357A I2S Audio DAC**       |    `LRC: 27`<br>`BCLK: 32`<br>`DIN: 33`     | **Vin:** ต่อ 5V (VIN), **GND:** ต่อ GND<br>⚠️ **ขา SD:** ต้องต่อเข้าไฟ **3.3V หรือ 5V** เพื่อเปิดใช้งานแอมป์ (ห้ามต่อเข้าช่องขันลำโพง) |

---

## 🛠️ โครงสร้างโปรเจกต์ (Project Structure)

```
Spotify_Stream_Deck/
├── include/
│   ├── config.h               # [ห้าม Push ขึ้น Git] เก็บ Wi-Fi SSID, Password, Spotify Secrets
│   ├── config.example.h       # ไฟล์ตัวอย่างสำหรับตั้งค่า Credentials
│   ├── pin_config.h           # นิยามขา Pin Mapping ทั้งหมดของบอร์ด
│   ├── SpotifyClient.h        # คลาสจัดการ Spotify Web API & OAuth 2.0 (Keep-Alive)
│   ├── HardwareManager.h      # คลาสควบคุมฮาร์ดแวร์, จอ OLED, เซนเซอร์, LED, มอเตอร์
│   └── SoundEffects.h         # คลาสสังเคราะห์เสียง I2S DAC (Chime, Beep, Vinyl Scratch)
├── src/
│   ├── main.cpp               # FreeRTOS Dual-Core Setup & Background Worker Task
│   ├── SpotifyClient.cpp      # การส่งคำขอ HTTPS REST API, Token Refresh, Player Controls
│   ├── HardwareManager.cpp    # การอ่านปุ่ม, EMA Filter, Smooth Marquee, Volume Overlay
│   └── SoundEffects.cpp       # การสร้างสัญญาณเสียง Sine Wave และเสียงเอฟเฟกต์ I2S DMA
├── tools/
│   └── get_spotify_token.py   # สคริปต์ Python สำหรับทำ Spotify OAuth 2.0 ขอ Refresh Token
├── platformio.ini             # การตั้งค่า PlatformIO และ Dependencies
└── README.md                  # คู่มือการใช้งานและเอกสารประกอบโปรเจกต์
```

---

## 🚀 คู่มือการติดตั้งและเริ่มต้นใช้งาน (Getting Started)

### 1. ความต้องการของระบบ (Prerequisites)

- **Visual Studio Code** พร้อมติดตั้งส่วนขยาย **PlatformIO IDE**
- **Python 3.x** สำหรับรันสคริปต์ขอ Spotify Token
- บัญชี **Spotify (แนะนำ Spotify Premium)** สำหรับการสั่งข้ามเพลง/ปรับเสียงผ่าน Web API

### 2. การขอ Spotify API Credentials & Refresh Token

1. เข้าไปที่ [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
2. สร้าง App ใหม่ และเข้าไปที่ **Settings**:
   - คัดลอก **Client ID** และ **Client Secret**
   - ในส่วน **Redirect URIs** ให้เพิ่ม URL: `http://127.0.0.1:8888/callback` (ห้ามใช้ `localhost`) แล้วกด Save
3. เปิด Terminal และรันสคริปต์ขอ Refresh Token:
   ```powershell
   python tools/get_spotify_token.py
   ```
4. กรอก Client ID และ Client Secret ตามที่หน้าจอถาม จากนั้นเว็บเบราว์เซอร์จะเปิดขึ้นมาให้กดยืนยันสิทธิ์ เมื่อสำเร็จ สคริปต์จะแสดง `REFRESH_TOKEN` ให้คัดลอกไว้

### 3. การตั้งค่า Credentials ในโปรเจกต์

1. คัดลอกไฟล์ `include/config.example.h` แล้วเปลี่ยนชื่อเป็น `include/config.h`
2. ใส่ข้อมูล Wi-Fi และ Spotify Tokens:

   ```cpp
   #define WIFI_SSID             "ชื่อไวไฟของคุณ_2.4GHz"
   #define WIFI_PASSWORD         "รหัสผ่านไวไฟ"

   #define SPOTIFY_CLIENT_ID     "คัดลอก_Client_ID_มาวางที่นี่"
   #define SPOTIFY_CLIENT_SECRET "คัดลอก_Client_Secret_มาวางที่นี่"
   #define SPOTIFY_REFRESH_TOKEN "คัดลอก_Refresh_Token_มาวางที่นี่"
   ```

### 4. คอมไพล์และอัปโหลดลงบอร์ด (Upload Firmware)

เชื่อมต่อบอร์ด ESP32 เข้ากับคอมพิวเตอร์ผ่านสาย Micro-USB จากนั้นรันคำสั่งใน Terminal:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -t monitor
```

---

## 📖 คู่มือการใช้งานอุปกรณ์ (User Manual)

### 1. เมื่อเปิดเครื่อง (Startup Sequence)

- หลอดไฟ NeoPixel 15 ดวงจะกวาดไฟสีฟ้า 1 รอบ
- ลำโพง I2S จะเล่นเสียง **Startup Chime (โด-มี-ซอล)**
- หน้าจอ OLED แสดงสถานะ `"Connecting..."` และทำการเชื่อมต่อ Wi-Fi พร้อมดึงข้อมูลเพลงที่เล่นอยู่จาก Spotify อัตโนมัติ

### 2. การควบคุมการเล่นเพลง (Playback Controls)

- **ปุ่ม Play/Pause (ขา 16):** กดเพื่อสลับสถานะเล่นเพลงหรือหยุดเพลง (หน้าจอ OLED และมอเตอร์จะตอบสนองทันทีแบบ Optimistic UI)
- **ปุ่ม Next (ขา 4):** กดเพื่อข้ามไปเพลงถัดไป
- **ปุ่ม Previous (ขา 17):** กดเพื่อย้อนกลับไปเพลงก่อนหน้า

### 3. การปรับระดับเสียงไร้สัมผัส (Hand Gesture Volume)

- ยื่นมือเข้าใกล้ด้านหน้าเซนเซอร์ Ultrasonic (ระยะ 5 ถึง 30 เซนติเมตร)
- หน้าจอ OLED จะสลับเป็นโหมด **VOLUME CONTROL** ขนาดใหญ่ทันที พร้อมแถบเสียงและตัวเลขระยะมือ
- หลอดไฟ NeoPixel จะติดไล่ตามระดับเสียง (เขียว $\rightarrow$ เหลือง $\rightarrow$ แดง)
- ลำโพงจะส่งเสียง Beep ตามระดับความดัง (320 Hz – 1100 Hz)
- เมื่อได้ระดับเสียงที่ต้องการ ให้ดึงมือออก ระบบจะรอเพียง **200 ms** แล้วส่งระดับเสียงไปยัง Spotify ในทันที

### 4. จานหมุนแผ่นเสียงจำลอง (Turntable & Scratching)

- ขณะที่เพลงกำลังเล่น มอเตอร์ DRV8833 จะหมุนจานแผ่นเสียงด้วยความเร็วคงที่
- **การสแครช/เลื่อนเพลง (Scrubbing):**
  - เอามือจับและหมุนจานแผ่นเสียง
  - มอเตอร์จะตัดไฟทันที เพลงใน Spotify จะหยุดชั่วคราว และมีเสียงเข็มครูดแผ่นเสียง (**Vinyl Scratch SFX**) ดังออกลำโพง
  - เมื่อปล่อยมือ เพลงจะข้ามไปเล่นที่ตำแหน่งใหม่ตามรอบที่หมุน และมอเตอร์จะกลับมาหมุนต่ออย่างนุ่มนวล

---

## ❓ การแก้ปัญหาที่พบบ่อย (Troubleshooting)

| อาการที่พบ                                                     | สาเหตุ                                                            | วิธีแก้ไข                                                                                                         |
| :------------------------------------------------------------- | :---------------------------------------------------------------- | :---------------------------------------------------------------------------------------------------------------- |
| **อัปโหลดโค้ดไม่เข้า (`The serial TX path seems to be down`)** | ขา Strapping Pin (เช่น ขา 15 หรือ 5) ถูกต่อโหลดดึงสัญญาณขณะ Boot  | ถอดสายจากขา 15 และ 5 ออกขณะอัปโหลด หรือใช้ขาปลอดภัยตามตาราง Pinout                                                |
| **ลำโพงไม่มีเสียงออก**                                         | ขา `SD` ของโมดูล MAX98357A หลุดหรือต่อผิด                         | นำขา `SD` ไปเสียบเข้ากับไฟ `3.3V` หรือ `5V (VIN)` เพื่อปลด Mute ห้ามขันรวมกับสายลำโพงเด็ดขาด                      |
| **ขึ้น Error `HTTP 403 Restriction violated`**                 | ส่งคำสั่งซ้ำกับสถานะปัจจุบัน (เช่น สั่ง Play ขณะเพลงเล่นอยู่แล้ว) | โค้ดได้รับการปรับปรุงให้ส่ง `CMD_PAUSE` และ `CMD_PLAY` แยกกันชัดเจน ตรวจสอบว่าแอป Spotify บนเครื่องเปิดใช้งานอยู่ |
| **ESP32 ต่อ Wi-Fi ไม่ติด**                                     | ใช้ Wi-Fi ความถี่ 5GHz                                            | ESP32 รองรับเฉพาะ Wi-Fi คลื่น **2.4GHz** เท่านั้น กรุณาเชื่อมต่อกับ SSID คลื่น 2.4GHz                             |
| **ตัวหนังสือบน OLED ซ้อนทับกัน**                               | เปิดโหมด Text Wrap อัตโนมัติ                                      | โค้ดมีการตั้งค่า `_display.setTextWrap(false);` ไว้แล้ว ข้อความจะเลื่อนแนวนอนบรรทัดเดียวไม่ตกบรรทัด               |

---

## 👥 ผู้พัฒนา (Contributors)

- รายวิชา **CPE-214 Embedded Systems Project**
- Kitartist Riaobroi
- Pisit Farkham
