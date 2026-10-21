#include <M5Cardputer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "AppConfig.h"
#include "CardGPTUI.h"
#include "OpenAIClient.h"
#include "SecureStore.h"

using namespace cardgpt;

namespace {

constexpr uint8_t kAudioCodecAddress = 0x18;
constexpr uint32_t kAudioCodecI2cFrequency = 100000;

uint32_t decodeUtf8Codepoint(const String& text, size_t offset, size_t& length) {
  const uint8_t first = static_cast<uint8_t>(text[offset]);
  length = 1;
  if ((first & 0x80) == 0) return first;
  if ((first & 0xe0) == 0xc0 && offset + 1 < text.length()) {
    length = 2;
    return ((first & 0x1f) << 6) |
           (static_cast<uint8_t>(text[offset + 1]) & 0x3f);
  }
  if ((first & 0xf0) == 0xe0 && offset + 2 < text.length()) {
    length = 3;
    return ((first & 0x0f) << 12) |
           ((static_cast<uint8_t>(text[offset + 1]) & 0x3f) << 6) |
           (static_cast<uint8_t>(text[offset + 2]) & 0x3f);
  }
  if ((first & 0xf8) == 0xf0 && offset + 3 < text.length()) {
    length = 4;
    return ((first & 0x07) << 18) |
           ((static_cast<uint8_t>(text[offset + 1]) & 0x3f) << 12) |
           ((static_cast<uint8_t>(text[offset + 2]) & 0x3f) << 6) |
           (static_cast<uint8_t>(text[offset + 3]) & 0x3f);
  }
  return first;
}

bool isEmojiCodepoint(uint32_t codepoint) {
  return (codepoint >= 0x1f000 && codepoint <= 0x1faff) ||
         (codepoint >= 0x2300 && codepoint <= 0x23ff) ||
         (codepoint >= 0x2600 && codepoint <= 0x27bf) ||
         (codepoint >= 0x2b00 && codepoint <= 0x2bff) ||
         codepoint == 0x00a9 || codepoint == 0x00ae || codepoint == 0x200d ||
         codepoint == 0x203c || codepoint == 0x2049 || codepoint == 0x20e3 ||
         codepoint == 0x2122 || codepoint == 0x2139 || codepoint == 0x3030 ||
         codepoint == 0x303d || codepoint == 0x3297 || codepoint == 0x3299 ||
         codepoint == 0xfe0e || codepoint == 0xfe0f;
}

String stripEmoji(const String& text) {
  String filtered;
  filtered.reserve(text.length());
  size_t offset = 0;
  while (offset < text.length()) {
    size_t length = 1;
    const uint32_t codepoint = decodeUtf8Codepoint(text, offset, length);
    if (!isEmojiCodepoint(codepoint)) {
      for (size_t i = 0; i < length; ++i) filtered += text[offset + i];
    }
    offset += length;
  }
  return filtered;
}

struct ClockCheckJob {
  OpenAIClient* client = nullptr;
  volatile bool done = false;
  bool success = false;
  String error;
};

struct ModelCheckJob {
  OpenAIClient* client = nullptr;
  const OAuthCredentials* credentials = nullptr;
  ModelCatalog* catalog = nullptr;
  volatile bool done = false;
  bool success = false;
  String error;
};

void clockCheckTask(void* parameter) {
  ClockCheckJob* job = static_cast<ClockCheckJob*>(parameter);
  job->success = job->client->syncClock(job->error);
  __atomic_store_n(&job->done, true, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}

void modelCheckTask(void* parameter) {
  ModelCheckJob* job = static_cast<ModelCheckJob*>(parameter);
  job->success = job->client->discoverTextModels(*job->credentials, *job->catalog,
                                                 job->error);
  __atomic_store_n(&job->done, true, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}

// M5Launcher or another OTA app can leave the Cardputer ADV's ES8311 codec
// powered across a software reboot. CardGPT is text-only, so mute and power
// down every audio stage explicitly instead of leaving an idle DAC audible.
bool powerDownAudioCodec() {
  struct CodecWrite {
    uint8_t reg;
    uint8_t value;
  };
  constexpr CodecWrite kPowerDownSequence[] = {
      {0x32, 0x00},  // DAC digital volume: minimum before switching it off.
      {0x12, 0x02},  // Power down the DAC.
      {0x0d, 0xfc},  // Power down the analogue circuitry.
      {0x0e, 0x6a},  // Disable ADC/PGA stages.
      {0x00, 0x00},  // ES8311 CSM power down.
  };

  bool success = true;
  for (const auto& write : kPowerDownSequence) {
    success = M5.In_I2C.writeRegister8(kAudioCodecAddress, write.reg, write.value,
                                       kAudioCodecI2cFrequency) && success;
  }
  return success;
}

}  // namespace

class CardGPTApp {
 public:
  void setup();
  void loop();

 private:
  SecureStore store_;
  OpenAIClient openai_;
  CardGPTUI ui_;
  WifiCredentials wifi_;
  OAuthCredentials oauth_;
  UserSettings settings_;
  ModelCatalog modelCatalog_;
  ChatMessage messages_[kMaxChatMessages];
  size_t messageCount_ = 0;
  String composer_;
  bool streaming_ = false;
  int scrollLines_ = 0;
  uint32_t lastStreamDrawMs_ = 0;
  uint32_t lastThinkingDrawMs_ = 0;
  uint8_t thinkingFrame_ = 0;

  bool connectWifi(const WifiCredentials& wifi, bool showProgress = true);
  bool chooseAndConnectWifi(bool allowCancel);
  bool editWifiSsid(String& ssid);
  bool editWifiPassword(const String& ssid, String& password, bool allowEmpty = false);
  bool ensureNetwork();
  bool ensureClock();
  bool ensureLogin();
  bool runDeviceLogin();
  void discoverModels();
  void buildFallbackCatalog();
  void normalizeModelAndReasoning();
  void cycleModel(int direction);
  void cycleReasoning(int direction);
  String selectedModelDisplayName() const;
  String displayNameFromSlug(const String& slug) const;

  void chatLoop();
  void sendComposer();
  void addMessage(bool fromUser, const String& text);
  void trimConversation();
  void redrawChat();
  void enterSettings();
  void confirmLanguageChange();
  void confirmSignOut();

  static void textDeltaThunk(const String& delta, void* context);
  static void networkPumpThunk(void* context);
  void onTextDelta(const String& delta);
  void onNetworkPump();

  bool waitForEnterOrEsc(bool& enterPressed);
  bool escapePressed(const Keyboard_Class::KeysState& keys) const;
  bool hasCharacter(const Keyboard_Class::KeysState& keys, char target) const;
  bool navigationUp(const Keyboard_Class::KeysState& keys) const;
  bool navigationDown(const Keyboard_Class::KeysState& keys) const;
  bool navigationLeft(const Keyboard_Class::KeysState& keys) const;
  bool navigationRight(const Keyboard_Class::KeysState& keys) const;
  const char* tr(const char* spanish, const char* english) const;
  void removeLastUtf8(String& value);
};

CardGPTApp app;

void setup() { app.setup(); }
void loop() { app.loop(); }

void CardGPTApp::setup() {
  Serial.begin(115200);
  delay(80);
  Serial.printf("\n[CardGPT] boot v%s\n", kFirmwareVersion);
  auto config = M5.config();
  config.internal_spk = false;
  config.internal_mic = false;
  M5Cardputer.begin(config, true);
  Serial.printf("[CardGPT] audio codec: %s\n",
                powerDownAudioCodec() ? "powered down" : "power-down failed");
  Serial.printf("[CardGPT] board=%d free_heap=%u\n",
                static_cast<int>(M5.getBoard()), ESP.getFreeHeap());
  ui_.begin();
  ui_.showSplash();
  Serial.println("[CardGPT] splash");
  delay(1800);

  if (!store_.begin()) {
    Serial.println("[CardGPT] fatal: NVS unavailable");
    ui_.showError("Memoria no disponible", "No se pudo abrir la configuración interna.",
                  "Reinicia el dispositivo");
    while (true) delay(1000);
  }
  store_.loadSettings(settings_);
  ui_.setLanguage(settings_.language);
  store_.loadWifi(wifi_);
  store_.loadOAuth(oauth_);
  Serial.printf("[CardGPT] NVS ready wifi=%s oauth=%s\n",
                wifi_.valid() ? "saved" : "setup-required",
                oauth_.valid() ? "saved" : "login-required");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (!wifi_.valid() || !connectWifi(wifi_)) {
    if (!chooseAndConnectWifi(false)) {
      ui_.showError(tr("WiFi necesario", "Wi-Fi required"),
                    tr("CardGPT necesita una conexión para iniciar.",
                       "CardGPT needs a connection to start."),
                    tr("Reinicia para reintentar", "Restart to try again"));
      while (true) delay(1000);
    }
  }
  if (!ensureClock()) {
    ui_.showError(tr("Hora segura", "Secure time"),
                  tr("No se pudo sincronizar la hora. TLS no se iniciará sin ella.",
                     "Could not synchronize time. TLS cannot start without it."),
                  tr("Enter: reintentar", "Enter: retry"));
    bool enter = false;
    while (!enter || !ensureClock()) waitForEnterOrEsc(enter);
  }
  if (!ensureLogin()) {
    ui_.showError(tr("Login necesario", "Login required"),
                  tr("No se pudo completar el acceso a Codex.",
                     "Could not complete Codex sign-in."),
                  tr("Reinicia para reintentar", "Restart to try again"));
    while (true) delay(1000);
  }
  discoverModels();
  redrawChat();
  Serial.printf("[CardGPT] ready model=%s reasoning=%s free_heap=%u\n",
                settings_.textModel.c_str(), settings_.reasoningEffort.c_str(),
                ESP.getFreeHeap());
}

void CardGPTApp::loop() { chatLoop(); }

bool CardGPTApp::connectWifi(const WifiCredentials& wifi, bool showProgress) {
  if (!wifi.valid()) return false;
  Serial.println("[CardGPT] WiFi: connecting");
  WiFi.disconnect(false, false);
  delay(80);
  WiFi.begin(wifi.ssid.c_str(), wifi.password.isEmpty() ? nullptr : wifi.password.c_str());
  const uint32_t started = millis();
  uint8_t frame = 0;
  while (millis() - started < 18000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[CardGPT] WiFi: connected rssi=%d\n", WiFi.RSSI());
      return true;
    }
    if (showProgress) ui_.showConnecting(wifi.ssid, frame++);
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() &&
        escapePressed(M5Cardputer.Keyboard.keysState())) break;
    delay(220);
  }
  WiFi.disconnect(false, false);
  Serial.printf("[CardGPT] WiFi: failed status=%d\n", static_cast<int>(WiFi.status()));
  return false;
}

