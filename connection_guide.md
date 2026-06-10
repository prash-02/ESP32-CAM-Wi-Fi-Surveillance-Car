# Camera Car – Full Connection & Flashing Guide

---

## ⚡ IMPORTANT: Remove ENA & ENB Jumpers From L298N First!
Those yellow jumpers on the L298N hardwire the enable pins to 5V.
With them in, the motors run at full speed ignoring all PWM signals.
Pull them out before wiring anything.

---

## 1. L298N Motor Driver Wiring

```
ESP32          L298N
─────          ──────
GPIO 25  ───►  ENA     (Left  motors – PWM speed)
GPIO 26  ───►  IN1     (Left  direction)
GPIO 27  ───►  IN2     (Left  direction)
GPIO 14  ───►  ENB     (Right motors – PWM speed)
GPIO 12  ───►  IN3     (Right direction)
GPIO 13  ───►  IN4     (Right direction)
GND      ───►  GND     ← CRITICAL: common ground!
```

### L298N Power:
```
Battery +  ───►  L298N  12V terminal
Battery -  ───►  L298N  GND terminal
                     │
                     └──► ESP32 GND  (same ground rail)

L298N 5V terminal ───►  ESP32 VIN   (only if 5V regulator jumper is ON the L298N)
```

### Motor connections (4 motors, 2 per channel in parallel):
```
L298N OUT1 ──┬── Left Motor 1 (+)
             └── Left Motor 2 (+)

L298N OUT2 ──┬── Left Motor 1 (-)
             └── Left Motor 2 (-)

L298N OUT3 ──┬── Right Motor 3 (+)
             └── Right Motor 4 (+)

L298N OUT4 ──┬── Right Motor 3 (-)
             └── Right Motor 4 (-)
```
> If car moves backward when it should go forward, swap OUT1↔OUT2 or OUT3↔OUT4.

---

## 2. Servo Motors (Pan & Tilt Camera Mount)

```
ESP32          Servo
─────          ─────
GPIO 16  ───►  Pan  servo  signal (orange/yellow wire)
GPIO 17  ───►  Tilt servo  signal (orange/yellow wire)

5V EXTERNAL ►  Both servo VCC (red wire)   ← NOT from ESP32 3.3V!
GND         ►  Both servo GND (brown/black)
```

> ⚠️ Do NOT power servos from ESP32's 3.3V pin.
> Use the 5V from L298N board or a separate 5V supply.
> Servos can draw 500mA+ and will crash the ESP32 if underpowered.

---

## 3. ESP32-CAM Connections (during normal operation)

```
Power Supply   ESP32-CAM
────────────   ─────────
5V         ──► 5V pin
GND        ──► GND pin
```

The ESP32-CAM connects to the ESP32's WiFi AP wirelessly.
No data wires between ESP32 and ESP32-CAM during operation.

---

## 4. How to Flash the ESP32-CAM Using Your ESP32 Board

> ✅ YES – you can use your ESP32 dev board as a programmer!
> The ESP32 board has a built-in USB-to-UART chip (CH340 or CP2102).
> By pulling the ESP32's EN pin to GND, the ESP32 chip goes into reset
> and the USB-UART chip becomes a pure serial pass-through.

### What you need:
- Your ESP32 dev board
- Jumper wires
- A short wire/jumper (to short ESP32 EN → GND)

### Step-by-step:

#### A. Wire it up:

```
ESP32 Dev Board    ESP32-CAM
───────────────    ─────────
EN         ──── GND  (on ESP32 itself – puts ESP32 chip in reset)
                      ↑ this makes the ESP32 act like a USB-UART adapter

TX (GPIO1) ──── UOR  (RXD pin on ESP32-CAM)
RX (GPIO3) ──── UOT  (TXD pin on ESP32-CAM)
5V         ──── 5V
GND        ──── GND

ESP32-CAM GPIO0 ──── GND  (puts ESP32-CAM into flash/download mode)
```

#### B. Visual connection diagram:

```
  [Your Computer USB]
         │
  ┌──────▼──────┐
  │  ESP32 Dev  │  EN ──┐
  │    Board    │       │ (short to GND)
  │  (as UART)  │  GND ─┘
  │             │
  │  TX ────────┼──────► UOR (RX) ─┐
  │  RX ────────┼──────► UOT (TX) ─┤  ESP32-CAM
  │  5V ────────┼──────► 5V        │
  │  GND ───────┼──────► GND       │
  └─────────────┘  GPIO0 ──────────┘── GND
```

#### C. Upload steps:

1. Wire everything as above (EN-to-GND on ESP32, GPIO0-to-GND on ESP32-CAM)
2. Open Arduino IDE
3. Select board: **AI Thinker ESP32-CAM**
4. Select the correct COM port (same as your ESP32 dev board)
5. Set Upload Speed: **115200** (more reliable than higher speeds)
6. Click **Upload** (⬆️ arrow)
7. When you see `Connecting........_____` in the output:
   - Press the **RST button** on the ESP32-CAM once
   - OR disconnect and reconnect its 5V power
