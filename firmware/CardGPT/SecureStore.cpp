#include "SecureStore.h"

#include <ArduinoJson.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

#include <memory>

namespace cardgpt {
namespace {

constexpr uint8_t kEnvelopeMagic[] = {'C', 'G', 1};
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kHeaderSize = sizeof(kEnvelopeMagic) + kNonceSize + kTagSize;
constexpr char kKdfContext[] = "CardGPT-NVS-AES256-GCM-v1/2026-10";

}  // namespace

bool SecureStore::begin() {
  if (open_) return true;
  deriveKey();
  open_ = preferences_.begin("cardgpt", false);
  return open_;
}

void SecureStore::end() {
  if (!open_) return;
  preferences_.end();
  open_ = false;
}

void SecureStore::deriveKey() {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(kKdfContext), strlen(kKdfContext));
  mbedtls_sha256_update(&sha, mac, sizeof(mac));
  mbedtls_sha256_finish(&sha, key_);
  mbedtls_sha256_free(&sha);
}

bool SecureStore::putSecret(const char* name, const String& value) {
  if (!open_) return false;
  const size_t plainLength = value.length();
  const size_t envelopeLength = kHeaderSize + plainLength;
  std::unique_ptr<uint8_t[]> envelope(new (std::nothrow) uint8_t[envelopeLength]);
  if (!envelope) return false;

  memcpy(envelope.get(), kEnvelopeMagic, sizeof(kEnvelopeMagic));
  uint8_t* nonce = envelope.get() + sizeof(kEnvelopeMagic);
  uint8_t* tag = nonce + kNonceSize;
  uint8_t* cipher = tag + kTagSize;
  esp_fill_random(nonce, kNonceSize);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key_, 256);
  if (result == 0) {
    result = mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT, plainLength, nonce, kNonceSize,
        reinterpret_cast<const uint8_t*>(name), strlen(name),
        reinterpret_cast<const uint8_t*>(value.c_str()), cipher, kTagSize, tag);
  }
  mbedtls_gcm_free(&gcm);
  if (result != 0) return false;

  return preferences_.putBytes(name, envelope.get(), envelopeLength) == envelopeLength;
}

bool SecureStore::getSecret(const char* name, String& value) {
  value = "";
  if (!open_) return false;
  const size_t envelopeLength = preferences_.getBytesLength(name);
  if (envelopeLength < kHeaderSize) return false;

  std::unique_ptr<uint8_t[]> envelope(new (std::nothrow) uint8_t[envelopeLength]);
  if (!envelope || preferences_.getBytes(name, envelope.get(), envelopeLength) != envelopeLength) {
    return false;
  }
  if (memcmp(envelope.get(), kEnvelopeMagic, sizeof(kEnvelopeMagic)) != 0) return false;

  const uint8_t* nonce = envelope.get() + sizeof(kEnvelopeMagic);
  const uint8_t* tag = nonce + kNonceSize;
  const uint8_t* cipher = tag + kTagSize;
  const size_t cipherLength = envelopeLength - kHeaderSize;
  std::unique_ptr<uint8_t[]> plain(new (std::nothrow) uint8_t[cipherLength + 1]);
  if (!plain) return false;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key_, 256);
  if (result == 0) {
    result = mbedtls_gcm_auth_decrypt(
        &gcm, cipherLength, nonce, kNonceSize,
        reinterpret_cast<const uint8_t*>(name), strlen(name), tag, kTagSize, cipher, plain.get());
  }
  mbedtls_gcm_free(&gcm);
  if (result != 0) return false;

  plain[cipherLength] = 0;
  value = String(reinterpret_cast<char*>(plain.get()), cipherLength);
  return true;
}

bool SecureStore::loadWifi(WifiCredentials& wifi) {
  return getSecret("ssid", wifi.ssid) && getSecret("wifi_pass", wifi.password) && wifi.valid();
}

bool SecureStore::saveWifi(const WifiCredentials& wifi) {
  return putSecret("ssid", wifi.ssid) && putSecret("wifi_pass", wifi.password);
}

void SecureStore::clearWifi() {
  if (!open_) return;
  preferences_.remove("ssid");
  preferences_.remove("wifi_pass");
}

bool SecureStore::loadOAuth(OAuthCredentials& oauth) {
  String expires;
  const bool loaded = getSecret("access", oauth.accessToken) &&
                      getSecret("refresh", oauth.refreshToken) &&
                      getSecret("account", oauth.accountId) && getSecret("expires", expires);
  // The ID token is only needed once to extract the account ID. Keeping it out
  // of NVS saves several entries in the launcher's small shared NVS partition.
  oauth.idToken = "";
  oauth.accessTokenExpiresAt = strtoull(expires.c_str(), nullptr, 10);
  return loaded && oauth.valid();
}

bool SecureStore::saveOAuth(const OAuthCredentials& oauth) {
  const bool saved = putSecret("access", oauth.accessToken) &&
                     putSecret("refresh", oauth.refreshToken) &&
                     putSecret("account", oauth.accountId) &&
                     putSecret("expires", String(oauth.accessTokenExpiresAt));
  // Remove the key written by pre-0.1 development builds, if present.
  preferences_.remove("idtoken");
  return saved;
}

void SecureStore::clearOAuth() {
  if (!open_) return;
  const char* keys[] = {"access", "refresh", "idtoken", "account", "expires"};
  for (const char* key : keys) preferences_.remove(key);
}

bool SecureStore::loadSettings(UserSettings& settings) {
  String json;
  if (!getSecret("settings", json)) return false;
  JsonDocument document;
  if (deserializeJson(document, json)) return false;
  settings.textModel = String(document["model"] | settings.textModel.c_str());
  settings.reasoningEffort =
      String(document["reasoning_effort"] | settings.reasoningEffort.c_str());
  settings.language = String(document["language"] | settings.language.c_str());
  if (settings.language != "es" && settings.language != "en") settings.language = "es";
  return true;
}

bool SecureStore::saveSettings(const UserSettings& settings) {
  JsonDocument document;
  document["model"] = settings.textModel;
  document["reasoning_effort"] = settings.reasoningEffort;
  document["language"] = settings.language;
  String json;
  serializeJson(document, json);
  return putSecret("settings", json);
}

void SecureStore::clearAll() {
  if (open_) preferences_.clear();
}

}  // namespace cardgpt