bool CardGPTApp::chooseAndConnectWifi(bool allowCancel) {
  constexpr size_t kMaxNetworks = 20;
  String ssids[kMaxNetworks];
  int32_t rssi[kMaxNetworks] = {};
  bool secured[kMaxNetworks] = {};

  while (true) {
    const String scanTitle = tr("Buscando redes WiFi", "Scanning Wi-Fi networks");
    const String scanBody = tr("Espera mientras CardGPT busca redes cercanas.",
                               "Please wait while CardGPT scans nearby networks.");
    uint8_t scanFrame = 0;
    ui_.showLoading(scanTitle, scanBody, scanFrame++, "1/2 WIFI");
    WiFi.scanDelete();
    int found = WiFi.scanNetworks(true, true);
    while (found == WIFI_SCAN_RUNNING) {
      ui_.showLoading(scanTitle, scanBody, scanFrame++, "1/2 WIFI");
      M5Cardputer.update();
      delay(130);
      found = WiFi.scanComplete();
    }
    if (found < 0) found = 0;
    size_t count = 0;
    for (int i = 0; i < found && count < kMaxNetworks; ++i) {
      const String candidate = WiFi.SSID(i);
      if (candidate.isEmpty()) continue;
      bool duplicate = false;
      for (size_t j = 0; j < count; ++j) {
        if (ssids[j] == candidate) {
          duplicate = true;
          if (WiFi.RSSI(i) > rssi[j]) rssi[j] = WiFi.RSSI(i);
          break;
        }
      }
      if (duplicate) continue;
      ssids[count] = candidate;
      rssi[count] = WiFi.RSSI(i);
      secured[count] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      ++count;
    }
    WiFi.scanDelete();

    const size_t total = count + 1;
    size_t selected = 0;
    ui_.showWifiList(ssids, rssi, secured, count, selected);
    while (true) {
      M5Cardputer.update();
      if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        delay(12);
        continue;
      }
      const auto keys = M5Cardputer.Keyboard.keysState();
      if (navigationUp(keys)) {
        selected = selected == 0 ? total - 1 : selected - 1;
        ui_.showWifiList(ssids, rssi, secured, count, selected);
      } else if (navigationDown(keys)) {
        selected = (selected + 1) % total;
        ui_.showWifiList(ssids, rssi, secured, count, selected);
      } else if (escapePressed(keys)) {
        if (allowCancel) return false;
        break;
      } else if (keys.enter) {
        WifiCredentials candidate;
        if (selected == count) {
          if (!editWifiSsid(candidate.ssid) ||
              !editWifiPassword(candidate.ssid, candidate.password, true)) {
            ui_.showWifiList(ssids, rssi, secured, count, selected);
            continue;
          }
        } else {
          candidate.ssid = ssids[selected];
          if (secured[selected] &&
              !editWifiPassword(candidate.ssid, candidate.password)) {
            ui_.showWifiList(ssids, rssi, secured, count, selected);
            continue;
          }
        }
        if (connectWifi(candidate)) {
          wifi_ = candidate;
          store_.saveWifi(wifi_);
          return true;
        }
        ui_.showError(tr("No se pudo conectar", "Could not connect"),
                      tr("Comprueba la contraseña o la cobertura.",
                         "Check the password or signal strength."));
        bool retry = false;
        waitForEnterOrEsc(retry);
        if (retry) {
          if (connectWifi(candidate)) {
            wifi_ = candidate;
            store_.saveWifi(wifi_);
            return true;
          }
        }
        ui_.showWifiList(ssids, rssi, secured, count, selected);
      }
    }
  }
}

