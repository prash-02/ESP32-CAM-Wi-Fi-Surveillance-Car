// ============================================================
//  ESP32-CAM – Stream Server  v3  (WiFi robust version)
//  Fixes: brownout disabled, longer retry, boot delay,
//         keeps retrying forever, better Serial diagnostics
// ============================================================
//
//  BOARD:  AI Thinker ESP32-CAM
//  POWER:  5V at least 500mA dedicated supply
//          GND must be common with ESP32 board
//
//  FLASH METHOD (using ESP32 as programmer):
//    ESP32 EN  → GND  (freezes ESP32 chip)
//    ESP32 TX  → ESP32-CAM UOR
//    ESP32 RX  → ESP32-CAM UOT
//    ESP32 5V  → ESP32-CAM 5V
//    ESP32 GND → ESP32-CAM GND
//    ESP32-CAM GPIO0 → GND  (flash mode)
//    After flash: remove GPIO0-GND wire, remove EN-GND wire, press RST
//
//  BOOT ORDER:
//    1. Power ESP32 controller first → wait 5 seconds
//    2. Then power ESP32-CAM
// ============================================================

// ── Disable brownout detector (fixes dim LED / power issues) ──
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <ArduinoOTA.h>

// ─── WiFi ──────────────────────────────────────────────────
const char* WIFI_SSID = "CameraCarAP";
const char* WIFI_PASS = "cameracar123";

// ─── Static IP ─────────────────────────────────────────────
IPAddress CAM_IP (192, 168, 4, 3);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET (255, 255, 255, 0);

// ─── LED pin (active LOW on AI Thinker) ────────────────────
#define LED_PIN 33

// ─── Camera pins (AI Thinker layout) ───────────────────────
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ─── Stream ────────────────────────────────────────────────
#define BOUNDARY "mjpegframe"
static const char* CT  = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char* PHD = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
httpd_handle_t streamSrv = NULL;

// ============================================================
//  STREAM HANDLER
// ============================================================
static esp_err_t streamHandler(httpd_req_t* req) {
  camera_fb_t* fb = NULL;
  esp_err_t res   = ESP_OK;
  char hbuf[80];

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  res = httpd_resp_set_type(req, CT);
  if (res != ESP_OK) return res;

  digitalWrite(LED_PIN, LOW);   // LED ON while streaming

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] Frame grab failed");
      res = ESP_FAIL; break;
    }
    size_t hl = snprintf(hbuf, sizeof(hbuf), PHD, fb->len);
    res = httpd_resp_send_chunk(req, hbuf, hl);
    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
  }
  digitalWrite(LED_PIN, HIGH);  // LED OFF
  return res;
}

static esp_err_t rootHandler(httpd_req_t* req) {
  const char* pg =
    "<!DOCTYPE html><html><body style='background:#000;color:#0f0;"
    "font-family:monospace;padding:20px'>"
    "<h2>&#9679; ESP32-CAM Online</h2>"
    "<p>Stream: <a style='color:#0cf' href='/stream'>/stream</a></p>"
    "<p>IP: 192.168.4.3</p>"
    "</body></html>";
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, pg, strlen(pg));
  return ESP_OK;
}

bool startStreamServer() {
  if (streamSrv != NULL) return true;  // already running
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.ctrl_port   = 32768;
  httpd_uri_t su = {"/stream", HTTP_GET, streamHandler, NULL};
  httpd_uri_t ru = {"/",       HTTP_GET, rootHandler,   NULL};
  if (httpd_start(&streamSrv, &cfg) == ESP_OK) {
    httpd_register_uri_handler(streamSrv, &su);
    httpd_register_uri_handler(streamSrv, &ru);
    Serial.println("[HTTP] Stream server started OK");
    Serial.println("[HTTP] ► http://192.168.4.3/stream");
    return true;
  }
  Serial.println("[HTTP] Stream server FAILED");
  return false;
}

