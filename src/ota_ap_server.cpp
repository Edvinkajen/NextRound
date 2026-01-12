#include "ota_ap_server.h"

#include <Update.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

namespace {
constexpr uint32_t kOtaTimeoutMs = 8UL * 60UL * 1000UL;
constexpr uint32_t kOtaChannelCycleMs = 5000;
constexpr uint8_t kOtaChannels[] = {1, 6, 11};

const char kOtaHtml[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>NextRound OTA</title>
  <style>
    body { font-family: sans-serif; background: #f2f2f2; margin: 0; padding: 20px; }
    .card { background: #fff; border-radius: 10px; padding: 20px; max-width: 420px; margin: 0 auto; }
    h1 { font-size: 20px; margin: 0 0 12px; }
    label { display: block; margin-top: 12px; }
    input[type="file"], input[type="text"] { width: 100%; }
    button { margin-top: 16px; padding: 10px 16px; }
    .status { margin-top: 12px; font-size: 14px; color: #333; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Firmware Update</h1>
    <p>Enter the PIN shown on the device, then select the firmware .bin.</p>
    <form id="ota-form" method="POST" action="/update" enctype="multipart/form-data">
      <label>PIN</label>
      <input type="text" name="pin" inputmode="numeric" maxlength="6" required />
      <label>Firmware (.bin)</label>
      <input type="file" name="firmware" required />
      <button type="submit">Upload & Install</button>
    </form>
    <div class="status" id="status">Waiting…</div>
  </div>
  <script>
    const form = document.getElementById('ota-form');
    form.addEventListener('submit', (e) => {
      const pin = form.querySelector('input[name="pin"]').value.trim();
      form.action = '/update?pin=' + encodeURIComponent(pin);
    });
    async function poll() {
      try {
        const res = await fetch('/status');
        const data = await res.json();
        const text = `State: ${data.state} | Progress: ${data.progress}%${data.error ? ' | Error: ' + data.error : ''}`;
        document.getElementById('status').textContent = text;
      } catch (e) {}
      setTimeout(poll, 1000);
    }
    poll();
  </script>
</body>
</html>
)HTML";

const char *stateToString(OtaApServer::State state) {
  switch (state) {
    case OtaApServer::State::Idle:
      return "idle";
    case OtaApServer::State::Starting:
      return "starting";
    case OtaApServer::State::Waiting:
      return "waiting";
    case OtaApServer::State::Uploading:
      return "uploading";
    case OtaApServer::State::Success:
      return "success";
    case OtaApServer::State::Failed:
      return "failed";
    case OtaApServer::State::Timeout:
      return "timeout";
    default:
      return "unknown";
  }
}
}  // namespace

OtaApServer::OtaApServer(const char *ssid) : ssid_(ssid) {}

void OtaApServer::onStateChange(StateCallback cb) {
  stateCallback_ = std::move(cb);
}

void OtaApServer::onProgress(ProgressCallback cb) {
  progressCallback_ = std::move(cb);
}

bool OtaApServer::isActive() const {
  return state_ != State::Idle;
}

uint8_t OtaApServer::getProgress() const {
  return progress_;
}

OtaApServer::State OtaApServer::getState() const {
  return state_;
}

bool OtaApServer::isTestMode() const {
  return testMode_;
}

const String &OtaApServer::getPassword() const {
  return password_;
}

const String &OtaApServer::getPin() const {
  return pin_;
}

const String &OtaApServer::getLastError() const {
  return lastError_;
}

void OtaApServer::setTestMode(bool enabled) {
  testMode_ = enabled;
}

void OtaApServer::begin() {
  if (isActive()) {
    return;
  }
  setState(State::Starting);
  resetUploadState();
  password_ = testMode_ ? String() : makePassword();
  pin_ = makePin();
  lastError_ = "";
  startMs_ = millis();
  lastActivityMs_ = startMs_;
  lastChannelSwitchMs_ = startMs_;
  channelIndex_ = 0;

  Serial.println("OTA: begin");
  Serial.print("OTA: ssid=");
  Serial.println(ssid_);
  Serial.print("OTA: password=");
  Serial.println(password_);
  Serial.print("OTA: pin=");
  Serial.println(pin_);
  Serial.print("OTA: test mode=");
  Serial.println(testMode_ ? "true" : "false");

  WiFi.mode(WIFI_OFF);
  delay(50);
  Serial.print("OTA: mode after WIFI_OFF=");
  Serial.println(WiFi.getMode());
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  const int apChannel = kOtaChannels[channelIndex_];
  const bool apOk =
      testMode_ ? WiFi.softAP(ssid_, nullptr, apChannel, false, 4)
                : WiFi.softAP(ssid_, password_.c_str(), apChannel, false, 4);
  Serial.print("OTA: softAP result=");
  Serial.println(apOk ? "true" : "false");
  if (!apOk) {
    lastError_ = "softAP failed";
    setState(State::Failed);
    return;
  }
  delay(200);
  const IPAddress apIp = WiFi.softAPIP();
  Serial.print("OTA AP IP: ");
  Serial.println(apIp);
  Serial.print("OTA AP channel: ");
  Serial.println(WiFi.channel());
  Serial.print("OTA AP SSID: ");
  Serial.println(WiFi.softAPSSID());
  Serial.print("OTA AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  delete server_;
  server_ = new AsyncWebServer(80);

  server_->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    lastActivityMs_ = millis();
    request->send(200, "text/html", kOtaHtml);
  });

  server_->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    lastActivityMs_ = millis();
    String json = "{";
    json += "\"state\":\"";
    json += stateToString(state_);
    json += "\",\"progress\":";
    json += String(progress_);
    json += ",\"error\":\"";
    json += lastError_;
    json += "\"}";
    request->send(200, "application/json", json);
  });