bool CardGPTApp::editWifiSsid(String& ssid) {
  ssid = "";
  ui_.showSsidInput(ssid);
  while (true) {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
      delay(12);
      continue;
    }
    const auto keys = M5Cardputer.Keyboard.keysState();
    if (escapePressed(keys)) return false;
    if (keys.enter) {
      if (!ssid.isEmpty()) return true;
      ui_.showSsidInput(ssid, tr("Escribe el nombre de la red", "Enter the network name"));
      continue;
    }
    if (keys.del || keys.backspace) removeLastUtf8(ssid);
    if (!keys.ctrl && !keys.fn && ssid.length() < 32) {
      for (char character : keys.word) {
        if (ssid.length() < 32) ssid += character;
      }
    }
    ui_.showSsidInput(ssid);
  }
}

bool CardGPTApp::editWifiPassword(const String& ssid, String& password, bool allowEmpty) {
  password = "";
  bool reveal = false;
  ui_.showPassword(ssid, password, reveal, "", allowEmpty);
  while (true) {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
      delay(12);
      continue;
    }
    const auto keys = M5Cardputer.Keyboard.keysState();
    if (escapePressed(keys)) return false;
    if (keys.enter) {
      if ((allowEmpty && password.isEmpty()) ||
          (password.length() >= 8 && password.length() <= 63)) return true;
      ui_.showPassword(ssid, password, reveal,
                       allowEmpty
                           ? tr("Vacía o entre 8 y 63 caracteres",
                                "Empty or between 8 and 63 characters")
                           : tr("Debe tener entre 8 y 63 caracteres",
                                "Must be between 8 and 63 characters"),
                       allowEmpty);
      continue;
    }
    if (keys.tab) reveal = !reveal;
    if (keys.del || keys.backspace) removeLastUtf8(password);
    if (!keys.ctrl && password.length() < 63) {
      for (char character : keys.word) password += character;
    }
    ui_.showPassword(ssid, password, reveal, "", allowEmpty);
  }
}

