#include "OpenAIClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <time.h>

#include <memory>

namespace cardgpt {
namespace {

constexpr uint32_t kTlsTimeoutMs = 15000;
constexpr uint32_t kStreamIdleTimeoutMs = 45000;

struct HttpPostJob {
  HTTPClient* http = nullptr;
  const String* body = nullptr;
  volatile bool done = false;
  int status = -1;
};

void httpPostTask(void* parameter) {
  HttpPostJob* job = static_cast<HttpPostJob*>(parameter);
  job->status = job->http->POST(*job->body);
  __atomic_store_n(&job->done, true, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}

class BlockingReadStream : public Stream {
 public:
  BlockingReadStream(NetworkClient& client, uint32_t idleTimeoutMs)
      : client_(client), idleTimeoutMs_(idleTimeoutMs) {}

  int available() override { return client_.available(); }
  int read() override { return waitForByte(false); }
  int peek() override { return waitForByte(true); }
  void flush() override { client_.flush(); }
  size_t write(uint8_t) override { return 0; }

 private:
  NetworkClient& client_;
  uint32_t idleTimeoutMs_;

  int waitForByte(bool peekOnly) {
    const uint32_t started = millis();
    while (client_.available() <= 0) {
      if (!client_.connected() || millis() - started >= idleTimeoutMs_) return -1;
      delay(1);
    }
    return peekOnly ? client_.peek() : client_.read();
  }
};

void configureHttp(HTTPClient& http) {
  http.setConnectTimeout(kTlsTimeoutMs);
  http.setTimeout(kTlsTimeoutMs);
  http.setUserAgent(String("CardGPT/") + kFirmwareVersion);
}

bool beginHttps(HTTPClient& http, NetworkClientSecure& client, const String& url, String& error) {
  client.setCACert(kOpenAIRootCAs);
  client.setHandshakeTimeout(15);
  if (!http.begin(client, url)) {
    error = "No se pudo iniciar HTTPS";
    return false;
  }
  configureHttp(http);
  return true;
}

String httpError(int code) {
  if (code < 0) return HTTPClient::errorToString(code);
  return String("HTTP ") + code;
}

bool decodeJwtPayload(const String& jwt, String& payload) {
  int first = jwt.indexOf('.');
  int second = first < 0 ? -1 : jwt.indexOf('.', first + 1);
  if (first < 0 || second < 0) return false;

  String encoded = jwt.substring(first + 1, second);
  encoded.replace('-', '+');
  encoded.replace('_', '/');
  while (encoded.length() % 4) encoded += '=';

  const size_t capacity = encoded.length() * 3 / 4 + 4;
  std::unique_ptr<uint8_t[]> decoded(new (std::nothrow) uint8_t[capacity + 1]);
  if (!decoded) return false;
  size_t decodedLength = 0;
  if (mbedtls_base64_decode(decoded.get(), capacity, &decodedLength,
                            reinterpret_cast<const uint8_t*>(encoded.c_str()),
                            encoded.length()) != 0) {
    return false;
  }
  decoded[decodedLength] = 0;
  payload = String(reinterpret_cast<char*>(decoded.get()), decodedLength);
  return true;
}

String jsonErrorMessage(const String& body) {
  JsonDocument document;
  if (deserializeJson(document, body)) return "";
  const char* nested = document["error"]["message"];
  if (nested && *nested) return String(nested);
  const char* direct = document["message"];
  return direct ? String(direct) : String();
}

void addCodexHeaders(HTTPClient& http, const OAuthCredentials& credentials) {
  http.addHeader("Authorization", String("Bearer ") + credentials.accessToken);
  http.addHeader("ChatGPT-Account-ID", credentials.accountId);
  http.addHeader("Originator", "cardgpt");
}

}  // namespace

bool OpenAIClient::syncClock(String& error) {
  error = "";
  const time_t now = time(nullptr);
  if (now > 1700000000) return true;

  configTzTime("UTC0", "time.cloudflare.com", "time.google.com", "pool.ntp.org");
  const uint32_t started = millis();
  while (millis() - started < 15000) {
    if (time(nullptr) > 1700000000) return true;
    delay(100);
  }
  error = "No se pudo sincronizar la hora segura";
  return false;
}

bool OpenAIClient::requestDeviceCode(DeviceCodeSession& session, String& error) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client,
                  "https://auth.openai.com/api/accounts/deviceauth/usercode", error)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"client_id\":\"") + kOAuthClientId + "\"}";
  const int status = http.POST(body);
  const String response = http.getString();
  http.end();
  if (status != 200) {
    error = String("No se pudo crear el codigo: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, response)) {
    error = "Respuesta de login no valida";
    return false;
  }
  session.deviceAuthId = String(document["device_auth_id"] | "");
  const char* userCode = document["user_code"];
  if (!userCode) userCode = document["usercode"];
  session.userCode = userCode ? String(userCode) : String();
  if (document["interval"].is<const char*>()) {
    session.intervalSeconds = String(document["interval"].as<const char*>()).toInt();
  } else {
    session.intervalSeconds = document["interval"] | 5;
  }
  session.intervalSeconds = constrain(session.intervalSeconds, 2U, 30U);
  session.startedAtMs = millis();
  if (session.deviceAuthId.isEmpty() || session.userCode.isEmpty()) {
    error = "El servidor no devolvio un codigo valido";
    return false;
  }
  return true;
}