  server_->on(
      "/update",
      HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        lastActivityMs_ = millis();
        if (!updateFinished_) {
          request->send(400, "text/plain", "Upload incomplete");
          return;
        }
        if (updateSuccess_) {
          request->send(200, "text/plain", "OK, rebooting");
          rebootAtMs_ = millis() + 1000;
        } else {
          request->send(500, "text/plain", lastError_.length() ? lastError_ : "Update failed");
        }
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data,
             size_t len, bool final) {
        (void)filename;
        if (uploadRejected_) {
          return;
        }
        lastActivityMs_ = millis();

        if (index == 0) {
          resetUploadState();
          String pinValue;
          if (request->hasParam("pin")) {
            pinValue = request->getParam("pin")->value();
          } else if (request->hasParam("pin", true)) {
            pinValue = request->getParam("pin", true)->value();
          }
          if (pinValue != pin_) {
            uploadRejected_ = true;
            lastError_ = "PIN mismatch";
            setState(State::Failed);
            return;
          }

          totalBytes_ = request->contentLength();
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            lastError_ = Update.errorString();
            setState(State::Failed);
            uploadRejected_ = true;
            return;
          }
          setState(State::Uploading);
        }

        if (state_ != State::Uploading) {
          return;
        }

        if (len > 0) {
          const size_t written = Update.write(data, len);
          if (written != len) {
            lastError_ = Update.errorString();
            Update.abort();
            setState(State::Failed);
            return;
          }
          updateProgress(written);
        }

        if (final) {
          updateFinished_ = true;
          updateSuccess_ = Update.end(true);
          if (!updateSuccess_) {
            lastError_ = Update.errorString();
            setState(State::Failed);
          } else {
            setState(State::Success);
          }
        }
      });

  server_->begin();
  Serial.println("OTA: webserver started");
  setState(State::Waiting);
}

void OtaApServer::tick() {
  if (!isActive()) {
    return;
  }
  if (testMode_ && state_ == State::Waiting &&
      millis() - lastChannelSwitchMs_ >= kOtaChannelCycleMs) {
    channelIndex_ = static_cast<uint8_t>((channelIndex_ + 1) %
                                         (sizeof(kOtaChannels) / sizeof(kOtaChannels[0])));
    const int apChannel = kOtaChannels[channelIndex_];
    const bool apOk =
        WiFi.softAP(ssid_, testMode_ ? nullptr : password_.c_str(), apChannel, false, 4);
    Serial.print("OTA: channel switch ");
    Serial.print(apChannel);
    Serial.print(" ok=");
    Serial.println(apOk ? "true" : "false");
    lastChannelSwitchMs_ = millis();
  }
  if (rebootAtMs_ > 0 && millis() >= rebootAtMs_) {
    delay(100);
    ESP.restart();
  }
  if (millis() - lastActivityMs_ >= kOtaTimeoutMs) {
    lastError_ = "timeout";
    setState(State::Timeout);
    end();
  }
}

void OtaApServer::end() {
  if (server_) {
    server_->end();
    delete server_;
    server_ = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("OTA: end");
  resetUploadState();
  setState(State::Idle);
}

void OtaApServer::setState(State next) {
  if (state_ == next) {
    return;
  }
  state_ = next;
  if (stateCallback_) {
    stateCallback_(state_);
  }
}

void OtaApServer::updateProgress(size_t written) {
  bytesWritten_ += written;
  if (totalBytes_ > 0) {
    progress_ = static_cast<uint8_t>((bytesWritten_ * 100UL) / totalBytes_);
    if (progress_ > 99 && !updateFinished_) {
      progress_ = 99;
    }
  }
  if (progressCallback_) {
    progressCallback_(progress_);
  }
}

void OtaApServer::resetUploadState() {
  progress_ = 0;
  totalBytes_ = 0;
  bytesWritten_ = 0;
  uploadRejected_ = false;
  updateFinished_ = false;
  updateSuccess_ = false;
}

String OtaApServer::makePassword() const {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const size_t alphabetSize = sizeof(alphabet) - 1;
  char buffer[11];
  for (size_t i = 0; i < sizeof(buffer) - 1; ++i) {
    buffer[i] = alphabet[random(alphabetSize)];
  }
  buffer[sizeof(buffer) - 1] = '\0';
  return String(buffer);
}

String OtaApServer::makePin() const {
  const uint32_t value = random(0, 1000000);
  char buffer[7];
  snprintf(buffer, sizeof(buffer), "%06lu", static_cast<unsigned long>(value));
  return String(buffer);
}
