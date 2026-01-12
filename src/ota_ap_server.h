#pragma once

#include <Arduino.h>
#include <functional>

class OtaApServer {
 public:
  enum class State : uint8_t {
    Idle,
    Starting,
    Waiting,
    Uploading,
    Success,
    Failed,
    Timeout,
  };

  using StateCallback = std::function<void(State)>;
  using ProgressCallback = std::function<void(uint8_t)>;

  explicit OtaApServer(const char *ssid);

  void begin();
  void tick();
  void end();

  bool isActive() const;
  uint8_t getProgress() const;
  State getState() const;
  bool isTestMode() const;
  const String &getPassword() const;
  const String &getPin() const;
  const String &getLastError() const;

  void setTestMode(bool enabled);
  void onStateChange(StateCallback cb);
  void onProgress(ProgressCallback cb);

 private:
  void setState(State next);
  void updateProgress(size_t written);
  void resetUploadState();
  String makePassword() const;
  String makePin() const;

  const char *ssid_;
  String password_;
  String pin_;
  String lastError_;

  State state_ = State::Idle;
  uint8_t progress_ = 0;
  size_t totalBytes_ = 0;
  size_t bytesWritten_ = 0;
  bool uploadRejected_ = false;
  bool updateFinished_ = false;
  bool updateSuccess_ = false;

  uint32_t startMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t rebootAtMs_ = 0;
  uint32_t lastChannelSwitchMs_ = 0;
  uint8_t channelIndex_ = 0;
  bool testMode_ = false;

  StateCallback stateCallback_;
  ProgressCallback progressCallback_;

  class AsyncWebServer *server_ = nullptr;
};
