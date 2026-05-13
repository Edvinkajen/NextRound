#include "wifi_server.h"
#include "pins.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <qrcode.h>

extern "C" {
#include "esp_ota_ops.h"
}

namespace WifiServer {

namespace {
WebServer server(80);

volatile bool  g_done    = false;
volatile bool  g_success = false;
char           g_status[128] = "Idle";
uint32_t       g_totalBytes  = 0;
uint32_t       g_writtenBytes = 0;

char g_ssid[20] = {};
char g_pass[16] = {};
char g_qrText[100] = {};
QRCode    g_qr;
uint8_t   g_qrData[256];
bool      g_qrReady = false;

void setStatus(const char* s) {
  strncpy(g_status, s, sizeof(g_status) - 1);
  g_status[sizeof(g_status) - 1] = '\0';
}

void generateCredentials() {
  const uint32_t r1 = esp_random();
  const uint32_t r2 = esp_random();
  snprintf(g_ssid, sizeof(g_ssid), "NextRound-%02X%02X%02X",
           static_cast<unsigned>(r1 & 0xFF),
           static_cast<unsigned>((r1 >> 8) & 0xFF),
           static_cast<unsigned>((r1 >> 16) & 0xFF));
  snprintf(g_pass, sizeof(g_pass), "%02X%02X%02X%02X",
           static_cast<unsigned>(r2 & 0xFF),
           static_cast<unsigned>((r2 >> 8) & 0xFF),
           static_cast<unsigned>((r2 >> 16) & 0xFF),
           static_cast<unsigned>((r2 >> 24) & 0xFF));
  snprintf(g_qrText, sizeof(g_qrText), "WIFI:T:WPA;S:%s;P:%s;H:true;;",
           g_ssid, g_pass);
}

void drawOledScreen(U8G2& u8g2, int uploadPercent) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 10, "NextRound-");
  u8g2.drawStr(0, 20, g_ssid + 10);
  u8g2.drawStr(0, 32, "192.168.4.1");

  if (uploadPercent > 0) {
    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), "Upload: %d%%", uploadPercent);
    u8g2.drawStr(0, 44, pctBuf);
  } else {
    u8g2.drawStr(0, 44, g_status);
  }

  if (g_qrReady) {
    constexpr uint8_t kScale = 2;
    const uint8_t qrSize = g_qr.size;
    const uint16_t qrPx  = static_cast<uint16_t>(qrSize) * kScale;
    const uint16_t x0 = 128 - qrPx;
    const uint16_t y0 = (64 - qrPx) / 2;
    for (uint8_t y = 0; y < qrSize; ++y) {
      for (uint8_t x = 0; x < qrSize; ++x) {
        if (qrcode_getModule(&g_qr, x, y)) {
          u8g2.drawBox(static_cast<uint8_t>(x0 + x * kScale),
                       static_cast<uint8_t>(y0 + y * kScale),
                       kScale, kScale);
        }
      }
    }
  }

  u8g2.sendBuffer();
}

static const char kDashboardHtml[] PROGMEM =
  "<!doctype html><html lang='en'><head>"
  "<meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>NextRound</title>"
  "<style>"
  "body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:16px;background:#111;color:#eee}"
  "h1{color:#e06080;margin-bottom:4px}h2{color:#ccc;font-size:1rem;margin:20px 0 6px}"
  ".card{background:#222;border-radius:8px;padding:12px 16px;margin:8px 0}"
  ".big{font-size:2.4rem;font-weight:bold;color:#e06080}"
  ".row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #333}"
  ".row:last-child{border:none}"
  "input[type=file]{width:100%;padding:8px;margin:8px 0;background:#333;color:#eee;border:1px solid #555;border-radius:4px}"
  "button{background:#e06080;color:#fff;border:none;padding:10px 18px;border-radius:4px;cursor:pointer;font-size:1rem}"
  "button:hover{background:#c04060}"
  "#status{color:#aaa}"
  "</style></head><body>"
  "<h1>NextRound</h1>"
  "<div class='card' id='device-card'>Loading...</div>"
  "<h2>Last measurement</h2>"
  "<div class='card' id='last-card'>Loading...</div>"
  "<h2>History</h2>"
  "<div class='card' id='hist-card'>Loading...</div>"
  "<h2>Firmware update</h2>"
  "<div class='card'>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='firmware' accept='.bin' required>"
  "<button type='submit'>Upload firmware</button>"
  "</form>"
  "<p>Status: <span id='ota-status'>ready</span></p>"
  "</div>"
  "<script>"
  "function fmt(v){return v==null?'--':v}"
  "fetch('/api/status').then(r=>r.json()).then(d=>{"
  "document.getElementById('device-card').innerHTML="
  "'<div class=row><span>Battery</span><span>'+fmt(d.battery)+'%</span></div>'"
  "+'<div class=row><span>Charging</span><span>'+(d.charging?'Yes':'No')+'</span></div>'"
  "+'<div class=row><span>Temp</span><span>'+fmt(d.temp!=null?d.temp.toFixed(1):null)+'°C</span></div>'"
  "+'<div class=row><span>Humidity</span><span>'+fmt(d.humidity!=null?d.humidity.toFixed(0):null)+'%</span></div>'"
  ";"
  "document.getElementById('last-card').innerHTML="
  "'<div class=row><span>User</span><span>'+fmt(d.lastUser)+'</span></div>'"
  "+'<div class=row><span>Promille</span><span class=big>'+fmt(d.lastMeasurement!=null?d.lastMeasurement.toFixed(2):null)+'</span></div>'"
  ";"
  "});"
  "fetch('/api/history').then(r=>r.json()).then(rows=>{"
  "let h='';rows.forEach(r=>{"
  "const d=new Date(r.ts*1000);"
  "const ds=d.toLocaleDateString()+' '+d.toLocaleTimeString();"
  "h+='<div class=row><span>'+r.user+'</span><span>'+r.promille.toFixed(2)+' ('+ds+')</span></div>'"
  "});"
  "document.getElementById('hist-card').innerHTML=h||'No measurements stored';"
  "});"
  "setInterval(()=>fetch('/status').then(r=>r.text()).then(t=>{document.getElementById('ota-status').innerText=t}).catch(()=>{}),1000);"
  "</script>"
  "</body></html>";