bool CardGPTApp::ensureNetwork() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (wifi_.valid() && connectWifi(wifi_)) return true;
  return chooseAndConnectWifi(true);
}

bool CardGPTApp::ensureClock() {
  const String title = tr("Conexión segura", "Secure connection");
  const String body = tr("Sincronizando la hora para verificar los certificados TLS.",
                         "Synchronizing time to verify TLS certificates.");
  ClockCheckJob job;
  job.client = &openai_;
  ui_.showLoading(title, body, 0, "TLS");
  const BaseType_t created =
      xTaskCreate(clockCheckTask, "cardgpt-tls", 8192, &job, 1, nullptr);
  if (created != pdPASS) {
    job.success = openai_.syncClock(job.error);
    job.done = true;
  }
  uint8_t frame = 1;
  while (!__atomic_load_n(&job.done, __ATOMIC_ACQUIRE)) {
    ui_.showLoading(title, body, frame++, "TLS");
    M5Cardputer.update();
    delay(130);
  }
  const bool synced = job.success;
  Serial.printf("[CardGPT] clock: %s\n", synced ? "synced" : "failed");
  return synced;
}

bool CardGPTApp::ensureLogin() {
  if (oauth_.valid()) {
    bool refreshed = false;
    String error;
    ui_.showWelcome(tr("Comprobando sesión", "Checking session"),
                    tr("Validando tu acceso de Codex…", "Validating your Codex access…"));
    if (openai_.ensureFreshAccessToken(oauth_, refreshed, error)) {
      if (refreshed) store_.saveOAuth(oauth_);
      Serial.printf("[CardGPT] OAuth: ready%s\n", refreshed ? " (refreshed)" : "");
      return true;
    }
    store_.clearOAuth();
    oauth_ = OAuthCredentials();
  }
  return runDeviceLogin();
}

bool CardGPTApp::runDeviceLogin() {
  while (true) {
    DeviceCodeSession session;
    String error;
    ui_.showWelcome(tr("Iniciar sesión", "Sign in"),
                    tr("Generando un código de acceso para Codex…",
                       "Generating a Codex access code…"));
    if (!openai_.requestDeviceCode(session, error)) {
      ui_.showError(tr("No se pudo iniciar", "Could not start"), error);
      bool retry = false;
      if (!waitForEnterOrEsc(retry) || !retry) return false;
      continue;
    }

    uint32_t nextPoll = millis();
    uint32_t lastDraw = 0;
    while (millis() - session.startedAtMs < 15UL * 60UL * 1000UL) {
      const uint32_t elapsed = (millis() - session.startedAtMs) / 1000;
      if (millis() - lastDraw > 900) {
        ui_.showDeviceCode(session.userCode, 900 - std::min<uint32_t>(900, elapsed));
        lastDraw = millis();
      }
      M5Cardputer.update();
      if (M5Cardputer.Keyboard.isChange() &&
          escapePressed(M5Cardputer.Keyboard.keysState())) return false;
      if (static_cast<int32_t>(millis() - nextPoll) >= 0) {
        const DevicePollResult result = openai_.pollDeviceCode(session, oauth_, error);
        if (result == DevicePollResult::kComplete) {
          store_.saveOAuth(oauth_);
          Serial.println("[CardGPT] OAuth: device login complete");
          for (uint8_t frame = 0; frame < 22; ++frame) {
            ui_.showLoginAnimation(frame, tr("Sesión iniciada", "Signed in"));
            delay(65);
          }
          return true;
        }
        if (result == DevicePollResult::kError) {
          ui_.showError(tr("Error de login", "Login error"), error);
          bool retry = false;
          if (!waitForEnterOrEsc(retry) || !retry) return false;
          break;
        }
        nextPoll = millis() + session.intervalSeconds * 1000UL;
      }
      delay(20);
    }
  }
}