DevicePollResult OpenAIClient::pollDeviceCode(const DeviceCodeSession& session,
                                               OAuthCredentials& credentials, String& error) {
  if (millis() - session.startedAtMs > 15UL * 60UL * 1000UL) {
    error = "El codigo ha caducado";
    return DevicePollResult::kError;
  }

  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client, "https://auth.openai.com/api/accounts/deviceauth/token", error)) {
    return DevicePollResult::kError;
  }
  http.addHeader("Content-Type", "application/json");
  String body;
  body.reserve(session.deviceAuthId.length() + session.userCode.length() + 64);
  body = "{\"device_auth_id\":";
  appendJsonString(body, session.deviceAuthId);
  body += ",\"user_code\":";
  appendJsonString(body, session.userCode);
  body += '}';

  const int status = http.POST(body);
  const String response = http.getString();
  http.end();
  if (status == 403 || status == 404) return DevicePollResult::kPending;
  if (status != 200) {
    error = String("Error de autorizacion: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return DevicePollResult::kError;
  }

  JsonDocument document;
  if (deserializeJson(document, response)) {
    error = "Respuesta de autorizacion no valida";
    return DevicePollResult::kError;
  }
  const String authorizationCode = String(document["authorization_code"] | "");
  const String verifier = String(document["code_verifier"] | "");
  if (authorizationCode.isEmpty() || verifier.isEmpty()) {
    error = "Faltan datos en la autorizacion";
    return DevicePollResult::kError;
  }
  if (!exchangeAuthorizationCode(authorizationCode, verifier, credentials, error)) {
    return DevicePollResult::kError;
  }
  return DevicePollResult::kComplete;
}

bool OpenAIClient::exchangeAuthorizationCode(const String& authorizationCode,
                                              const String& codeVerifier,
                                              OAuthCredentials& credentials, String& error) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client, "https://auth.openai.com/oauth/token", error)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=authorization_code&client_id=";
  body += formEncode(kOAuthClientId);
  body += "&code=" + formEncode(authorizationCode);
  body += "&redirect_uri=" + formEncode("https://auth.openai.com/deviceauth/callback");
  body += "&code_verifier=" + formEncode(codeVerifier);

  const int status = http.POST(body);
  const String response = http.getString();
  http.end();
  if (status != 200) {
    error = String("No se pudo completar el login: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, response)) {
    error = "Tokens de login no validos";
    return false;
  }
  credentials.idToken = String(document["id_token"] | "");
  credentials.accessToken = String(document["access_token"] | "");
  credentials.refreshToken = String(document["refresh_token"] | "");

  uint64_t idExpiration = 0;
  parseJwt(credentials.idToken, idExpiration, &credentials.accountId);
  parseJwt(credentials.accessToken, credentials.accessTokenExpiresAt, nullptr);
  if (!credentials.valid()) {
    error = "El login no devolvio una cuenta completa";
    return false;
  }
  return true;
}