void setupRoutes(const DashboardData& data) {
  server.on("/", HTTP_GET, [&]() {
    server.send_P(200, "text/html", kDashboardHtml);
  });

  server.on("/api/status", HTTP_GET, [&]() {
    JsonDocument doc;
    doc["battery"]         = data.batteryPercent;
    doc["charging"]        = data.charging;
    doc["temp"]            = data.tempC;
    doc["humidity"]        = data.humidity;
    doc["lastMeasurement"] = data.lastMeasurement;
    doc["lastUser"]        = data.lastUser != nullptr ? data.lastUser : "";
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/history", HTTP_GET, []() {
    JsonDocument histDoc;
    bool ok = false;
    if (LittleFS.exists("/users.json")) {
      File f = LittleFS.open("/users.json", "r");
      if (f) {
        ok = (deserializeJson(histDoc, f) == DeserializationError::Ok);
        f.close();
      }
    }

    JsonDocument outDoc;
    JsonArray arr = outDoc.to<JsonArray>();
    if (ok && histDoc["users"].is<JsonObject>()) {
      for (JsonPair kv : histDoc["users"].as<JsonObject>()) {
        JsonObject u = kv.value().as<JsonObject>();
        if (!u["timestamp"].is<long>()) continue;
        JsonObject row = arr.add<JsonObject>();
        row["user"]     = kv.key().c_str();
        row["promille"] = u["measurement"] | 0.0f;
        row["ts"]       = u["timestamp"].as<long>();
      }
    }
    String json;
    serializeJson(outDoc, json);
    server.send(200, "application/json", json);
  });

  server.on("/status", HTTP_GET, []() {
    server.send(200, "text/plain", g_status);
  });

  // OTA: POST finalizer
  server.on("/update", HTTP_POST,
    []() {
      if (Update.hasError()) {
        setStatus("Update failed");
        server.send(500, "text/plain", "Update failed.");
        g_success = false;
        g_done    = true;
        return;
      }
      if (!Update.end(true)) {
        setStatus("Finalise failed");
        server.send(500, "text/plain", "Update end failed.");
        g_success = false;
        g_done    = true;
        return;
      }
      setStatus("Update OK, rebooting...");
      server.send(200, "text/plain", "Update OK. Rebooting...");
      g_success = true;
      g_done    = true;
    },
    // OTA: upload handler
    []() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        g_totalBytes   = 0;
        g_writtenBytes = 0;
        setStatus("Upload started");
        String fn = up.filename;
        fn.toLowerCase();
        if (!fn.endsWith(".bin")) {
          setStatus("Rejected: not a .bin file");
          Update.abort();
          return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          setStatus("Update.begin failed");
          Update.printError(Serial);
        }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.isRunning()) {
          Update.write(up.buf, up.currentSize);
          g_writtenBytes += up.currentSize;
        }
        g_totalBytes = up.totalSize;
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        setStatus("Upload aborted");
        Update.abort();
      }
    }
  );
}
}  // namespace

bool markAppValid() {
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

void enterBlocking(U8G2& display, const DashboardData& data) {
  g_done    = false;
  g_success = false;
  setStatus("Starting...");

  generateCredentials();
  qrcode_initText(&g_qr, g_qrData, 3, ECC_LOW, g_qrText);
  g_qrReady = false;

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(200);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setHostname(g_ssid);

  if (!WiFi.softAP(g_ssid, g_pass, 1, 0, 4)) {
    setStatus("SoftAP failed");
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 20, "WiFi failed");
    display.sendBuffer();
    delay(2000);
    return;
  }
  delay(200);
  g_qrReady = true;

  server.stop();
  setupRoutes(data);
  server.begin();
  setStatus("Ready");

  drawOledScreen(display, 0);

  uint32_t lastUiMs    = 0;
  uint32_t cancelStart = 0;
  bool     cancelHeld  = false;

  while (!g_done) {
    server.handleClient();

    const uint32_t nowMs    = millis();
    const bool     btnHeld  = digitalRead(PIN_BUTTON) == LOW;

    if (btnHeld) {
      if (!cancelHeld) {
        cancelHeld  = true;
        cancelStart = nowMs;
      } else if (nowMs - cancelStart >= 2000) {
        setStatus("Canceled by user");
        g_done = true;
        break;
      }
    } else {
      cancelHeld = false;
    }

    if (nowMs - lastUiMs > 33) {
      lastUiMs = nowMs;
      const int pct = (g_totalBytes > 0)
          ? static_cast<int>((100ULL * g_writtenBytes) / g_totalBytes)
          : 0;
      drawOledScreen(display, pct);
    }

    delay(2);
    yield();
  }

  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  if (g_success) {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(10, 30, "Update complete");
    display.drawStr(10, 44, "Rebooting...");
    display.sendBuffer();
    delay(1000);
    ESP.restart();
  }
}

}  // namespace WifiServer