void CardGPTApp::discoverModels() {
  const String title = tr("Preparando CardGPT", "Preparing CardGPT");
  const String body = tr("Consultando los modelos disponibles para tu plan…",
                         "Checking the models available on your plan…");
  ModelCheckJob job;
  job.client = &openai_;
  job.credentials = &oauth_;
  job.catalog = &modelCatalog_;
  ui_.showLoading(title, body, 0, tr("PLAN", "PLAN"));
  const BaseType_t created =
      xTaskCreate(modelCheckTask, "cardgpt-plan", 12288, &job, 1, nullptr);
  if (created != pdPASS) {
    job.success = openai_.discoverTextModels(oauth_, modelCatalog_, job.error);
    job.done = true;
  }
  uint8_t frame = 1;
  while (!__atomic_load_n(&job.done, __ATOMIC_ACQUIRE)) {
    ui_.showLoading(title, body, frame++, tr("PLAN", "PLAN"));
    M5Cardputer.update();
    delay(130);
  }
  const String error = job.error;
  if (job.success) {
    normalizeModelAndReasoning();
    store_.saveSettings(settings_);
    Serial.printf("[CardGPT] models: live=%u selected=%s reasoning=%s\n",
                  static_cast<unsigned>(modelCatalog_.count), settings_.textModel.c_str(),
                  settings_.reasoningEffort.c_str());
  } else {
    Serial.printf("[CardGPT] models: discovery failed (%s), local fallback enabled\n",
                  error.c_str());
    buildFallbackCatalog();
    normalizeModelAndReasoning();
  }
}

void CardGPTApp::buildFallbackCatalog() {
  modelCatalog_.clear();
  auto addModel = [this](const String& slug) {
    if (slug.isEmpty() || modelCatalog_.indexOf(slug) >= 0 ||
        modelCatalog_.count >= kMaxAvailableModels) return;
    ModelOption& option = modelCatalog_.models[modelCatalog_.count++];
    option.slug = slug;
    option.displayName = displayNameFromSlug(slug);
    option.defaultReasoning = settings_.reasoningEffort.isEmpty()
                                  ? String("low") : settings_.reasoningEffort;
    option.reasoningLevels[option.reasoningLevelCount++] = option.defaultReasoning;
    if (!option.supportsReasoning("low") &&
        option.reasoningLevelCount < kMaxReasoningLevels) {
      option.reasoningLevels[option.reasoningLevelCount++] = "low";
    }
  };
  addModel(settings_.textModel);
  addModel(kTextModelTerra);
  addModel(kTextModelSol);
}

void CardGPTApp::normalizeModelAndReasoning() {
  if (modelCatalog_.count == 0) buildFallbackCatalog();
  int modelIndex = modelCatalog_.indexOf(settings_.textModel);
  if (modelIndex < 0) modelIndex = modelCatalog_.indexOf(kTextModelTerra);
  if (modelIndex < 0) modelIndex = modelCatalog_.indexOf(kTextModelSol);
  if (modelIndex < 0 && modelCatalog_.count) modelIndex = 0;
  if (modelIndex < 0) return;

  const ModelOption& option = modelCatalog_.models[modelIndex];
  settings_.textModel = option.slug;
  if (!option.supportsReasoning(settings_.reasoningEffort)) {
    settings_.reasoningEffort = option.supportsReasoning(option.defaultReasoning)
                                    ? option.defaultReasoning
                                    : option.reasoningLevels[0];
  }
}

void CardGPTApp::cycleModel(int direction) {
  if (modelCatalog_.count == 0) return;
  int index = modelCatalog_.indexOf(settings_.textModel);
  if (index < 0) index = 0;
  index = (index + direction + static_cast<int>(modelCatalog_.count)) %
          static_cast<int>(modelCatalog_.count);
  settings_.textModel = modelCatalog_.models[index].slug;
  normalizeModelAndReasoning();
  store_.saveSettings(settings_);
}