bool OpenAIClient::ensureFreshAccessToken(OAuthCredentials& credentials, bool& refreshed,
                                           String& error, bool force) {
  refreshed = false;
  if (!credentials.valid()) {
    error = "No hay una sesion valida";
    return false;
  }
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  if (!force && now > 1700000000 && credentials.accessTokenExpiresAt > now + 300) return true;
  if (!refreshAccessToken(credentials, error)) return false;
  refreshed = true;
  return true;
}

bool OpenAIClient::refreshAccessToken(OAuthCredentials& credentials, String& error) {
  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client, "https://auth.openai.com/oauth/token", error)) return false;
  http.addHeader("Content-Type", "application/json");

  String body;
  body.reserve(credentials.refreshToken.length() + 180);
  body = "{\"grant_type\":\"refresh_token\",\"client_id\":\"";
  body += kOAuthClientId;
  body += "\",\"refresh_token\":";
  appendJsonString(body, credentials.refreshToken);
  body += ",\"refresh_reason\":\"proactive_expiry\"}";

  const int status = http.POST(body);
  const String response = http.getString();
  http.end();
  if (status != 200) {
    error = String("No se pudo renovar la sesion: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, response)) {
    error = "Respuesta de renovacion no valida";
    return false;
  }
  if (document["access_token"].is<const char*>()) {
    credentials.accessToken = String(document["access_token"].as<const char*>());
  }
  if (document["refresh_token"].is<const char*>()) {
    credentials.refreshToken = String(document["refresh_token"].as<const char*>());
  }
  if (document["id_token"].is<const char*>()) {
    credentials.idToken = String(document["id_token"].as<const char*>());
    uint64_t ignored = 0;
    String refreshedAccount;
    if (parseJwt(credentials.idToken, ignored, &refreshedAccount) && !refreshedAccount.isEmpty()) {
      credentials.accountId = refreshedAccount;
    }
  }
  parseJwt(credentials.accessToken, credentials.accessTokenExpiresAt, nullptr);
  if (!credentials.valid()) {
    error = "La sesion renovada esta incompleta";
    return false;
  }
  return true;
}