// ============================================================
//  CAMERA INIT
// ============================================================
bool initCam() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM; cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM; cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM; cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM; cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    cfg.frame_size   = FRAMESIZE_VGA;   // 640×480
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 2;
    Serial.println("[CAM] PSRAM found → VGA 640x480, quality=12");
  } else {
    cfg.frame_size   = FRAMESIZE_QVGA;  // 320×240 (safer)
    cfg.jpeg_quality = 15;
    cfg.fb_count     = 1;
    Serial.println("[CAM] No PSRAM → QVGA 320x240, quality=15");
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[CAM] INIT FAILED! Error: 0x%x\n", err);
    Serial.println("[CAM] Check: ribbon cable seated? Board is AI Thinker?");
    return false;
  }

  // Improve image quality settings
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);     // -2 to 2
    s->set_contrast(s, 1);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_whitebal(s, 1);       // auto white balance
    s->set_awb_gain(s, 1);       // auto WB gain
    s->set_exposure_ctrl(s, 1);  // auto exposure
    s->set_aec2(s, 1);           // auto exposure correction
    s->set_gain_ctrl(s, 1);      // auto gain
    s->set_lenc(s, 1);           // lens correction
    s->set_hmirror(s, 0);        // 1 = mirror, 0 = normal
    s->set_vflip(s, 0);          // 1 = flip, 0 = normal
    Serial.println("[CAM] Image settings applied");
  }
  return true;
}

// ============================================================
//  WIFI CONNECT  –  retries forever, never gives up
// ============================================================
void connectWiFi() {
  Serial.printf("[WIFI] Connecting to '%s'...\n", WIFI_SSID);

  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.config(CAM_IP, GATEWAY, SUBNET);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    // Blink LED fast while connecting
    digitalWrite(LED_PIN, LOW);  delay(150);
    digitalWrite(LED_PIN, HIGH); delay(150);

    attempt++;
    if (attempt % 10 == 0) {
      Serial.printf("[WIFI] Still trying... attempt %d\n", attempt);
      Serial.printf("[WIFI] Make sure ESP32 controller is powered ON first!\n");

      // Every 20 failed attempts, reset WiFi and retry
      if (attempt % 20 == 0) {
        Serial.println("[WIFI] Resetting WiFi and retrying...");
        WiFi.disconnect(true);
        delay(1000);
        WiFi.mode(WIFI_STA);
        WiFi.config(CAM_IP, GATEWAY, SUBNET);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    } else {
      Serial.print(".");
    }
  }

  Serial.println();
  Serial.println("[WIFI] ✓ CONNECTED!");
  Serial.printf("[WIFI] IP address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WIFI] Expected:   192.168.4.3\n");

  if (WiFi.localIP() != CAM_IP) {
    Serial.println("[WIFI] WARNING: Got different IP than expected!");
    Serial.println("[WIFI] Stream will be at: http://" + WiFi.localIP().toString() + "/stream");
  }

  // Solid LED = connected
  digitalWrite(LED_PIN, LOW);
  delay(200);
  digitalWrite(LED_PIN, HIGH);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);  // let serial settle

  // ── CRITICAL: Disable brownout detector first thing
  //    This prevents resets caused by slight voltage drops
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.println("\n\n=== ESP32-CAM Stream Server v3 ===");
  Serial.println("[SYS] Brownout detector disabled");

  // LED setup
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // OFF (active low)

  // ── Small boot delay so ESP32 AP has time to start up
  Serial.println("[SYS] Waiting 3s for ESP32 AP to be ready...");
  for (int i = 3; i > 0; i--) {
    Serial.printf("[SYS] Starting in %d...\n", i);
    digitalWrite(LED_PIN, LOW);  delay(250);
    digitalWrite(LED_PIN, HIGH); delay(250);
  }

  // ── Init camera
  if (!initCam()) {
    Serial.println("[SYS] Camera failed. Halting.");
    while (true) {  // blink SOS
      for (int i=0; i<3; i++){digitalWrite(LED_PIN,LOW);delay(200);digitalWrite(LED_PIN,HIGH);delay(200);}
      delay(600);
    }
  }
  Serial.println("[CAM] ✓ Camera initialized");

  // ── Connect WiFi (retries forever)
  connectWiFi();

  // ── OTA wireless updates
  ArduinoOTA.setHostname("esp32cam-car");
  ArduinoOTA.setPassword("car123ota");
  ArduinoOTA.onStart([]() { Serial.println("[OTA] Starting..."); });
  ArduinoOTA.onEnd([]()   { Serial.println("[OTA] Done!"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error[%u]\n", e); });
  ArduinoOTA.begin();
  Serial.println("[OTA] Ready");

  // ── Start stream server
  startStreamServer();

  Serial.println("\n══════════════════════════════════");
  Serial.println("  ESP32-CAM is READY");
  Serial.println("  Stream: http://192.168.4.3/stream");
  Serial.println("══════════════════════════════════\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  ArduinoOTA.handle();

  // Auto-reconnect if WiFi drops
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Connection lost! Reconnecting...");
      streamSrv = NULL;
      connectWiFi();
      startStreamServer();
    }
  }

  delay(50);
}