void CardGPTApp::cycleReasoning(int direction) {
  const ModelOption* option = modelCatalog_.find(settings_.textModel);
  if (!option || option->reasoningLevelCount == 0) return;
  int index = 0;
  for (size_t i = 0; i < option->reasoningLevelCount; ++i) {
    if (option->reasoningLevels[i] == settings_.reasoningEffort) {
      index = static_cast<int>(i);
      break;
    }
  }
  index = (index + direction + static_cast<int>(option->reasoningLevelCount)) %
          static_cast<int>(option->reasoningLevelCount);
  settings_.reasoningEffort = option->reasoningLevels[index];
  store_.saveSettings(settings_);
}

String CardGPTApp::selectedModelDisplayName() const {
  const ModelOption* option = modelCatalog_.find(settings_.textModel);
  return option && !option->displayName.isEmpty()
             ? option->displayName : displayNameFromSlug(settings_.textModel);
}

String CardGPTApp::displayNameFromSlug(const String& slug) const {
  if (slug == kTextModelTerra) return "GPT-5.6-Terra";
  if (slug == kTextModelSol) return "GPT-5.6-Sol";
  String name = slug;
  if (!name.startsWith("gpt-")) return name;
  name.remove(0, 4);
  bool capitalize = false;
  for (size_t i = 0; i < name.length(); ++i) {
    if (name[i] == '-') {
      capitalize = true;
    } else if (capitalize) {
      name.setCharAt(i, static_cast<char>(toupper(static_cast<unsigned char>(name[i]))));
      capitalize = false;
    }
  }
  return String("GPT-") + name;
}

void CardGPTApp::chatLoop() {
  if (ui_.pollBattery()) redrawChat();
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    delay(8);
    return;
  }
  const auto keys = M5Cardputer.Keyboard.keysState();
  // Fn + ` is emitted by the Cardputer keyboard as the actual Escape key.
  if (keys.esc) {
    enterSettings();
    return;
  }
  // Ctrl changes ',' to its shifted value '<' in the Cardputer keyboard map.
  if (keys.ctrl && (hasCharacter(keys, ',') || hasCharacter(keys, '<'))) {
    enterSettings();
    return;
  }
  if (keys.ctrl && hasCharacter(keys, 'l')) {
    messageCount_ = 0;
    composer_ = "";
    scrollLines_ = 0;
    redrawChat();
    return;
  }
  if (keys.up) {
    scrollLines_ = std::min(scrollLines_ + 3, 42);
    redrawChat();
    return;
  }
  if (keys.down) {
    scrollLines_ = std::max(0, scrollLines_ - 3);
    redrawChat();
    return;
  }
  if (keys.enter) {
    sendComposer();
    return;
  }
  if (keys.del || keys.backspace) removeLastUtf8(composer_);
  bool composerChanged = keys.del || keys.backspace;
  if (!keys.ctrl && !keys.fn && composer_.length() < 700) {
    for (char character : keys.word) {
      composer_ += character;
      composerChanged = true;
    }
  }
  // Releasing an arrow while Fn remains held produces a modifier-only keyboard
  // change. Do not snap the chat back to the bottom on that release event.
  if (!composerChanged) return;
  scrollLines_ = 0;
  redrawChat();
}

void CardGPTApp::sendComposer() {
  composer_.trim();
  if (composer_.isEmpty() || streaming_) return;
  if (!ensureNetwork()) {
    ui_.showError(tr("Sin conexión", "No connection"),
                  tr("No fue posible reconectar a WiFi.",
                     "Could not reconnect to Wi-Fi."));
    bool ignored = false;
    waitForEnterOrEsc(ignored);
    redrawChat();
    return;
  }

  bool refreshed = false;
  String error;
  if (!openai_.ensureFreshAccessToken(oauth_, refreshed, error)) {
    ui_.showError(tr("Sesión caducada", "Session expired"), error,
                  tr("Enter: volver a iniciar sesión", "Enter: sign in again"));
    bool enter = false;
    waitForEnterOrEsc(enter);
    if (enter && runDeviceLogin()) discoverModels();
    redrawChat();
    return;
  }
  if (refreshed) store_.saveOAuth(oauth_);

  const String prompt = composer_;
  composer_ = "";
  addMessage(true, prompt);
  addMessage(false, "");
  streaming_ = true;
  scrollLines_ = 0;
  thinkingFrame_ = 0;
  lastThinkingDrawMs_ = 0;
  lastStreamDrawMs_ = 0;
  redrawChat();

  String requestError;
  // The final empty assistant message is a local streaming target, not history.
  const size_t requestMessageCount = messageCount_ > 0 ? messageCount_ - 1 : 0;
  bool success = openai_.streamTextResponse(oauth_, settings_.textModel,
                                             settings_.reasoningEffort, messages_,
                                             requestMessageCount, textDeltaThunk, networkPumpThunk,
                                             this, requestError);
  if (!success && messages_[messageCount_ - 1].text.isEmpty() &&
      requestError.indexOf("HTTP 401") >= 0) {
    bool forcedRefresh = false;
    String refreshError;
    if (openai_.ensureFreshAccessToken(oauth_, forcedRefresh, refreshError, true)) {
      store_.saveOAuth(oauth_);
      success = openai_.streamTextResponse(oauth_, settings_.textModel,
                                            settings_.reasoningEffort, messages_,
                                            requestMessageCount, textDeltaThunk, networkPumpThunk,
                                            this, requestError);
    }
  }
  streaming_ = false;
  thinkingFrame_ = 0;
  if (!success && messages_[messageCount_ - 1].text.isEmpty()) {
    messages_[messageCount_ - 1].text =
        String(tr("No pude responder: ", "I couldn’t respond: ")) + requestError;
  }
  if (messages_[messageCount_ - 1].text.isEmpty()) {
    messages_[messageCount_ - 1].text =
        tr("La respuesta llegó vacía.", "The response was empty.");
  }
  store_.saveSettings(settings_);
  trimConversation();
  redrawChat();
}

