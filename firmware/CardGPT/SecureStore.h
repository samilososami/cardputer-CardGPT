#pragma once

#include <Preferences.h>

#include "AppConfig.h"

namespace cardgpt {

class SecureStore {
 public:
  bool begin();
  void end();

  bool loadWifi(WifiCredentials& wifi);
  bool saveWifi(const WifiCredentials& wifi);
  void clearWifi();

  bool loadOAuth(OAuthCredentials& oauth);
  bool saveOAuth(const OAuthCredentials& oauth);
  void clearOAuth();

  bool loadSettings(UserSettings& settings);
  bool saveSettings(const UserSettings& settings);
  void clearAll();

 private:
  Preferences preferences_;
  uint8_t key_[32] = {};
  bool open_ = false;

  void deriveKey();
  bool putSecret(const char* name, const String& value);
  bool getSecret(const char* name, String& value);
};

}  // namespace cardgpt

