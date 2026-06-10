# 🚗 ESP32 Camera Car

A WiFi-controlled FPV camera car built with an ESP32 and ESP32-CAM.  
Control it from any browser on your phone or laptop — no app needed.

![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B%20%7C%20HTML%2FJS-orange?style=flat-square)
![IDE](https://img.shields.io/badge/IDE-Arduino-teal?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)

---

## 📷 Features

- **Live video stream** from ESP32-CAM viewable directly in the browser
- **Virtual dual joystick** web interface — works on phone and desktop
- **Pan & tilt camera** control with 2 servo motors
- **4-motor differential drive** (2 motors per side, in parallel)
- **WebSocket control** at 20 Hz for low latency response
- **mDNS support** — open `http://cameracar.local` instead of typing an IP
- **FPV-style HUD** — live speed, direction arrow, ping display, pan/tilt angles
- **Turbo mode** toggle (65% normal / 100% turbo)
- **Emergency stop** button with haptic feedback on mobile
- **Safety timeout** — motors auto-stop if browser disconnects
- **OTA updates** for ESP32-CAM after first flash
- **No ENA/ENB wired to ESP32** — safe motor wiring, prevents GPIO damage

---

## 🧰 Hardware Required

| Component | Quantity | Notes |
|-----------|----------|-------|
| ESP32 Dev Board | 1 | Main controller |
| ESP32-CAM (AI Thinker) | 1 | Video streaming |
| ESP32-CAM-MB Programmer | 1 | For easy flashing |
| L298N Motor Driver | 1 | Dual H-bridge |
| DC Motors | 4 | 2 left side, 2 right side |
| SG90 / MG90S Servo | 2 | Camera pan & tilt |
| Car chassis | 1 | 4WD platform |
| Battery | 1 | 7.4V–12V LiPo or Li-ion |
| 5V regulator / BEC | 1 | For ESP32 and servos |
| Jumper wires | — | Male-to-male |

---

## 🏗️ System Architecture

```
┌──────────────────────┐         WiFi AP          ┌──────────────────────┐
│      Browser         │  ◄──── cameracar.local ───►│    ESP32 (Master)    │
│  (Phone / Laptop)    │         WebSocket           │  192.168.4.1         │
│                      │                             │                      │
│  Left  Joystick ─────┼─── drive + steer ──────────►  L298N → 4 Motors   │
│  Right Joystick ─────┼─── pan rate + tilt rate ───►  Servo Pan + Tilt   │
│                      │                             └──────────────────────┘
│  Video stream ◄──────┼──────────────────────────────────────────────────┐
└──────────────────────┘          WiFi AP          ┌──────────────────────┐
                                                    │    ESP32-CAM         │
                                                    │  192.168.4.3 (static)│
                                                    │  /stream → MJPEG     │
                                                    └──────────────────────┘
```

---

## ⚡ Wiring

### ⚠️ Important Safety Note
Many L298N boards output **raw battery voltage (9V–12V)** on the ENA and ENB pins instead of a safe logic level. **Never connect ENA/ENB to ESP32 GPIO** — it will instantly destroy the GPIO or the entire chip.

**Solution used in this project:** Leave the ENA and ENB **jumpers ON** the L298N board (motors always enabled), and control speed by PWM-ing the IN pins directly. Safe, simple, and works perfectly.

---

### ESP32 → L298N

> Remove **nothing** — keep both jumpers (ENA & ENB) on the L298N board.

| ESP32 GPIO | L298N Pin | Function |
|------------|-----------|----------|
| GPIO 26 | IN1 | Left motors — forward (PWM) |
| GPIO 27 | IN2 | Left motors — backward (PWM) |
| GPIO 12 | IN3 | Right motors — forward (PWM) |
| GPIO 13 | IN4 | Right motors — backward (PWM) |
| GND | GND | Common ground ← **must connect** |

> **Do NOT connect** GPIO 25, GPIO 14, or any other pin to ENA/ENB.

### L298N Power

| L298N Terminal | Connect To |
|----------------|------------|
| 12V | Battery positive |
| GND | Battery negative |
| GND | Also to ESP32 GND (common ground) |
| 5V out | ❌ Do NOT use — power ESP32 separately |

### L298N → Motors (4 motors, 2 per channel in parallel)

```
OUT1 ──┬── Motor 1 (+)      OUT3 ──┬── Motor 3 (+)
       └── Motor 2 (+)             └── Motor 4 (+)

OUT2 ──┬── Motor 1 (-)      OUT4 ──┬── Motor 3 (-)
       └── Motor 2 (-)             └── Motor 4 (-)
```

> If the car moves backwards when it should go forward, swap OUT1 ↔ OUT2 (or OUT3 ↔ OUT4).

---

### ESP32 → Servo Motors

| ESP32 GPIO | Servo | Wire Color |
|------------|-------|------------|
| GPIO 16 | Pan servo signal | Orange / Yellow |
| GPIO 17 | Tilt servo signal | Orange / Yellow |
| 5V (external) | Both servo VCC | Red |
| GND | Both servo GND | Brown / Black |

> ⚠️ Power servos from an external 5V supply. Do **not** use the ESP32's 3.3V pin — servos draw too much current and will crash the ESP32.

---

### ESP32-CAM (wireless — no data wires needed during operation)

| Supply | ESP32-CAM |
|--------|-----------|
| 5V | 5V pin |
| GND | GND pin |

The ESP32-CAM connects to the ESP32's WiFi AP wirelessly and streams at `http://192.168.4.3/stream`. The control page loads this automatically.

---

## 💻 Software Setup

### 1. Install ESP32 Board Package

In Arduino IDE → **File → Preferences**, add this URL to *Additional Boards Manager URLs*:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then go to **Tools → Board → Boards Manager**, search `esp32`, and install **esp32 by Espressif Systems**.

---

### 2. Install Libraries

Go to **Tools → Manage Libraries** and install:

| Library | Author |
|---------|--------|
| ESPAsyncWebServer | lacamera |
| AsyncTCP | dvarrel |
| ESP32Servo | Kevin Harrington |

> `ESPmDNS` and `ArduinoOTA` are built into the ESP32 board package — no install needed.

---

### 3. Board Settings

#### For `esp32_webserver_controller.ino` (main ESP32):

| Setting | Value |
|---------|-------|
| Board | ESP32 Dev Module |
| Upload Speed | 921600 |
| Flash Mode | QIO |
| Partition Scheme | Default 4MB with spiffs |

#### For `esp32cam_stream.ino` (ESP32-CAM):

| Setting | Value |
|---------|-------|
| Board | **AI Thinker ESP32-CAM** |
| Upload Speed | **115200** |
| Flash Mode | **DIO** |
| Flash Frequency | 40MHz |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |

> ⚠️ Using the wrong Flash Mode for ESP32-CAM causes **"Packet content transfer stopped"** errors. Always use DIO at 115200 for the ESP32-CAM.

---

## 🔁 Flashing Order

Always flash in this order:

```
1️⃣  Flash ESP32 first       →  esp32_webserver_controller.ino
     It creates the WiFi AP "CameraCarAP".

2️⃣  Flash ESP32-CAM second  →  esp32cam_stream.ino
     It connects to the AP created by ESP32.
     Check Serial Monitor for its assigned IP (should be 192.168.4.3).
```

If you flash the ESP32-CAM first, it cannot connect because the AP doesn't exist yet.

---

## 📡 Flashing the ESP32-CAM

### Option A — Using the ESP32-CAM-MB Programmer Board (Easiest)

1. Plug the ESP32-CAM onto the MB board (gold pins align)
2. Connect the MB board to your PC via Micro USB
3. Select board: **AI Thinker ESP32-CAM**
4. Click **Upload**
5. When `Connecting........_____` appears, press the **IO0** button on the MB board
6. Wait for "Done uploading."
7. Press **RST** to boot into the new firmware

### Option B — Using Your ESP32 Dev Board as a Programmer

Short the ESP32's **EN pin to GND** (this disables the ESP32 chip and turns the board into a USB-UART pass-through), then wire:

| ESP32 Board | ESP32-CAM |
|-------------|-----------|
| EN → GND (on ESP32 itself) | — |
| TX (GPIO1) | UOR (RX) |
| RX (GPIO3) | UOT (TX) |
| 5V | 5V |
| GND | GND |
| — | GPIO0 → GND (flash mode) |

After uploading: remove both the GPIO0-GND and EN-GND wires, then press RST on the ESP32-CAM.

---

## 🎮 How to Use

1. Power on the ESP32 (controller board)
2. Power on the ESP32-CAM
3. On your phone or laptop, connect to WiFi: **`CameraCarAP`** (password: `cameracar123`)
4. Open a browser and go to: **`http://cameracar.local`**
   - On Android: use `http://192.168.4.1` (Android doesn't support `.local` mDNS)
5. The camera stream loads automatically from `192.168.4.3`

### Controls

| Control | Action |
|---------|--------|
| **Left joystick** | Drive forward/backward + steer left/right |
| **Right joystick** | Pan and tilt the camera (rate mode — hold to keep rotating) |
| **⛔ STOP** | Emergency stop. Move left joystick again to resume |
| **↺ CENTRE** | Returns camera servos to 90°/90° |
| **⚡ TURBO** | Switches between 65% speed (normal) and 100% (turbo) |

> The car does **not move on startup**. You must move the left joystick to begin driving.

---

## 🛠️ Troubleshooting

| Problem | Likely Cause | Fix |
|---------|-------------|-----|
| Car moves on its own at startup | ENA/ENB jumpers removed and EN pins floating | Put jumpers back on L298N |
| Motors spin at full speed, ignore joystick | ENA/ENB jumpers still on but EN pins connected | Disconnect ENA/ENB from ESP32 |
| `Packet content transfer stopped` on upload | Wrong Flash Mode | Set Flash Mode to **DIO** in Arduino IDE |
| ESP32-CAM not detected | GPIO0 not held LOW | Hold GPIO0 to GND before upload and press RST |
| Camera shows "OFFLINE" in browser | ESP32-CAM not connected to AP yet | Wait 10s, check Serial Monitor for IP |
| `http://cameracar.local` doesn't open | Android mDNS not supported | Use `http://192.168.4.1` instead |
| `ledcSetup` compile error | New ESP32 board package (3.x) | Use `ledcAttach(pin, freq, bits)` — already done in this code |
| Servo twitches once on boot | Boot state of GPIO0/GPIO2 | Normal behaviour, servos settle immediately |
| ESP32 brownout / restart loop | Power supply too weak | Use a proper LiPo with BEC, not USB power alone |
| Car goes in wrong direction | Motor wires reversed | Swap OUT1↔OUT2 or OUT3↔OUT4 on L298N |

---

## 📁 File Structure

```
📦 esp32-camera-car/
├── 📄 esp32_webserver_controller.ino   ← Flash to ESP32 dev board
├── 📄 esp32cam_stream.ino              ← Flash to ESP32-CAM
├── 📄 connection_guide.md              ← Detailed wiring + flashing guide
└── 📄 README.md                        ← This file
```

---

## 🔋 Power Budget

| Component | Current Draw |
|-----------|-------------|
| ESP32 dev board | ~240 mA peak |
| ESP32-CAM (streaming) | ~160 mA average |
| 4 DC motors (loaded) | ~500 mA each = ~2 A total |
| 2 SG90 servos | ~250 mA each = ~500 mA total |
| **Total estimate** | **~3–4 A @ 7.4V–12V** |

Use a **LiPo 2S (7.4V) or 3S (11.1V)** with at least 2000 mAh and 20C discharge rating.  
Power the ESP32 and servos from a **5V BEC** connected to the battery.

---

## 🔧 Customisation

### Change WiFi name or password
In `esp32_webserver_controller.ino` and `esp32cam_stream.ino`, edit:
```cpp
const char* AP_SSID = "CameraCarAP";   // change WiFi name here
const char* AP_PASS = "cameracar123";  // change password here
```

### Change browser address
In `esp32_webserver_controller.ino`, edit:
```cpp
const char* MDNS_HOST = "cameracar";  // opens at http://cameracar.local
```

### Adjust normal speed limit
In the HTML (inside the controller .ino), find:
```js
const scale = turbo ? 100 : 65;   // change 65 to any value 0–100
```

### Adjust servo range
In `esp32_webserver_controller.ino`:
```cpp
servoPan.write (constrain(pan,  30, 150));  // change pan  min/max degrees
servoTilt.write(constrain(tilt, 40, 140));  // change tilt min/max degrees
```

### Change video resolution
In `esp32cam_stream.ino`:
```cpp
cfg.frame_size   = FRAMESIZE_VGA;    // VGA=640×480, QVGA=320×240, SVGA=800×600
cfg.jpeg_quality = 12;               // 10=best quality, 63=worst
```

---

## 📝 License

This project is open source under the [MIT License](LICENSE).  
Feel free to use, modify, and share it.

---

## 🙌 Acknowledgements

- [ESPAsyncWebServer](https://github.com/lacamera/ESPAsyncWebServer) by lacamera
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) by Kevin Harrington
- Espressif ESP32 Arduino core and esp-idf camera drivers

---

*Built with ❤️ using ESP32 + Arduino IDE*
