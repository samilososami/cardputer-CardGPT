#pragma once

#include <Arduino.h>

#include "AppConfig.h"

namespace cardgpt {

class CardGPTUI {
 public:
  void begin();
  void setLanguage(const String& language) { english_ = language == "en"; }
  bool pollBattery();
  void showSplash();
  void drawHeader(const String& rightStatus = "");
  void showWelcome(const String& title, const String& body, const String& footer = "");
  void showLoading(const String& title, const String& body, uint8_t frame,
                   const String& rightStatus = "");
  void showWifiList(const String* ssids, const int32_t* rssi, const bool* secured,
                    size_t count, size_t selected);
  void showSsidInput(const String& ssid, const String& error = "");
  void showPassword(const String& ssid, const String& password, bool reveal,
                    const String& error = "", bool allowEmpty = false);
  void showConnecting(const String& ssid, uint8_t frame);
  void showDeviceCode(const String& code, uint32_t secondsRemaining);
  void showLoginAnimation(uint8_t frame, const String& message);
  void showChat(const ChatMessage* messages, size_t count, const String& input,
                const String& modelDisplayName, const String& reasoningEffort,
                bool streaming, int scrollLines = 0, uint8_t thinkingFrame = 0);
  void showSettings(size_t selected, const UserSettings& settings,
                    const ModelCatalog& catalog);
  void showLanguageConfirmation(bool targetEnglish);
  void showAbout();
  void showShortcuts();
  void showError(const String& title, const String& detail, const String& footer = "");
  void showNotice(const String& title, const String& detail, const String& footer = "Enter");

  void drawWrapped(const String& text, int x, int y, int width, uint16_t color,
                   int maxLines = 8, int lineHeight = 11);
  String tailToWidth(const String& text, int width);
  String headToWidth(const String& text, int width);

 private:
  struct DisplayLine {
    String text;
    bool fromUser = false;
    bool role = false;
  };

  void useBodyFont();
  void present();
  const char* tr(const char* spanish, const char* english) const;
  String compactModelName(const String& displayName) const;
  void appendWrapped(DisplayLine* lines, size_t capacity, size_t& count,
                     const String& text, bool fromUser, bool role = false,
                     int width = 226);
  size_t nextUtf8(const String& text, size_t offset) const;

  static constexpr uint32_t kBatterySampleIntervalMs = 1000;
  static constexpr uint32_t kBatteryPublishIntervalMs = 30000;
  static constexpr size_t kBatteryWindowSamples = 30;
  int16_t batterySamples_[kBatteryWindowSamples] = {};
  size_t batterySampleCount_ = 0;
  size_t batterySampleIndex_ = 0;
  int batteryPercent_ = -1;
  uint32_t lastBatterySampleMs_ = 0;
  uint32_t lastBatteryPublishMs_ = 0;
  bool english_ = false;
};

}  // namespace cardgpt

