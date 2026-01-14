#include "OTA.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

extern "C" {
  #include "esp_ota_ops.h"
}

namespace OTA_update {

  static WebServer server(80);
  static volatile bool g_done = false;
  static volatile bool g_success = false;

  static char g_status[128] = "Idle";

  static uint32_t g_total = 0;
  static uint32_t g_written = 0;

  static void setStatus(const char* s) {
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
  }

  const char* lastStatus() { return g_status; }

  // --- UI helpers (U8g2) ---
  static void uiDraw(U8G2& u8g2, const char* line1, const char* line2, int percent) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);

    u8g2.drawStr(0, 12, "NR Update Mode");
    u8g2.drawHLine(0, 14, 128);

    if (line1) u8g2.drawStr(0, 28, line1);
    if (line2) u8g2.drawStr(0, 40, line2);

    // progress bar
    u8g2.drawFrame(0, 52, 128, 10);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int w = (128 * percent) / 100;
    u8g2.drawBox(0, 52, w, 10);

    char pbuf[24];
    snprintf(pbuf, sizeof(pbuf), "%d%%", percent);
    u8g2.drawStr(104, 50, pbuf);

    u8g2.sendBuffer();
  }

  bool markAppValidCancelRollback() {
    // This is safe to call every boot. If rollback isn't enabled/pending,
    // it just returns an error code; we'll treat "OK or not supported" as non-fatal.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) return true;

    // Some cores/targets may return "not supported" depending on config.
    // We'll still return false so you can log it, but don't have to crash.
    return false;
  }

  // Minimal HTML upload page
  static const char* kIndexHtml =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Firmware Update</title></head><body style='font-family:sans-serif;max-width:720px;margin:24px;'>"
    "<h2>Firmware Update</h2>"
    "<p>Select a <b>.bin</b> firmware file and upload.</p>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin' required style='width:100%;padding:8px;'/><br><br>"
    "<button type='submit' style='padding:10px 14px;'>Upload & Update</button>"
    "</form>"
    "<hr><p>Status: <span id='s'>ready</span></p>"
    "<script>"
    "setInterval(()=>fetch('/status').then(r=>r.text()).then(t=>document.getElementById('s').innerText=t).catch(()=>{}),800);"
    "</script>"
    "</body></html>";

  static void setupRoutes() {
    server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", kIndexHtml);
    });

    server.on("/status", HTTP_GET, []() {
      server.send(200, "text/plain", g_status);
    });

    // POST handler finalizer:
    server.on("/update", HTTP_POST,
      []() {
        // Called after upload finished
        if (Update.hasError()) {
          setStatus("Update failed (write/error)");
          server.send(500, "text/plain", "Update failed.");
          g_success = false;
          g_done = true;
          return;
        }

        if (!Update.end(true)) { // true = set boot partition
          setStatus("Update failed (end)");
          server.send(500, "text/plain", "Update end failed.");
          g_success = false;
          g_done = true;
          return;
        }

        setStatus("Update OK, rebooting...");
        server.send(200, "text/plain", "Update OK. Rebooting...");
        g_success = true;
        g_done = true;
      },
      // Upload handler:
      []() {
        HTTPUpload& up = server.upload();

        if (up.status == UPLOAD_FILE_START) {
          g_total = 0;
          g_written = 0;

          setStatus("Upload started");

          // Optional: sanity check extension
          String fn = up.filename;
          fn.toLowerCase();
          if (!fn.endsWith(".bin")) {
            setStatus("Rejected: not a .bin");
            Update.abort();
            return;
          }

          // Start update (flash)
          // UPDATE_SIZE_UNKNOWN lets Update figure out; requires OTA partitions to be large enough.
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            setStatus("Update.begin failed");
            Update.printError(Serial);
            return;
          }

        } else if (up.status == UPLOAD_FILE_WRITE) {
          // Write chunk
          if (Update.isRunning()) {
            size_t written = Update.write(up.buf, up.currentSize);
            if (written != up.currentSize) {
              setStatus("Write mismatch (flash)");
              Update.printError(Serial);
            }
            g_written += up.currentSize;
          }
          // up.totalSize is not always reliable across stacks; but in Arduino-ESP32 it usually is.
          g_total = up.totalSize;

        } else if (up.status == UPLOAD_FILE_END) {
          // Finish happens in POST finalizer (Update.end)
          setStatus("Upload finished, finalizing...");

        } else if (up.status == UPLOAD_FILE_ABORTED) {
          setStatus("Upload aborted");
          Update.abort();
        }
      }
    );
  }

  void enterUpdateModeBlocking(U8G2& u8g2) {
    g_done = false;
    g_success = false;
    setStatus("Starting update mode...");

    // Keep system lean:
    // - You should stop BLE/sensors/logging in your main firmware BEFORE calling this.
    // - Here we only handle WiFi + WebServer + minimal UI.

    // Reset WiFi into clean state
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);

    WiFi.setHostname("NR_Update");
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    bool ap_ok = WiFi.softAP("NR_Update", "123456789", 6, 0, 4, false); // 2.4 GHz, channel 1, open
    delay(200);

    if (!ap_ok) {
      setStatus("SoftAP failed");
      uiDraw(u8g2, "SoftAP failed", "Reboot and retry", 0);
      return;
    }

    IPAddress ip = WiFi.softAPIP();

    // Web server
    server.stop();
    setupRoutes();
    server.begin();
    setStatus("AP up, open 192.168.4.1");

    // Initial screen
    uiDraw(u8g2, "WiFi: NR_Update", "Open: 192.168.4.1", 0);

    uint32_t lastUi = 0;

    while (!g_done) {
      server.handleClient();

      // UI refresh (10 Hz)
      uint32_t now = millis();
      if (now - lastUi > 100) {
        lastUi = now;

        int percent = 0;
        if (g_total > 0) percent = (int)((100ULL * g_written) / g_total);

        // Show status + progress
        uiDraw(u8g2, "WiFi: NR_Update", g_status, percent);
      }

      //delay(2);
      yield();
    }

    // Final screen + reboot if success
    if (g_success) {
      uiDraw(u8g2, "Update complete", "Rebooting...", 100);
      delay(800);
      ESP.restart();
    } else {
      uiDraw(u8g2, "Update failed", g_status, 0);
      // return to caller (still blocking mode ended, caller can decide what to do)
    }
  }

} // namespace NextRoundOTA