bool OpenAIClient::revoke(const OAuthCredentials& credentials, String& error) {
  if (credentials.refreshToken.isEmpty()) return true;
  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client, "https://auth.openai.com/oauth/revoke", error)) return false;
  http.addHeader("Content-Type", "application/json");
  String body = "{\"token\":";
  appendJsonString(body, credentials.refreshToken);
  body += ",\"token_type_hint\":\"refresh_token\",\"client_id\":\"";
  body += kOAuthClientId;
  body += "\"}";
  const int status = http.POST(body);
  const String response = http.getString();
  http.end();
  if (status < 200 || status >= 300) {
    error = String("No se pudo revocar en el servidor: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }
  return true;
}

bool OpenAIClient::discoverTextModels(const OAuthCredentials& credentials,
                                      ModelCatalog& catalog, String& error) {
  catalog.clear();
  NetworkClientSecure client;
  HTTPClient http;
  String url = String("https://chatgpt.com/backend-api/codex/models?client_version=") +
               kCodexCompatVersion;
  if (!beginHttps(http, client, url, error)) return false;
  http.useHTTP10(true);
  addCodexHeaders(http, credentials);
  const int status = http.GET();
  if (status != 200) {
    const String response = http.getString();
    http.end();
    error = String("No se pudo consultar modelos: ") + httpError(status);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }

  JsonDocument filter;
  filter["models"][0]["slug"] = true;
  filter["models"][0]["display_name"] = true;
  filter["models"][0]["visibility"] = true;
  filter["models"][0]["default_reasoning_level"] = true;
  filter["models"][0]["supported_reasoning_levels"][0]["effort"] = true;

  // NetworkClient can briefly return -1 between TLS records. ArduinoJson sees
  // that as EOF, so wrap it with an idle-aware reader while filtering the large
  // catalog in-place. This keeps peak RAM low and avoids false IncompleteInput.
  JsonDocument document;
  NetworkClient& networkStream = http.getStream();
  BlockingReadStream bodyStream(networkStream, kTlsTimeoutMs);
  const DeserializationError parseError =
      deserializeJson(document, bodyStream, DeserializationOption::Filter(filter));
  http.end();
  if (parseError) {
    error = String("Catalogo de modelos no valido: ") + parseError.c_str();
    return false;
  }

  for (JsonObjectConst source : document["models"].as<JsonArrayConst>()) {
    if (catalog.count >= kMaxAvailableModels) break;
    const String slug = String(source["slug"] | "");
    const String visibility = String(source["visibility"] | "");
    if (slug.isEmpty() || (!visibility.isEmpty() && visibility != "list")) continue;

    ModelOption& option = catalog.models[catalog.count];
    option.slug = slug;
    option.displayName = String(source["display_name"] | slug.c_str());
    option.defaultReasoning = String(source["default_reasoning_level"] | "");
    for (JsonObjectConst level :
         source["supported_reasoning_levels"].as<JsonArrayConst>()) {
      if (option.reasoningLevelCount >= kMaxReasoningLevels) break;
      const String effort = String(level["effort"] | "");
      if (!effort.isEmpty() && !option.supportsReasoning(effort)) {
        option.reasoningLevels[option.reasoningLevelCount++] = effort;
      }
    }
    if (option.reasoningLevelCount == 0) {
      option.reasoningLevels[option.reasoningLevelCount++] =
          option.defaultReasoning.isEmpty() ? "low" : option.defaultReasoning;
    }
    if (option.defaultReasoning.isEmpty() ||
        !option.supportsReasoning(option.defaultReasoning)) {
      option.defaultReasoning = option.reasoningLevels[0];
    }
    ++catalog.count;
  }
  catalog.live = catalog.count > 0;
  if (!catalog.live) error = "Tu plan no devolvio modelos visibles";
  return catalog.live;
}

bool OpenAIClient::streamTextResponse(OAuthCredentials& credentials, String& model,
                                       const String& reasoningEffort,
                                       const ChatMessage* messages, size_t messageCount,
                                       TextDeltaCallback onDelta, NetworkPumpCallback pump,
                                       void* context, String& error) {
  int status = 0;
  return streamTextAttempt(credentials, model, reasoningEffort, messages, messageCount,
                           onDelta, pump, context, status, error);
}

bool OpenAIClient::streamTextAttempt(const OAuthCredentials& credentials, const String& model,
                                      const String& reasoningEffort,
                                      const ChatMessage* messages, size_t messageCount,
                                      TextDeltaCallback onDelta, NetworkPumpCallback pump,
                                      void* context, int& httpStatus, String& error) {
  String body;
  body.reserve(9000);
  body = "{\"model\":";
  appendJsonString(body, model);
  body += ",\"stream\":true,\"instructions\":";
  appendJsonString(body,
                   "Answer clearly and concisely in the language used by the user. "
                   "You are CardGPT running on a small handheld screen. "
                   "Never use emoji, emoticons, pictographs, or decorative Unicode symbols. "
                   "Output plain text only.");
  body += ",\"input\":[";
  for (size_t i = 0; i < messageCount; ++i) {
    if (i) body += ',';
    body += "{\"type\":\"message\",\"role\":\"";
    body += messages[i].fromUser ? "user" : "assistant";
    body += "\",\"content\":[{\"type\":\"";
    body += messages[i].fromUser ? "input_text" : "output_text";
    body += "\",\"text\":";
    appendJsonString(body, messages[i].text);
    body += "}]}";
  }
  body += "] ,\"tools\":[],\"tool_choice\":\"auto\",\"parallel_tool_calls\":false,";
  body += "\"reasoning\":{\"effort\":";
  appendJsonString(body, reasoningEffort.isEmpty() ? String("low") : reasoningEffort);
  body += ",\"summary\":\"auto\"},";
  body += "\"store\":false,\"include\":[]}";

  NetworkClientSecure client;
  HTTPClient http;
  if (!beginHttps(http, client, "https://chatgpt.com/backend-api/codex/responses", error)) {
    httpStatus = -1;
    return false;
  }
  http.useHTTP10(true);
  http.setTimeout(60000);
  addCodexHeaders(http, credentials);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "text/event-stream");
  http.addHeader("x-client-request-id", makeRequestId());

  // POST includes DNS, TCP and TLS setup and can block for several seconds.
  // Run it off the UI task so the thinking dots animate from the first moment.
  HttpPostJob postJob;
  postJob.http = &http;
  postJob.body = &body;
  const BaseType_t postTask =
      xTaskCreate(httpPostTask, "cardgpt-post", 12288, &postJob, 1, nullptr);
  if (postTask == pdPASS) {
    while (!__atomic_load_n(&postJob.done, __ATOMIC_ACQUIRE)) {
      if (pump) pump(context);
      delay(25);
    }
    httpStatus = postJob.status;
  } else {
    if (pump) pump(context);
    httpStatus = http.POST(body);
  }
  if (httpStatus != 200) {
    const String response = http.getString();
    http.end();
    error = httpError(httpStatus);
    const String detail = jsonErrorMessage(response);
    if (!detail.isEmpty()) error += " - " + detail;
    return false;
  }

  NetworkClient& stream = http.getStream();
  String line;
  line.reserve(1800);
  uint32_t lastData = millis();
  bool completed = false;
  while ((http.connected() || stream.available()) && millis() - lastData < kStreamIdleTimeoutMs) {
    bool consumed = false;
    while (stream.available()) {
      consumed = true;
      lastData = millis();
      const char c = static_cast<char>(stream.read());
      if (c != '\n') {
        if (c != '\r' && line.length() < 8192) line += c;
        continue;
      }

      if (line.startsWith("data: ")) {
        const String data = line.substring(6);
        if (data.indexOf("\"type\":\"response.output_text.delta\"") >= 0) {
          JsonDocument document;
          if (!deserializeJson(document, data)) {
            const char* delta = document["delta"];
            if (delta && onDelta) onDelta(String(delta), context);
          }
        } else if (data.indexOf("\"type\":\"response.completed\"") >= 0) {
          completed = true;
        } else if (data.indexOf("\"type\":\"error\"") >= 0) {
          error = jsonErrorMessage(data);
          if (error.isEmpty()) error = "Error durante la respuesta";
          http.end();
          return false;
        }
      }
      line = "";
    }
    if (pump) pump(context);
    if (completed) break;
    if (!consumed) delay(2);
  }
  http.end();
  if (!completed) {
    error = "La respuesta se interrumpio";
    return false;
  }
  return true;
}

