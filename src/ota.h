#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

namespace NextRoundOTA {

  // Call this once early in normal firmware *after* your self-test passes.
  // This confirms the currently running app as "valid" and cancels rollback.
  // (If you never call this after an OTA update, bootloader may roll back.)
  bool markAppValidCancelRollback();

  // Enters a blocking update mode:
  // - Starts SoftAP "NR_Update"
  // - Hosts a tiny upload page at http://192.168.4.1
  // - Accepts a firmware .bin and flashes it using Update.h
  // - Reboots on success
  //
  // Returns only if user aborts (optional) or on fatal error (won't reboot).
  void enterUpdateModeBlocking(U8G2& u8g2);

  // Optional: if you want to show a reason why update mode ended
  const char* lastStatus();
}