void CardGPTApp::addMessage(bool fromUser, const String& text) {
  if (messageCount_ == kMaxChatMessages) {
    for (size_t i = 1; i < messageCount_; ++i) messages_[i - 1] = std::move(messages_[i]);
    --messageCount_;
  }
  messages_[messageCount_].fromUser = fromUser;
  messages_[messageCount_].text = text;
  if (messages_[messageCount_].text.length() > kMaxMessageChars) {
    messages_[messageCount_].text.remove(0,
        messages_[messageCount_].text.length() - kMaxMessageChars);
  }
  ++messageCount_;
  trimConversation();
}

void CardGPTApp::trimConversation() {
  size_t total = 0;
  for (size_t i = 0; i < messageCount_; ++i) total += messages_[i].text.length();
  while (messageCount_ > 2 && total > kMaxConversationChars) {
    const size_t removeCount = (messageCount_ >= 2 && messages_[0].fromUser &&
                                !messages_[1].fromUser) ? 2 : 1;
    for (size_t i = removeCount; i < messageCount_; ++i) {
      messages_[i - removeCount] = std::move(messages_[i]);
    }
    messageCount_ -= removeCount;
    total = 0;
    for (size_t i = 0; i < messageCount_; ++i) total += messages_[i].text.length();
  }
}

void CardGPTApp::redrawChat() {
  ui_.showChat(messages_, messageCount_, composer_, selectedModelDisplayName(),
               settings_.reasoningEffort, streaming_, scrollLines_, thinkingFrame_);
}

void CardGPTApp::enterSettings() {
  size_t selected = 0;
  ui_.showSettings(selected, settings_, modelCatalog_);
  while (true) {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
      delay(12);
      continue;
    }
    const auto keys = M5Cardputer.Keyboard.keysState();
    if (escapePressed(keys)) break;
    if (navigationUp(keys)) selected = selected == 0 ? 7 : selected - 1;
    if (navigationDown(keys)) selected = (selected + 1) % 8;
    if (navigationLeft(keys)) {
      if (selected == 0) {
        cycleModel(-1);
      } else if (selected == 1) {
        cycleReasoning(-1);
      } else if (selected == 2) {
        confirmLanguageChange();
      }
    }
    if (navigationRight(keys)) {
      if (selected == 0) {
        cycleModel(1);
      } else if (selected == 1) {
        cycleReasoning(1);
      } else if (selected == 2) {
        confirmLanguageChange();
      }
    }
    if (keys.enter) {
      if (selected == 0) {
        cycleModel(1);
      } else if (selected == 1) {
        cycleReasoning(1);
      } else if (selected == 2) {
        confirmLanguageChange();
      } else if (selected == 3) {
        chooseAndConnectWifi(true);
        ensureClock();
      } else if (selected == 4) {
        messageCount_ = 0;
        composer_ = "";
      } else if (selected == 5) {
        confirmSignOut();
      } else if (selected == 6) {
        ui_.showAbout();
        bool done = false;
        while (!done) {
          M5Cardputer.update();
          if (M5Cardputer.Keyboard.isChange() &&
              escapePressed(M5Cardputer.Keyboard.keysState())) done = true;
          delay(12);
        }
      } else if (selected == 7) {
        ui_.showShortcuts();
        bool done = false;
        while (!done) {
          M5Cardputer.update();
          if (M5Cardputer.Keyboard.isChange() &&
              escapePressed(M5Cardputer.Keyboard.keysState())) done = true;
          delay(12);
        }
      }
    }
    ui_.showSettings(selected, settings_, modelCatalog_);
  }
  store_.saveSettings(settings_);
  redrawChat();
}