8. Uploading begins. Wait for "Done uploading."
9. **After upload:**
   - Remove the GPIO0-to-GND wire from ESP32-CAM
   - Remove the EN-to-GND wire from ESP32 board
   - Press RST on ESP32-CAM (or power cycle)
   - ESP32-CAM now runs normally

#### D. Common errors and fixes:

| Error | Fix |
|-------|-----|
| `Failed to connect to ESP32: Timed out` | Press RST on ESP32-CAM while dots are appearing |
| `Brownout detector was triggered` | Power issue – use 5V external supply, not USB alone |
| `No COM port showing` | Install CP210x or CH340 driver for your OS |
| Camera shows garbled image | Re-check all camera ribbon cable connections |
| Upload starts but freezes at 0% | Wrong baud rate – try 115200 |

---

## 5. Easiest Alternative: ESP32-CAM-MB Programmer Board

If you have the small ESP32-CAM-MB board (often sold with the ESP32-CAM):
```
1. Plug ESP32-CAM onto the MB board (gold pins align)
2. Connect MB board to computer via micro USB
3. Select AI Thinker ESP32-CAM in Arduino IDE
4. Click Upload
5. Press the IO0 button on MB board while "Connecting..." shows
6. Done! No wires needed.
```
The MB board is sold for ~$3 on AliExpress/Amazon and is the easiest option.

---

## 6. Flashing Order (important!)

**Always flash in this order:**
```
1️⃣  Flash ESP32 (esp32_webserver_controller.ino) first
     → It creates the WiFi AP "CameraCarAP"
     → Verify: Serial Monitor shows AP started at 192.168.4.1

2️⃣  Flash ESP32-CAM (esp32cam_stream.ino) second
     → It connects to the AP created by ESP32
     → Verify: Serial Monitor shows "IP: 192.168.4.3"

If you flash ESP32-CAM first, it can't connect because the AP doesn't exist yet.
```

---

## 7. Library Installation (Arduino IDE)

### For ESP32 (controller):
Open Arduino IDE → Tools → Manage Libraries → search and install:

| Library | Author |
|---------|--------|
| ESPAsyncWebServer | lacamera |
| AsyncTCP | dvarrel |
| ESP32Servo | Kevin Harrington |

> ESPmDNS is built into the ESP32 board package. No install needed.

### For ESP32-CAM:
- `ArduinoOTA` – built-in, no install needed
- `esp_camera.h` – built-in with ESP32 board package

### Board Package (if not installed):
1. Arduino IDE → File → Preferences
2. Add to "Additional Boards Manager URLs":
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Boards Manager → search `esp32` → install **esp32 by Espressif Systems**
4. Select board: **ESP32 Dev Module** (for controller) or **AI Thinker ESP32-CAM**

---

## 8. How to Use the Control Page

1. Power up the ESP32 (controller)
2. Power up the ESP32-CAM
3. On your phone or laptop: connect to WiFi **CameraCarAP** (password: `cameracar123`)
4. Open browser: **http://cameracar.local** (Android: use **http://192.168.4.1**)
5. Camera stream loads automatically (wait ~5 seconds)

| Control | Action |
|---------|--------|
| Left joystick | Drive forward/back + steer left/right |
| Right joystick | Pan and tilt the camera (rate mode – hold to keep moving) |
| ⛔ STOP button | Emergency stop – move left joystick again to resume |
| ↺ CENTRE | Returns camera to 90°/90° |
| ⚡ TURBO | Full speed mode (default is 65% for control) |

> **Car does NOT move on startup.** You must move the left joystick to drive.

---

## 9. Improvements Made (vs Previous Version)

| Issue | Fix Applied |
|-------|-------------|
| Car moves on boot | Direction pins set LOW BEFORE PWM attaches |
| Car moves after STOP | `stopped=true` on WebSocket connect; joystick must move >5% to resume |
| Camera jumps on joystick release | Camera now uses rate mode (incremental) – holds position on release |
| Had to type IP in browser | mDNS added → http://cameracar.local |
| Had to enter CAM IP manually | Camera IP hardcoded (192.168.4.3 static), auto-loads |
| Camera stream didn't retry | Auto-retries every 3 seconds on error |
| No latency display | Ping/pong WebSocket latency shown in header |
| WiFi dropout = no recovery | ESP32-CAM auto-reconnects, motor safety timeout |
| No wireless re-flash for CAM | OTA added → update ESP32-CAM wirelessly after first flash |

---

## 10. Power Budget

| Component | Current Draw |
|-----------|-------------|
| ESP32 dev board | ~240mA peak |
| ESP32-CAM (streaming) | ~160mA, ~270mA with flash LED |
| 4 DC motors (loaded) | ~500mA each = 2A total |
| 2 Servo motors | ~500mA each = 1A total |
| **Total estimate** | **~3.5–4A @ 12V recommended** |

Use a battery that can supply at least 4A continuously.
LiPo 3S (11.1V) or Li-ion 3S with a proper BMS is ideal.

---

*End of guide*
