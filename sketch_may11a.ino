/*
 * ============================================================================
 * FanCam — ESP32-CAM Intelligent Fan Controller + Live Dashboard
 * ============================================================================
 * Description : AI-Thinker ESP32-CAM firmware combining OV2640 MJPEG streaming
 *               with a 4-pin PWM fan controller, tachometer, stall detection,
 *               LED dimmer, FreeRTOS multi-core architecture, NVS persistence,
 *               OTA updates, and a polished single-page web dashboard.
 *
 * Pinout:
 *   LED_PIN  = GPIO4   Onboard white/flash LED (active HIGH via LEDC)
 *   FAN_PIN  = GPIO13  PWM to fan MOSFET gate or 4-pin fan PWM input
 *   TACH_PIN = GPIO2   Tachometer input (INPUT_PULLUP, FALLING edge ISR)
 *   Camera pins defined in camera_pins.h (AI-Thinker OV2640)
 *
 * Arduino IDE Board Settings:
 *   Board            : AI Thinker ESP32-CAM
 *   Partition Scheme : Huge APP (3MB No OTA / 1MB SPIFFS)
 *   PSRAM            : Enabled
 *   Flash Size       : 4MB (32Mb)
 *   Upload Speed     : 921600
 *   CPU Frequency    : 240 MHz
 *
 * Library Dependencies (install via Arduino Library Manager):
 *   - ESP32 Arduino core  >= 2.0.14   (espressif/arduino-esp32)
 *   - ArduinoJson         >= 6.21.3   (bblanchon/ArduinoJson)
 *   - ESPmDNS             (bundled with ESP32 core)
 *   - ArduinoOTA          (bundled with ESP32 core)
 *   - Preferences         (bundled with ESP32 core)
 *   - WebServer           (bundled with ESP32 core)
 *   - esp_camera          (bundled with ESP32 core)
 *
 * LEDC Channel Assignments (MUST NOT conflict with camera XCLK):
 *   Channel 0 : Camera XCLK — used internally by esp_camera driver. DO NOT TOUCH.
 *   Channel 4 : FAN_PIN   @ 25 kHz  8-bit  (4-pin PC fan spec)
 *   Channel 5 : LED_PIN   @  5 kHz  8-bit  (smooth dimming)
 *
 * How to wire the fan:
 *   - Fan supply: 12 V rail (or 5 V for 5 V fans). Keep ESP on its own 3.3 V reg.
 *   - Common ground: ESP GND and fan supply GND MUST share a common reference.
 *   - 3-pin / brushless via MOSFET:
 *       GPIO13 --[1kOhm]--> Gate of N-channel MOSFET (e.g. IRLZ44N)
 *       Drain -> Fan -ve | Source -> GND | Fan +ve -> 12 V
 *   - 4-pin fan (preferred): Connect GPIO13 DIRECTLY to pin 4 (PWM line).
 *       No MOSFET needed on the PWM line; still tie grounds together.
 *   - Tachometer (pin 3 on 4-pin header):
 *       Connect to TACH_PIN (GPIO2) with INPUT_PULLUP.
 *       DO NOT connect tach to 5 V — GPIO2 is 3.3 V tolerant only.
 *       A 1 kOhm series resistor adds ESD protection.
 *   - LED_PIN (GPIO4) is the BRIGHT onboard flash LED (~300 mA at full duty).
 *       The AI-Thinker PCB has a ~10 Ohm current limiter. Use sparingly.
 *       For an external LED: add a 68 Ohm series resistor for ~20 mA at 3.3 V.
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include <esp_task_wdt.h>
#include <math.h>

#include "camera_pins.h"
#include "web_content.h"

// ─── Firmware version ────────────────────────────────────────────────────────
#define FW_VERSION "1.0.0"

// ─── Hardware pins ───────────────────────────────────────────────────────────
#define LED_PIN   4
#define FAN_PIN   13
#define TACH_PIN  2

// ─── LEDC channel assignments ────────────────────────────────────────────────
// Channel 0 is claimed by esp_camera for XCLK. Using 4 & 5 keeps us clear.
#define LEDC_FAN_CH   4
#define LEDC_LED_CH   5
#define LEDC_FAN_FREQ 25000
#define LEDC_LED_FREQ  5000
#define LEDC_RES       8       // 8-bit: 0-255

// ─── FreeRTOS task priorities ────────────────────────────────────────────────
#define PRI_WEB    5
#define PRI_STREAM 5
#define PRI_FAN    6
#define PRI_TACH   6

// ─── Logging ─────────────────────────────────────────────────────────────────
enum LogLevel { LOG_DEBUG=0, LOG_INFO, LOG_WARN, LOG_ERROR };
volatile LogLevel gLogLevel = LOG_INFO;

#define LOG(lvl, fmt, ...) do { \
  if ((lvl) >= gLogLevel) { \
    const char* _n[] = {"DBG","INF","WRN","ERR"}; \
    Serial.printf("[%8lu][%s] " fmt "\n", millis(), _n[(lvl)], ##__VA_ARGS__); \
  } \
} while(0)

// ─── Ring-buffer event log ───────────────────────────────────────────────────
#define EVENT_BUF_SIZE 20
struct FanEvent {
  uint32_t tMs;
  char     type[20];
  uint8_t  pwm;
  uint16_t rpm;
};
FanEvent  gEvents[EVENT_BUF_SIZE];
uint8_t   gEventHead  = 0;
uint8_t   gEventCount = 0;
SemaphoreHandle_t gEventMux;

void pushEvent(const char* type, uint8_t pwm, uint16_t rpm) {
  xSemaphoreTake(gEventMux, portMAX_DELAY);
  FanEvent& e = gEvents[gEventHead];
  e.tMs = millis();
  strlcpy(e.type, type, sizeof(e.type));
  e.pwm = pwm;
  e.rpm = rpm;
  gEventHead = (gEventHead + 1) % EVENT_BUF_SIZE;
  if (gEventCount < EVENT_BUF_SIZE) gEventCount++;
  xSemaphoreGive(gEventMux);
}

// ─── Shared telemetry ────────────────────────────────────────────────────────
struct Telemetry {
  float    tempC;
  uint8_t  fanPWM;
  uint8_t  fanTargetPWM;
  uint16_t fanRPM;
  uint8_t  ledPWM;
  bool     fanAuto;
  bool     ledOn;
  bool     ledPulse;
  uint32_t uptimeMs;
  int32_t  rssi;
  uint32_t freeHeap;
  uint16_t stallCount;
  bool     faultLatched;
  uint16_t healthRPM[4];
  bool     healthReady;
};
Telemetry gTel = {};
SemaphoreHandle_t gTelMux;

// ─── Settings (NVS-persisted) ─────────────────────────────────────────────────
struct Settings {
  float   curveTemp[5];
  float   curvePWM[5];
  float   slewRate;
  float   stallRatio;
  float   stallDelay;
  uint8_t manualPWM;
  bool    fanAuto;
  bool    ledOn;
  uint8_t ledPWM;
  bool    ledPulse;
  char    ssid[64];
  char    pass[64];
};

Settings gSettings = {
  {40, 45, 55, 65, 75},  // curveTemp defaults
  { 0, 30, 60, 85,100},  // curvePWM defaults
  2.0f,                   // slewRate
  30.0f,                  // stallRatio
  3.0f,                   // stallDelay
  50,                     // manualPWM
  true,                   // fanAuto
  false,                  // ledOn
  50,                     // ledPWM
  false,                  // ledPulse
  "YOUR_WIFI_SSID",       // default placeholder, overridden by NVS if user changes it
  "YOUR_WIFI_PASSWORD"    // default placeholder
};

Preferences gPrefs;
SemaphoreHandle_t gSettingsMux;

void loadSettings() {
  gPrefs.begin("fancam", true);
  for (int i = 0; i < 5; i++) {
    char k[8];
    snprintf(k, 8, "ct%d", i); gSettings.curveTemp[i] = gPrefs.getFloat(k, gSettings.curveTemp[i]);
    snprintf(k, 8, "cp%d", i); gSettings.curvePWM[i]  = gPrefs.getFloat(k, gSettings.curvePWM[i]);
  }
  gSettings.slewRate   = gPrefs.getFloat("slew",    gSettings.slewRate);
  gSettings.stallRatio = gPrefs.getFloat("sratio",  gSettings.stallRatio);
  gSettings.stallDelay = gPrefs.getFloat("sdelay",  gSettings.stallDelay);
  gSettings.manualPWM  = gPrefs.getUChar("mpwm",    gSettings.manualPWM);
  gSettings.fanAuto    = gPrefs.getBool ("fauto",    gSettings.fanAuto);
  gSettings.ledOn      = gPrefs.getBool ("ledon",    gSettings.ledOn);
  gSettings.ledPWM     = gPrefs.getUChar("ledpwm",  gSettings.ledPWM);
  gSettings.ledPulse   = gPrefs.getBool ("ledpulse",gSettings.ledPulse);
  // Only overwrite the compiled-in defaults when NVS actually has a value stored.
  // This lets the hardcoded credentials work on first flash without any setup.
  char tmp[64] = "";
  if (gPrefs.getString("ssid", tmp, sizeof(tmp)) && strlen(tmp) > 0)
    strlcpy(gSettings.ssid, tmp, sizeof(gSettings.ssid));
  tmp[0] = '\0';
  if (gPrefs.getString("pass", tmp, sizeof(tmp)) && strlen(tmp) > 0)
    strlcpy(gSettings.pass, tmp, sizeof(gSettings.pass));
  gPrefs.end();
}

void persistSettings() {
  gPrefs.begin("fancam", false);
  for (int i = 0; i < 5; i++) {
    char k[8];
    snprintf(k, 8, "ct%d", i); gPrefs.putFloat(k, gSettings.curveTemp[i]);
    snprintf(k, 8, "cp%d", i); gPrefs.putFloat(k, gSettings.curvePWM[i]);
  }
  gPrefs.putFloat("slew",    gSettings.slewRate);
  gPrefs.putFloat("sratio",  gSettings.stallRatio);
  gPrefs.putFloat("sdelay",  gSettings.stallDelay);
  gPrefs.putUChar("mpwm",    gSettings.manualPWM);
  gPrefs.putBool ("fauto",   gSettings.fanAuto);
  gPrefs.putBool ("ledon",   gSettings.ledOn);
  gPrefs.putUChar("ledpwm",  gSettings.ledPWM);
  gPrefs.putBool ("ledpulse",gSettings.ledPulse);
  gPrefs.putString("ssid",   gSettings.ssid);
  gPrefs.putString("pass",   gSettings.pass);
  gPrefs.end();
}

// ─── Tachometer ISR ──────────────────────────────────────────────────────────
// Debounce rejects noise pulses closer than 500 µs — real fan pulses at
// 3000 RPM / 2 pulses/rev are ~10 ms apart, well above the threshold.
volatile uint32_t gTachPulses  = 0;
volatile uint32_t gLastPulseUs = 0;
static const uint32_t TACH_DEBOUNCE_US = 500;

void IRAM_ATTR tachISR() {
  uint32_t now = micros();
  if ((now - gLastPulseUs) < TACH_DEBOUNCE_US) return;
  gLastPulseUs = now;
  gTachPulses++;
}

// ─── PWM helpers ─────────────────────────────────────────────────────────────
inline uint32_t pctToDuty(float pct) {
  return (uint32_t)constrain(roundf(pct * 2.55f), 0.0f, 255.0f);
}
void setFanDuty(float pct) { ledcWrite(LEDC_FAN_CH, pctToDuty(pct)); }
void setLedDuty(float pct) { ledcWrite(LEDC_LED_CH, pctToDuty(pct)); }

// ─── Temperature curve interpolation ─────────────────────────────────────────
float tempToPWM(float tempC) {
  float* T = gSettings.curveTemp;
  float* P = gSettings.curvePWM;
  if (tempC <= T[0]) return P[0];
  if (tempC >= T[4]) return P[4];
  for (int i = 0; i < 4; i++) {
    if (tempC >= T[i] && tempC <= T[i+1]) {
      float r = (tempC - T[i]) / (T[i+1] - T[i]);
      return P[i] + r * (P[i+1] - P[i]);
    }
  }
  return P[4];
}

// ─── Slew-rate limiter ────────────────────────────────────────────────────────
// Called every 20 ms (50 Hz); slewRate is % per 100 ms, so per-tick = * 0.2.
static float slew(float cur, float tgt, float slewPer100ms) {
  float maxD = slewPer100ms * 0.2f;
  float d = tgt - cur;
  if (fabsf(d) <= maxD) return tgt;
  return cur + (d > 0 ? maxD : -maxD);
}

// ─── Camera init ─────────────────────────────────────────────────────────────
bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;  // owned by esp_camera for XCLK
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
    cfg.frame_size   = FRAMESIZE_VGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 2;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;
    LOG(LOG_INFO, "PSRAM found — VGA dual framebuffer");
  } else {
    cfg.frame_size   = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 15;
    cfg.fb_count     = 1;
    cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    LOG(LOG_WARN, "No PSRAM — QVGA single framebuffer");
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) { LOG(LOG_ERROR, "Camera init 0x%x", err); return false; }

  LOG(LOG_INFO, "Camera OK");
  return true;
}

// ─── Wi-Fi ───────────────────────────────────────────────────────────────────
bool gApMode = false;

void initWifi() {
  if (strlen(gSettings.ssid) > 0) {
    LOG(LOG_INFO, "STA -> %s", gSettings.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(gSettings.ssid, gSettings.pass);
    uint32_t deadline = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
      LOG(LOG_INFO, "Connected: %s", WiFi.localIP().toString().c_str());
      return;
    }
    LOG(LOG_WARN, "STA failed, falling back to AP");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP-FanCam", "fancam1234");
  gApMode = true;
  LOG(LOG_INFO, "AP: 192.168.4.1");
}

// =============================================================================
// Stream server — port 81, Core 0
// Runs as a raw TCP server so MJPEG streams can't block the main WebServer.
// =============================================================================
WiFiServer gStreamServer(81);

void streamTask(void* pv) {
  gStreamServer.begin();
  LOG(LOG_INFO, "Stream :81");
  for (;;) {
    WiFiClient client = gStreamServer.available();
    if (!client) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    // Drain the HTTP request headers
    String req;
    uint32_t t0 = millis();
    while (client.connected() && millis() - t0 < 3000) {
      if (client.available()) { req += (char)client.read(); }
      if (req.endsWith("\r\n\r\n")) break;
    }

    bool capture = req.indexOf("GET /capture") >= 0;

    if (capture) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        client.printf("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
                      "Content-Disposition: attachment; filename=capture.jpg\r\n"
                      "Content-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
      } else {
        client.print("HTTP/1.1 503 Unavailable\r\n\r\n");
      }
    } else {
      client.print("HTTP/1.1 200 OK\r\n"
                   "Content-Type: multipart/x-mixed-replace; boundary=--fbnd\r\n"
                   "Cache-Control: no-cache\r\nConnection: close\r\n\r\n");
      while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        client.printf("--fbnd\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.print("\r\n");
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    client.stop();
  }
}

// =============================================================================
// Web server — port 80, Core 0
// =============================================================================
WebServer gWeb(80);

void handleRoot() {
  gWeb.sendHeader("Cache-Control", "no-cache");
  gWeb.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleStats() {
  StaticJsonDocument<512> doc;
  xSemaphoreTake(gTelMux, portMAX_DELAY);
  doc["tempC"]        = (float)(int)(gTel.tempC * 10) / 10.0f;
  doc["fanPWM"]       = gTel.fanPWM;
  doc["fanTargetPWM"] = gTel.fanTargetPWM;
  doc["fanRPM"]       = gTel.fanRPM;
  doc["ledPWM"]       = gTel.ledPWM;
  doc["mode"]         = gTel.fanAuto ? "auto" : "manual";
  doc["uptimeMs"]     = gTel.uptimeMs;
  doc["rssi"]         = gTel.rssi;
  doc["freeHeap"]     = gTel.freeHeap;
  doc["stallCount"]   = gTel.stallCount;
  doc["faultLatched"] = gTel.faultLatched;
  if (gTel.healthReady) {
    JsonArray hr = doc.createNestedArray("healthRPM");
    for (int i = 0; i < 4; i++) hr.add(gTel.healthRPM[i]);
  }
  xSemaphoreGive(gTelMux);
  String out; serializeJson(doc, out);
  gWeb.sendHeader("Access-Control-Allow-Origin", "*");
  gWeb.send(200, "application/json", out);
}

void handleEvents() {
  String out = "[";
  xSemaphoreTake(gEventMux, portMAX_DELAY);
  for (uint8_t i = 0; i < gEventCount; i++) {
    uint8_t idx = (gEventHead - gEventCount + i + EVENT_BUF_SIZE) % EVENT_BUF_SIZE;
    if (i) out += ',';
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"tMs\":%lu,\"type\":\"%s\",\"pwm\":%u,\"rpm\":%u}",
             (unsigned long)gEvents[idx].tMs, gEvents[idx].type,
             gEvents[idx].pwm, gEvents[idx].rpm);
    out += buf;
  }
  xSemaphoreGive(gEventMux);
  out += ']';
  gWeb.sendHeader("Access-Control-Allow-Origin", "*");
  gWeb.send(200, "application/json", out);
}

void handleApiFan() {
  if (!gWeb.hasArg("plain")) { gWeb.send(400, "text/plain", "No body"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, gWeb.arg("plain"))) { gWeb.send(400, "text/plain", "Bad JSON"); return; }
  xSemaphoreTake(gSettingsMux, portMAX_DELAY);
  if (doc.containsKey("mode")) {
    const char* m = doc["mode"] | "auto";
    gSettings.fanAuto = (strcmp(m, "auto") == 0);
  }
  if (doc.containsKey("pwm")) {
    gSettings.manualPWM = (uint8_t)constrain(doc["pwm"].as<int>(), 0, 100);
  }
  xSemaphoreGive(gSettingsMux);
  persistSettings();
  gWeb.send(200, "application/json", "{\"ok\":true}");
}

void handleApiLed() {
  if (!gWeb.hasArg("plain")) { gWeb.send(400, "text/plain", "No body"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, gWeb.arg("plain"))) { gWeb.send(400, "text/plain", "Bad JSON"); return; }
  xSemaphoreTake(gSettingsMux, portMAX_DELAY);
  if (doc.containsKey("on"))    gSettings.ledOn    = doc["on"].as<bool>();
  if (doc.containsKey("pwm"))   gSettings.ledPWM   = (uint8_t)constrain(doc["pwm"].as<int>(), 0, 100);
  if (doc.containsKey("pulse")) gSettings.ledPulse = doc["pulse"].as<bool>();
  xSemaphoreGive(gSettingsMux);
  persistSettings();
  gWeb.send(200, "application/json", "{\"ok\":true}");
}

void handleApiSettings() {
  if (!gWeb.hasArg("plain")) { gWeb.send(400, "text/plain", "No body"); return; }
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, gWeb.arg("plain"))) { gWeb.send(400, "text/plain", "Bad JSON"); return; }
  xSemaphoreTake(gSettingsMux, portMAX_DELAY);
  if (doc.containsKey("curve")) {
    JsonArray arr = doc["curve"].as<JsonArray>();
    int i = 0;
    for (JsonObject pt : arr) {
      if (i >= 5) break;
      gSettings.curveTemp[i] = constrain(pt["t"].as<float>(), 0.0f, 120.0f);
      gSettings.curvePWM[i]  = constrain(pt["p"].as<float>(), 0.0f, 100.0f);
      i++;
    }
  }
  if (doc.containsKey("slewRate"))   gSettings.slewRate   = constrain(doc["slewRate"].as<float>(),   0.1f, 50.0f);
  if (doc.containsKey("stallRatio")) gSettings.stallRatio = constrain(doc["stallRatio"].as<float>(), 5.0f, 90.0f);
  if (doc.containsKey("stallDelay")) gSettings.stallDelay = constrain(doc["stallDelay"].as<float>(), 1.0f, 30.0f);
  xSemaphoreGive(gSettingsMux);
  persistSettings();
  gWeb.send(200, "application/json", "{\"ok\":true}");
}

void handleAckFault() {
  xSemaphoreTake(gTelMux, portMAX_DELAY);
  gTel.faultLatched = false;
  gTel.stallCount   = 0;
  xSemaphoreGive(gTelMux);
  gWeb.send(200, "application/json", "{\"ok\":true}");
}

void handleReboot() {
  gWeb.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void handleApiWifi() {
  if (!gWeb.hasArg("plain")) { gWeb.send(400, "text/plain", "No body"); return; }
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, gWeb.arg("plain"))) { gWeb.send(400, "text/plain", "Bad JSON"); return; }
  const char* ssid = doc["ssid"] | "";
  const char* pass = doc["pass"] | "";
  if (strlen(ssid) == 0) { gWeb.send(400, "application/json", "{\"error\":\"SSID required\"}"); return; }
  xSemaphoreTake(gSettingsMux, portMAX_DELAY);
  strlcpy(gSettings.ssid, ssid, sizeof(gSettings.ssid));
  strlcpy(gSettings.pass, pass, sizeof(gSettings.pass));
  xSemaphoreGive(gSettingsMux);
  persistSettings();
  gWeb.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void webTask(void* pv) {
  gWeb.on("/",              HTTP_GET,  handleRoot);
  gWeb.on("/stats",         HTTP_GET,  handleStats);
  gWeb.on("/events",        HTTP_GET,  handleEvents);
  gWeb.on("/api/fan",       HTTP_POST, handleApiFan);
  gWeb.on("/api/led",       HTTP_POST, handleApiLed);
  gWeb.on("/api/settings",  HTTP_POST, handleApiSettings);
  gWeb.on("/api/ack-fault", HTTP_POST, handleAckFault);
  gWeb.on("/api/reboot",    HTTP_POST, handleReboot);
  gWeb.on("/api/wifi",      HTTP_POST, handleApiWifi);
  gWeb.onNotFound([](){
    if (gWeb.method() == HTTP_OPTIONS) {
      gWeb.sendHeader("Access-Control-Allow-Origin",  "*");
      gWeb.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      gWeb.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      gWeb.send(204);
    } else {
      gWeb.send(404, "text/plain", "Not found");
    }
  });
  gWeb.begin();
  LOG(LOG_INFO, "Web :80");
  for (;;) {
    gWeb.handleClient();
    ArduinoOTA.handle();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// =============================================================================
// Fan + LED control task — Core 1, 50 Hz
// =============================================================================
static float    gFanActualPct = 0.0f;
static float    gLedActualPct = 0.0f;
static bool     gKickActive   = false;
static uint32_t gKickEndMs    = 0;
static uint32_t gStallSince   = 0;
static bool     gInRecovery   = false;
static uint32_t gRecentStalls[3] = {0, 0, 0};

// Brief high-speed kick to overcome bearing stiction at low targets.
static float applyKickStart(float targetPct) {
  if (targetPct > 0.0f && targetPct < 20.0f) {
    if (!gKickActive) {
      gKickActive = true;
      gKickEndMs  = millis() + 400;
    }
    if (millis() < gKickEndMs) return 40.0f;
    gKickActive = false;
  } else {
    gKickActive = false;
  }
  return targetPct;
}

// Stall guard: returns the PWM to output (may differ from commanded during recovery).
// Returns -1 if recovery is already in progress — caller should skip to next tick.
static float handleStall(float cmdPct, uint16_t rpm) {
  bool faultLatched;
  xSemaphoreTake(gTelMux, portMAX_DELAY);
  faultLatched = gTel.faultLatched;
  xSemaphoreGive(gTelMux);
  if (faultLatched) return 100.0f;
  if (gInRecovery)  return -1.0f;
  if (cmdPct < 30.0f) { gStallSince = 0; return cmdPct; }

  uint16_t healthRPM[4];
  bool healthReady;
  xSemaphoreTake(gTelMux, portMAX_DELAY);
  healthReady = gTel.healthReady;
  memcpy(healthRPM, gTel.healthRPM, sizeof(healthRPM));
  xSemaphoreGive(gTelMux);

  float stallRatio, stallDelay;
  xSemaphoreTake(gSettingsMux, portMAX_DELAY);
  stallRatio = gSettings.stallRatio;
  stallDelay = gSettings.stallDelay;
  xSemaphoreGive(gSettingsMux);

  if (healthReady) {
    int step = (cmdPct <= 37) ? 0 : (cmdPct <= 62) ? 1 : (cmdPct <= 87) ? 2 : 3;
    uint16_t expRPM = healthRPM[step];
    float minRPM = expRPM * (stallRatio / 100.0f);
    bool badRPM = (rpm == 0 || (expRPM > 0 && rpm < minRPM));

    if (badRPM) {
      if (gStallSince == 0) gStallSince = millis();
      if ((millis() - gStallSince) / 1000.0f >= stallDelay) {
        LOG(LOG_WARN, "Stall! RPM=%u min=%.0f", rpm, minRPM);
        pushEvent("stall", (uint8_t)cmdPct, rpm);

        // Rotate the stall timestamp ring and check for persistent fault
        gRecentStalls[2] = gRecentStalls[1];
        gRecentStalls[1] = gRecentStalls[0];
        gRecentStalls[0] = millis();

        xSemaphoreTake(gTelMux, portMAX_DELAY);
        gTel.stallCount++;
        if (gRecentStalls[2] > 0 && (gRecentStalls[0] - gRecentStalls[2]) < 60000UL) {
          gTel.faultLatched = true;
          pushEvent("fault", (uint8_t)cmdPct, rpm);
          LOG(LOG_ERROR, "Fault latched — 3 stalls in 60s");
        }
        xSemaphoreGive(gTelMux);

        gStallSince = 0;
        gInRecovery = true;

        // Stop → blast → resume (blocking is fine here: we own Core 1)
        setFanDuty(0); gFanActualPct = 0;
        LOG(LOG_INFO, "Recovery: stopping...");
        vTaskDelay(pdMS_TO_TICKS(2000));

        setFanDuty(100); gFanActualPct = 100;
        LOG(LOG_INFO, "Recovery: clearing...");
        vTaskDelay(pdMS_TO_TICKS(3000));

        gInRecovery = false;
        pushEvent("recovery", (uint8_t)cmdPct, 0);
        LOG(LOG_INFO, "Recovery done");
      }
    } else {
      gStallSince = 0;
    }
  }
  return cmdPct;
}

void fanLedTask(void* pv) {
  esp_task_wdt_add(NULL);

  TickType_t xLastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(20);

  float pulsePhase = 0.0f;
  // Angular increment for a 2-second sine period at 50 Hz
  const float pulseInc = (2.0f * (float)M_PI) / (2000.0f / 20.0f);

  for (;;) {
    esp_task_wdt_reset();

    // Snapshot settings atomically so the fan loop is consistent within a tick
    float   slewRate;
    bool    fanAuto, ledOn, ledPulse;
    uint8_t manualPWM, ledPWMtgt;
    xSemaphoreTake(gSettingsMux, portMAX_DELAY);
    slewRate  = gSettings.slewRate;
    fanAuto   = gSettings.fanAuto;
    manualPWM = gSettings.manualPWM;
    ledOn     = gSettings.ledOn;
    ledPWMtgt = gSettings.ledPWM;
    ledPulse  = gSettings.ledPulse;
    xSemaphoreGive(gSettingsMux);

    float tempC = temperatureRead();  // ESP32 internal die sensor
    float targetPct = fanAuto ? tempToPWM(tempC) : (float)manualPWM;
    targetPct = constrain(targetPct, 0.0f, 100.0f);

    uint16_t rpm;
    xSemaphoreTake(gTelMux, portMAX_DELAY);
    rpm = gTel.fanRPM;
    xSemaphoreGive(gTelMux);

    if (!gInRecovery) {
      float eff = handleStall(targetPct, rpm);
      if (eff >= 0.0f) targetPct = eff;
    }

    float kicked = applyKickStart(targetPct);
    gFanActualPct = slew(gFanActualPct, kicked, slewRate);
    setFanDuty(gFanActualPct);

    // LED: pulse mode produces a sine-wave brightness envelope
    float ledTarget = 0.0f;
    if (ledOn) {
      if (ledPulse) {
        ledTarget = (sinf(pulsePhase) * 0.5f + 0.5f) * (float)ledPWMtgt;
        pulsePhase += pulseInc;
        if (pulsePhase > 2.0f * (float)M_PI) pulsePhase -= 2.0f * (float)M_PI;
      } else {
        ledTarget = (float)ledPWMtgt;
      }
    }
    // LED can slew twice as fast as the fan — no mechanical inertia concern
    gLedActualPct = slew(gLedActualPct, ledTarget, slewRate * 2.0f);
    setLedDuty(gLedActualPct);

    xSemaphoreTake(gTelMux, portMAX_DELAY);
    gTel.tempC        = tempC;
    gTel.fanPWM       = (uint8_t)roundf(gFanActualPct);
    gTel.fanTargetPWM = (uint8_t)roundf(targetPct);
    gTel.fanAuto      = fanAuto;
    gTel.ledPWM       = (uint8_t)roundf(gLedActualPct);
    gTel.uptimeMs     = millis();
    gTel.rssi         = WiFi.RSSI();
    gTel.freeHeap     = esp_get_free_heap_size();
    xSemaphoreGive(gTelMux);

    vTaskDelayUntil(&xLastWake, period);
  }
}

// =============================================================================
// Tach sampler task — Core 1, 1 Hz
// =============================================================================
void tachTask(void* pv) {
  uint32_t prevPulses = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t now   = gTachPulses;          // 32-bit read is atomic on Xtensa
    uint32_t delta = now - prevPulses;
    prevPulses = now;
    // 2 pulses/rev, 1 s window: RPM = (delta / 2) * 60 = delta * 30
    uint16_t rpm = (uint16_t)(delta * 30UL);
    xSemaphoreTake(gTelMux, portMAX_DELAY);
    gTel.fanRPM = rpm;
    xSemaphoreGive(gTelMux);
  }
}

// =============================================================================
// Boot-time fan health test
// =============================================================================
void runHealthTest() {
  LOG(LOG_INFO, "Fan health test...");
  const float steps[4] = {25.0f, 50.0f, 75.0f, 100.0f};
  uint16_t results[4]  = {};

  setFanDuty(0); gFanActualPct = 0;
  vTaskDelay(pdMS_TO_TICKS(1000));

  for (int i = 0; i < 4; i++) {
    setFanDuty(steps[i]); gFanActualPct = steps[i];
    LOG(LOG_INFO, "  %.0f%%...", steps[i]);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Average two successive 1-second RPM readings for stability
    uint16_t r1, r2;
    xSemaphoreTake(gTelMux, portMAX_DELAY); r1 = gTel.fanRPM; xSemaphoreGive(gTelMux);
    vTaskDelay(pdMS_TO_TICKS(1000));
    xSemaphoreTake(gTelMux, portMAX_DELAY); r2 = gTel.fanRPM; xSemaphoreGive(gTelMux);
    results[i] = (r1 + r2) / 2;
    LOG(LOG_INFO, "  -> %u RPM", results[i]);
  }

  setFanDuty(0); gFanActualPct = 0;

  xSemaphoreTake(gTelMux, portMAX_DELAY);
  memcpy(gTel.healthRPM, results, sizeof(results));
  gTel.healthReady = true;
  xSemaphoreGive(gTelMux);

  LOG(LOG_INFO, "Health: 25%%=%u 50%%=%u 75%%=%u 100%%=%u",
      results[0], results[1], results[2], results[3]);
}

// =============================================================================
// setup()
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  LOG(LOG_INFO, "FanCam v%s", FW_VERSION);

  // After a watchdog reset the fan may have been spinning; blast it full for
  // 3 s as a fail-safe so the user knows the device restarted abnormally.
  if (esp_reset_reason() == ESP_RST_TASK_WDT || esp_reset_reason() == ESP_RST_WDT) {
    LOG(LOG_WARN, "WDT reset — fan 100%% for 3s");
    ledcSetup(LEDC_FAN_CH, LEDC_FAN_FREQ, LEDC_RES);
    ledcAttachPin(FAN_PIN, LEDC_FAN_CH);
    ledcWrite(LEDC_FAN_CH, 255);
    delay(3000);
  }

  gTelMux      = xSemaphoreCreateMutex();
  gSettingsMux = xSemaphoreCreateMutex();
  gEventMux    = xSemaphoreCreateMutex();

  loadSettings();

  // LEDC — channels 4 and 5 only; channel 0 belongs to the camera driver
  ledcSetup(LEDC_FAN_CH, LEDC_FAN_FREQ, LEDC_RES);
  ledcAttachPin(FAN_PIN, LEDC_FAN_CH);
  ledcWrite(LEDC_FAN_CH, 0);

  ledcSetup(LEDC_LED_CH, LEDC_LED_FREQ, LEDC_RES);
  ledcAttachPin(LED_PIN, LEDC_LED_CH);
  ledcWrite(LEDC_LED_CH, 0);

  pinMode(TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, FALLING);

  initCamera();
  initWifi();

  if (MDNS.begin("fancam")) {
    MDNS.addService("http", "tcp", 80);
    LOG(LOG_INFO, "mDNS: fancam.local");
  }

  ArduinoOTA.setHostname("fancam");
  ArduinoOTA.setPassword("fancam-ota");
  ArduinoOTA.onStart([]()  { LOG(LOG_INFO, "OTA start");    });
  ArduinoOTA.onEnd([]()    { LOG(LOG_INFO, "OTA complete"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    LOG(LOG_DEBUG, "OTA %u%%", (p * 100) / t);
  });
  ArduinoOTA.onError([](ota_error_t e) { LOG(LOG_ERROR, "OTA err %u", e); });
  ArduinoOTA.begin();

  // Core 0: network tasks
  xTaskCreatePinnedToCore(webTask,    "web",    8192, NULL, PRI_WEB,    NULL, 0);
  xTaskCreatePinnedToCore(streamTask, "stream", 8192, NULL, PRI_STREAM, NULL, 0);

  // Core 1: real-time control tasks
  xTaskCreatePinnedToCore(fanLedTask, "fanLed", 4096, NULL, PRI_FAN,   NULL, 1);
  xTaskCreatePinnedToCore(tachTask,   "tach",   2048, NULL, PRI_TACH,  NULL, 1);

  // Health test runs here (Core 1) after tach task has had a chance to start
  vTaskDelay(pdMS_TO_TICKS(500));
  runHealthTest();

  LOG(LOG_INFO, "Ready — %s  http://fancam.local",
      gApMode ? "192.168.4.1" : WiFi.localIP().toString().c_str());
}

// =============================================================================
// loop() — all work is in tasks; just yield to keep the idle WDT happy
// =============================================================================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
