#pragma once

#include <Arduino.h>

#include "AppConfig.h"

namespace cardgpt {

struct DeviceCodeSession {
  String deviceAuthId;
  String userCode;
  uint32_t intervalSeconds = 5;
  uint32_t startedAtMs = 0;
};

enum class DevicePollResult { kPending, kComplete, kError };

using TextDeltaCallback = void (*)(const String& delta, void* context);
using NetworkPumpCallback = void (*)(void* context);

class OpenAIClient {
 public:
  bool syncClock(String& error);

  bool requestDeviceCode(DeviceCodeSession& session, String& error);
  DevicePollResult pollDeviceCode(const DeviceCodeSession& session,
                                  OAuthCredentials& credentials, String& error);

  bool ensureFreshAccessToken(OAuthCredentials& credentials, bool& refreshed, String& error,
                              bool force = false);
  bool revoke(const OAuthCredentials& credentials, String& error);

  bool discoverTextModels(const OAuthCredentials& credentials, ModelCatalog& catalog,
                          String& error);

  bool streamTextResponse(OAuthCredentials& credentials, String& model,
                          const String& reasoningEffort,
                          const ChatMessage* messages, size_t messageCount,
                          TextDeltaCallback onDelta, NetworkPumpCallback pump,
                          void* context, String& error);

  static bool parseJwt(const String& jwt, uint64_t& expiration, String* accountId = nullptr);

 private:
  bool exchangeAuthorizationCode(const String& authorizationCode, const String& codeVerifier,
                                 OAuthCredentials& credentials, String& error);
  bool refreshAccessToken(OAuthCredentials& credentials, String& error);
  bool streamTextAttempt(const OAuthCredentials& credentials, const String& model,
                         const String& reasoningEffort,
                         const ChatMessage* messages, size_t messageCount,
                         TextDeltaCallback onDelta, NetworkPumpCallback pump,
                         void* context, int& httpStatus, String& error);

  static String formEncode(const String& value);
  static void appendJsonString(String& target, const String& value);
  static String makeRequestId();
};

}  // namespace cardgpt