void CardGPTApp::confirmLanguageChange() {
  const bool targetEnglish = !settings_.english();
  ui_.showLanguageConfirmation(targetEnglish);
  bool confirm = false;
  if (!waitForEnterOrEsc(confirm) || !confirm) return;

  settings_.language = targetEnglish ? "en" : "es";
  store_.saveSettings(settings_);
  ui_.setLanguage(settings_.language);
  ui_.showNotice(tr("Idioma guardado", "Language saved"),
                 tr("CardGPT se reiniciará para aplicar Español.",
                    "CardGPT will restart to apply English."),
                 tr("Reiniciando…", "Restarting…"));
  delay(1100);
  ESP.restart();
  while (true) delay(1000);
}

void CardGPTApp::confirmSignOut() {
  ui_.showNotice(tr("Cerrar sesión", "Sign out"),
                 tr("Se revocará el acceso de CardGPT y se borrarán los tokens del dispositivo.",
                    "CardGPT access will be revoked and the device tokens will be erased."),
                 tr("Enter: confirmar   `: cancelar", "Enter: confirm   `: cancel"));
  bool confirm = false;
  if (!waitForEnterOrEsc(confirm) || !confirm) return;
  String revokeError;
  openai_.revoke(oauth_, revokeError);
  store_.clearOAuth();
  oauth_ = OAuthCredentials();
  messageCount_ = 0;
  if (!runDeviceLogin()) return;
  discoverModels();
}

void CardGPTApp::textDeltaThunk(const String& delta, void* context) {
  static_cast<CardGPTApp*>(context)->onTextDelta(delta);
}

void CardGPTApp::networkPumpThunk(void* context) {
  static_cast<CardGPTApp*>(context)->onNetworkPump();
}

void CardGPTApp::onNetworkPump() {
  M5Cardputer.update();
  bool needsRedraw = ui_.pollBattery();
  if (streaming_ && messageCount_ > 0 &&
      messages_[messageCount_ - 1].text.isEmpty() &&
      millis() - lastThinkingDrawMs_ >= 140) {
    thinkingFrame_ = (thinkingFrame_ + 1) % 3;
    needsRedraw = true;
    lastThinkingDrawMs_ = millis();
  }
  if (needsRedraw) redrawChat();
  delay(0);
}

void CardGPTApp::onTextDelta(const String& delta) {
  if (messageCount_ == 0) return;
  const String filtered = stripEmoji(delta);
  if (filtered.isEmpty()) return;
  messages_[messageCount_ - 1].text += filtered;
  if (messages_[messageCount_ - 1].text.length() > kMaxMessageChars) {
    messages_[messageCount_ - 1].text.remove(
        0, messages_[messageCount_ - 1].text.length() - kMaxMessageChars);
  }
  if (millis() - lastStreamDrawMs_ > 160) {
    redrawChat();
    lastStreamDrawMs_ = millis();
  }
}

bool CardGPTApp::waitForEnterOrEsc(bool& enterPressed) {
  enterPressed = false;
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      const auto keys = M5Cardputer.Keyboard.keysState();
      if (keys.enter) {
        enterPressed = true;
        return true;
      }
      if (escapePressed(keys)) return true;
    }
    delay(12);
  }
}

bool CardGPTApp::escapePressed(const Keyboard_Class::KeysState& keys) const {
  return keys.esc || hasCharacter(keys, '`') || hasCharacter(keys, '~');
}

bool CardGPTApp::hasCharacter(const Keyboard_Class::KeysState& keys, char target) const {
  for (char character : keys.word) {
    if (tolower(static_cast<unsigned char>(character)) ==
        tolower(static_cast<unsigned char>(target))) return true;
  }
  return false;
}

// The four arrow legends share the ; , . / keys on Cardputer hardware. In
// navigation-only screens those physical keys act as arrows without Fn, while
// text-entry screens keep their punctuation behaviour.
bool CardGPTApp::navigationUp(const Keyboard_Class::KeysState& keys) const {
  return keys.up || hasCharacter(keys, ';') || hasCharacter(keys, ':');
}

bool CardGPTApp::navigationDown(const Keyboard_Class::KeysState& keys) const {
  return keys.down || hasCharacter(keys, '.') || hasCharacter(keys, '>');
}

bool CardGPTApp::navigationLeft(const Keyboard_Class::KeysState& keys) const {
  return keys.left || hasCharacter(keys, ',') || hasCharacter(keys, '<');
}

bool CardGPTApp::navigationRight(const Keyboard_Class::KeysState& keys) const {
  return keys.right || hasCharacter(keys, '/') || hasCharacter(keys, '?');
}

const char* CardGPTApp::tr(const char* spanish, const char* english) const {
  return settings_.english() ? english : spanish;
}

void CardGPTApp::removeLastUtf8(String& value) {
  if (value.isEmpty()) return;
  int index = value.length() - 1;
  while (index > 0 && (static_cast<uint8_t>(value[index]) & 0xc0) == 0x80) --index;
  value.remove(index);
}