bool OpenAIClient::parseJwt(const String& jwt, uint64_t& expiration, String* accountId) {
  String payload;
  if (!decodeJwtPayload(jwt, payload)) return false;
  JsonDocument document;
  if (deserializeJson(document, payload)) return false;
  expiration = document["exp"] | 0ULL;
  if (accountId) {
    const char* account = document["https://api.openai.com/auth"]["chatgpt_account_id"];
    *accountId = account ? String(account) : String();
  }
  return true;
}

String OpenAIClient::formEncode(const String& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3 / 2);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += kHex[c >> 4];
      encoded += kHex[c & 0x0f];
    }
  }
  return encoded;
}

void OpenAIClient::appendJsonString(String& target, const String& value) {
  target += '"';
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    switch (c) {
      case '"': target += "\\\""; break;
      case '\\': target += "\\\\"; break;
      case '\b': target += "\\b"; break;
      case '\f': target += "\\f"; break;
      case '\n': target += "\\n"; break;
      case '\r': target += "\\r"; break;
      case '\t': target += "\\t"; break;
      default:
        if (c < 0x20) {
          target += "\\u00";
          target += kHex[c >> 4];
          target += kHex[c & 0x0f];
        } else {
          target += static_cast<char>(c);
        }
    }
  }
  target += '"';
}

String OpenAIClient::makeRequestId() {
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char output[37];
  snprintf(output, sizeof(output),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
           bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return String(output);
}

}  // namespace cardgpt

