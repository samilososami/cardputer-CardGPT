#include "CardGPTUI.h"

#include <M5Cardputer.h>

#include <algorithm>

#include "SplashBitmap.h"

namespace cardgpt {
namespace {

M5Canvas display(&M5Cardputer.Display);

// Kept as an explicit coverage string so release validation can verify that
// the selected UI font contains every Spanish glyph CardGPT may render.
[[maybe_unused]] constexpr char kSpanishGlyphCoverage[] =
    "¿¡ áéíóú ÁÉÍÓÚ ñÑ üÜ";

void drawLightning(int x, int y, uint16_t color) {
  display.fillTriangle(x + 3, y, x, y + 5, x + 3, y + 5, color);
  display.fillTriangle(x + 2, y + 4, x + 5, y + 4, x + 1, y + 9, color);
}

}  // namespace

void CardGPTUI::begin() {
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(150);
  M5Cardputer.Display.fillScreen(kColorBackground);

  // The original v0.1.1 palette is RGB565. A 16-bit full-frame buffer preserves
  // #080c08, #191c19 and #292d29 exactly while still pushing each completed
  // frame in one transfer to avoid visible clear/draw flicker.
  display.setColorDepth(16);
  if (!display.createSprite(240, 135)) {
    Serial.println("[CardGPT] UI: no se pudo reservar el framebuffer");
  }
  display.setTextWrap(false);
  display.setAttribute(lgfx::utf8_switch, 1);
  display.setTextDatum(top_left);
  display.fillScreen(kColorBackground);
  useBodyFont();
  pollBattery();
  present();
}

bool CardGPTUI::pollBattery() {
  const uint32_t now = millis();
  if (batterySampleCount_ > 0 &&
      now - lastBatterySampleMs_ < kBatterySampleIntervalMs) {
    return false;
  }
  lastBatterySampleMs_ = now;

  const int rawBattery = M5.Power.getBatteryLevel();
  if (rawBattery < 0) return false;
  const int sample = constrain(rawBattery, 0, 100);
  batterySamples_[batterySampleIndex_] = sample;
  batterySampleIndex_ = (batterySampleIndex_ + 1) % kBatteryWindowSamples;
  if (batterySampleCount_ < kBatteryWindowSamples) ++batterySampleCount_;

  // Seed the header immediately, then only publish a 30-second moving average
  // every 30 seconds. Frequent framebuffer redraws never query or alter it.
  if (batteryPercent_ < 0) {
    batteryPercent_ = sample;
    lastBatteryPublishMs_ = now;
    return true;
  }
  if (now - lastBatteryPublishMs_ < kBatteryPublishIntervalMs) return false;

  int sum = 0;
  for (size_t i = 0; i < batterySampleCount_; ++i) sum += batterySamples_[i];
  const int averaged = (sum + static_cast<int>(batterySampleCount_ / 2)) /
                       static_cast<int>(batterySampleCount_);
  lastBatteryPublishMs_ = now;
  if (averaged == batteryPercent_) return false;
  batteryPercent_ = averaged;
  return true;
}

void CardGPTUI::useBodyFont() {
  // efontJA contains the complete Latin repertoire used by Spanish, unlike
  // efontCN and the ASCII-only bundled DejaVu fonts.
  display.setFont(&fonts::efontJA_12);
  display.setTextSize(1);
}

void CardGPTUI::present() { display.pushSprite(0, 0); }

const char* CardGPTUI::tr(const char* spanish, const char* english) const {
  return english_ ? english : spanish;
}

void CardGPTUI::showSplash() {
  display.fillScreen(TFT_WHITE);
  display.drawXBitmap(0, 0, CARDGPT_SPLASH_XBM, CARDGPT_SPLASH_WIDTH,
                      CARDGPT_SPLASH_HEIGHT, TFT_BLACK, TFT_WHITE);
  present();
}

void CardGPTUI::drawHeader(const String& rightStatus) {
  pollBattery();
  display.fillRect(0, 0, 240, 19, kColorBackground);
  display.drawXBitmap(3, 2, CARDGPT_LOGO_XBM, CARDGPT_LOGO_WIDTH,
                      CARDGPT_LOGO_HEIGHT, kColorText, kColorBackground);
  display.setFont(&fonts::DejaVu12);
  display.setTextColor(kColorText, kColorBackground);
  display.drawString("CardGPT", 21, 2);

  useBodyFont();
  const String battery = batteryPercent_ < 0 ? String("--%")
                                             : String(batteryPercent_) + "%";
  const int batteryWidth = display.textWidth(battery);
  const int groupWidth = 5 + 2 + batteryWidth;
  const int groupX = 120 - groupWidth / 2;
  drawLightning(groupX, 4, kColorAccentSoft);
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(battery, groupX + 7, 2);

  if (!rightStatus.isEmpty()) {
    display.setTextColor(kColorMuted, kColorBackground);
    const String fitted = headToWidth(rightStatus, 90);
    const int width = display.textWidth(fitted);
    display.drawString(fitted, max(146, 237 - width), 2);
  }
  display.drawFastHLine(0, 18, 240, 0x2104);
  useBodyFont();
}

void CardGPTUI::showWelcome(const String& title, const String& body, const String& footer) {
  display.fillScreen(kColorBackground);
  drawHeader(tr("CONFIGURACIÓN", "SETUP"));
  useBodyFont();
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(title, 8, 27);
  useBodyFont();
  drawWrapped(body, 8, 48, 224, kColorText, 4, 14);
  if (!footer.isEmpty()) {
    display.fillRoundRect(7, 110, 226, 20, 6, kColorPanel);
    display.setTextColor(kColorMuted, kColorPanel);
    display.drawCentreString(footer, 120, 113);
  }
  present();
}

void CardGPTUI::showLoading(const String& title, const String& body, uint8_t frame,
                            const String& rightStatus) {
  display.fillScreen(kColorBackground);
  drawHeader(rightStatus);
  useBodyFont();
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(title, 8, 27);
  drawWrapped(body, 8, 47, 224, kColorText, 3, 14);

  const int active = frame % 3;
  for (int i = 0; i < 3; ++i) {
    const int y = i == active ? 91 : 96;
    display.fillCircle(108 + i * 12, y, 3,
                       i == active ? kColorText : kColorMuted);
  }
  present();
}

void CardGPTUI::showWifiList(const String* ssids, const int32_t* rssi, const bool* secured,
                             size_t count, size_t selected) {
  display.fillScreen(kColorBackground);
  drawHeader("1/2 WIFI");
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(tr("Selecciona o añade una red", "Choose or add a network"), 7, 21);

  const size_t visible = 6;
  const size_t total = count + 1;
  size_t first = selected >= visible ? selected - visible + 1 : 0;
  for (size_t row = 0; row < visible && first + row < total; ++row) {
    const size_t index = first + row;
    const int y = 37 + row * 15;
    if (index == selected) display.fillRoundRect(4, y - 1, 232, 15, 4, kColorSelection);
    const uint16_t background = index == selected ? kColorSelection : kColorBackground;
    display.setTextColor(index == selected ? kColorText : 0xce59, background);
    if (index == count) {
      display.drawString(tr("+  Añadir red WiFi", "+  Add Wi-Fi"), 9, y);
      continue;
    }
    String label = ssids[index];
    while (display.textWidth(label) > 165 && label.length() > 2) label.remove(label.length() - 1);
    display.drawString(label, 9, y);
    display.drawRightString(String(rssi[index]) + "dB", 219, y);
    display.drawString(secured[index] ? "*" : "o", 226, y);
  }
  present();
}

void CardGPTUI::showSsidInput(const String& ssid, const String& error) {
  display.fillScreen(kColorBackground);
  drawHeader("1/2 WIFI");
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(tr("Añadir red oculta", "Add hidden network"), 8, 27);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawString("SSID", 8, 52);
  display.fillRoundRect(7, 68, 226, 24, 6, kColorPanel);
  display.setTextColor(kColorText, kColorPanel);
  display.drawString(tailToWidth(ssid, 205) + "_", 14, 73);
  if (!error.isEmpty()) {
    display.setTextColor(kColorDanger, kColorBackground);
    display.drawCentreString(error, 120, 97);
  }
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(tr("Enter: continuar  `: cancelar", "Enter: continue  `: cancel"),
                           120, 118);
  present();
}

void CardGPTUI::showPassword(const String& ssid, const String& password, bool reveal,
                             const String& error, bool allowEmpty) {
  display.fillScreen(kColorBackground);
  drawHeader("1/2 WIFI");
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(tr("Conectar a:", "Connect to:"), 8, 25);
  display.setTextColor(0xd69a, kColorBackground);
  display.drawString(ssid, 8, 40);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawString(allowEmpty
                         ? tr("Contraseña (vacía si abierta)",
                              "Password (empty if open)")
                         : tr("Contraseña WPA", "WPA password"),
                     8, 58);
  display.fillRoundRect(7, 74, 226, 24, 6, kColorPanel);
  String shown;
  if (reveal) {
    shown = password;
  } else {
    shown.reserve(password.length());
    for (size_t i = 0; i < password.length(); ++i) shown += '*';
  }
  shown = tailToWidth(shown, 205);
  display.setTextColor(kColorText, kColorPanel);
  display.drawString(shown + "_", 14, 79);
  if (!error.isEmpty()) {
    display.setTextColor(kColorDanger, kColorBackground);
    display.drawCentreString(error, 120, 100);
  }
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(tr("Enter: conectar  Tab: mostrar", "Enter: connect  Tab: show"),
                           120, 118);
  present();
}

void CardGPTUI::showConnecting(const String& ssid, uint8_t frame) {
  display.fillScreen(kColorBackground);
  drawHeader("WIFI");
  display.setTextColor(kColorText, kColorBackground);
  display.drawCentreString(tr("Conectando", "Connecting"), 120, 43);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(ssid, 120, 61);
  const char* dots[] = {".", "..", "...", "...."};
  display.setFont(&fonts::DejaVu18);
  display.setTextColor(kColorText, kColorBackground);
  display.drawCentreString(dots[frame % 4], 120, 78);
  useBodyFont();
  present();
}

void CardGPTUI::showDeviceCode(const String& code, uint32_t secondsRemaining) {
  display.fillScreen(kColorBackground);
  drawHeader("2/2 LOGIN");

  const int quiet = 8;
  const int box = CARDGPT_QR_WIDTH + quiet * 2;
  const int x0 = 3;
  const int y0 = 26;
  display.fillRect(x0, y0, box, box, TFT_WHITE);
  display.drawXBitmap(x0 + quiet, y0 + quiet, CARDGPT_DEVICE_QR_XBM,
                      CARDGPT_QR_WIDTH, CARDGPT_QR_HEIGHT,
                      static_cast<uint16_t>(TFT_BLACK), static_cast<uint16_t>(TFT_WHITE));

  useBodyFont();
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(tr("Escanea el QR", "Scan the QR"), 90, 27);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawString(tr("o accede a:", "or open:"), 90, 42);
  display.drawString("auth.openai.com", 90, 56);
  display.drawString("/codex/device", 90, 70);
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(tr("Introduce:", "Enter:"), 90, 85);
  display.setFont(&fonts::DejaVu18);
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(code, 90, 97);
  useBodyFont();
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawRightString(String(secondsRemaining / 60) + ":" +
                              (secondsRemaining % 60 < 10 ? "0" : "") +
                              String(secondsRemaining % 60),
                          235, 120);
  present();
}

void CardGPTUI::showLoginAnimation(uint8_t frame, const String& message) {
  display.fillScreen(kColorBackground);
  drawHeader("LOGIN OK");
  const int cx = 120;
  const int cy = 66;
  for (int i = 0; i < 8; ++i) {
    const float angle = (i + frame) * 0.785398f;
    const int x = cx + static_cast<int>(cosf(angle) * 24);
    const int y = cy + static_cast<int>(sinf(angle) * 24);
    const uint16_t color = i < 3 ? kColorText : (i < 6 ? kColorAccentSoft : kColorBorder);
    display.fillCircle(x, y, 3, color);
  }
  display.setTextColor(kColorText, kColorBackground);
  display.drawCentreString(message, 120, 103);
  present();
}

void CardGPTUI::showChat(const ChatMessage* messages, size_t count, const String& input,
                         const String& modelDisplayName, const String& reasoningEffort,
                         bool streaming, int scrollLines, uint8_t thinkingFrame) {
  display.fillScreen(kColorBackground);
  String modelStatus = compactModelName(modelDisplayName);
  if (!reasoningEffort.isEmpty()) modelStatus += " / " + reasoningEffort;
  drawHeader(streaming ? tr("PENSANDO…", "THINKING…") : modelStatus);

  DisplayLine lines[48];
  size_t lineCount = 0;
  for (size_t i = 0; i < count; ++i) {
    appendWrapped(lines, 48, lineCount,
                  messages[i].fromUser ? tr("Tú", "You") : "",
                  messages[i].fromUser, true, 226);
    appendWrapped(lines, 48, lineCount, messages[i].text, messages[i].fromUser, false,
                  messages[i].fromUser ? 184 : 224);
  }
  constexpr int visibleLines = 6;
  constexpr int lineHeight = 14;
  if (count == 0) {
    useBodyFont();
    display.setTextColor(0xce59, kColorBackground);
    display.drawCentreString(tr("¿Qué tienes en mente hoy?", "What’s on your mind?"), 120, 51);
  } else {
    const int maxOffset = max(0, static_cast<int>(lineCount) - visibleLines);
    const int offset = constrain(max(0, scrollLines), 0, maxOffset);
    const int end = static_cast<int>(lineCount) - offset;
    const int start = max(0, end - visibleLines);
    int y = 20;
    for (int i = start; i < end; ++i) {
      DisplayLine& line = lines[i];
      if (line.role && line.fromUser) {
        display.setTextColor(0xbdf7, kColorBackground);
        display.drawRightString(line.text, 233, y);
      } else if (line.role) {
        display.drawXBitmap(6, y, CARDGPT_LOGO_XBM, CARDGPT_LOGO_WIDTH,
                            CARDGPT_LOGO_HEIGHT, kColorText,
                            kColorBackground);
        if (streaming && i == static_cast<int>(lineCount) - 1) {
          const int active = thinkingFrame % 3;
          for (int dot = 0; dot < 3; ++dot) {
            const int dotY = y + (dot == active ? 5 : 8);
            display.fillCircle(27 + dot * 6, dotY, 1,
                               dot == active ? kColorText : kColorMuted);
          }
        }
      } else if (line.fromUser) {
        const int textWidth = std::min<int32_t>(184, display.textWidth(line.text));
        const int x = 232 - textWidth - 8;
        display.fillRoundRect(x - 4, y - 1, textWidth + 9, 14, 4, kColorBubble);
        display.setTextColor(kColorText, kColorBubble);
        display.drawString(line.text, x, y);
      } else {
        display.setTextColor(kColorAccentSoft, kColorBackground);
        display.drawString(line.text, 6, y);
      }
      y += lineHeight;
    }

    if (lineCount > visibleLines) {
      const int trackY = 21;
      const int trackH = 82;
      display.drawFastVLine(238, trackY, trackH, 0x2945);
      const int thumbH = max(10, trackH * visibleLines / static_cast<int>(lineCount));
      const int travel = trackH - thumbH;
      const int thumbY = trackY + (maxOffset == 0 ? 0 : travel * start / maxOffset);
      display.drawFastVLine(238, thumbY, thumbH, kColorMuted);
    }
  }

  display.fillRoundRect(4, 106, 232, 26, 8, kColorPanel);
  display.drawRoundRect(4, 106, 232, 26, 8, kColorBorder);
  display.setTextColor(input.isEmpty() ? kColorMuted : kColorText, kColorPanel);
  const String composer = input.isEmpty()
      ? String(tr("Escribe un mensaje…", "Type a message…"))
      : tailToWidth(input, 212);
  display.drawString(composer, 11, 112);
  present();
}

void CardGPTUI::showSettings(size_t selected, const UserSettings& settings,
                             const ModelCatalog& catalog) {
  display.fillScreen(kColorBackground);
  drawHeader(tr("AJUSTES", "SETTINGS"));
  const char* labelsEs[] = {"Modelo", "Razonamiento", "Idioma", "Cambiar WiFi",
                            "Borrar chat", "Cerrar sesión", "Acerca de", "Atajos"};
  const char* labelsEn[] = {"Model", "Thinking", "Language", "Change Wi-Fi",
                            "Clear chat", "Sign out", "About", "Shortcuts"};
  const ModelOption* selectedModel = catalog.find(settings.textModel);
  String modelName = selectedModel ? selectedModel->displayName : settings.textModel;
  if (modelName.isEmpty()) modelName = settings.textModel;
  String values[8] = {modelName,
                      settings.reasoningEffort,
                      settings.english() ? "English" : "Español",
                      "", "", "", "", ""};
  for (size_t i = 0; i < 8; ++i) {
    const int y = 22 + i * 14;
    if (i == selected) display.fillRoundRect(4, y - 1, 232, 13, 4, kColorSelection);
    const uint16_t background = i == selected ? kColorSelection : kColorBackground;
    display.setTextColor(i == selected ? kColorText : 0xce59, background);
    display.drawString(english_ ? labelsEn[i] : labelsEs[i], 9, y);
    if (!values[i].isEmpty()) display.drawRightString(headToWidth(values[i], 126), 231, y);
  }
  present();
}

void CardGPTUI::showLanguageConfirmation(bool targetEnglish) {
  display.fillScreen(kColorBackground);
  drawHeader(tr("IDIOMA", "LANGUAGE"));
  display.setTextColor(kColorText, kColorBackground);
  useBodyFont();
  display.drawString(tr("Confirmar cambio", "Confirm change"), 8, 27);
  drawWrapped(targetEnglish
                  ? tr("El idioma cambiará a English. CardGPT tendrá que reiniciarse.",
                       "Language will change to English. CardGPT must restart.")
                  : tr("El idioma cambiará a Español. CardGPT tendrá que reiniciarse.",
                       "Language will change to Spanish. CardGPT must restart."),
              8, 46, 224, kColorText, 3, 13);
  display.fillRoundRect(28, 88, 184, 25, 7, kColorSelection);
  display.drawRoundRect(28, 88, 184, 25, 7, kColorBorder);
  display.setTextColor(kColorText, kColorSelection);
  display.drawCentreString(tr("ENTER  CONFIRMAR", "ENTER  CONFIRM"), 120, 94);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(tr("`: cancelar", "`: cancel"), 120, 120);
  present();
}

void CardGPTUI::showAbout() {
  display.fillScreen(kColorBackground);
  drawHeader(tr("ACERCA DE", "ABOUT"));
  display.setTextColor(kColorText, kColorBackground);
  display.drawString(String("CardGPT v") + kFirmwareVersion, 8, 26);
  drawWrapped(tr("Proyecto comunitario independiente, no afiliado ni respaldado por OpenAI.",
                 "Independent community project, not affiliated with or endorsed by OpenAI."),
              8, 43, 224, 0xce59, 3, 13);
  display.setTextColor(kColorAccentSoft, kColorBackground);
  display.drawCentreString(tr("Hecho por Sami González Kamel",
                              "Made by Sami González Kamel"),
                           120, 86);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString("ig:samilososami", 120, 101);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(tr("`: volver", "`: back"), 120, 121);
  present();
}

void CardGPTUI::showShortcuts() {
  display.fillScreen(kColorBackground);
  drawHeader(tr("ATAJOS", "SHORTCUTS"));
  const char* keys[] = {"Fn + `", "Ctrl + L", "Fn + Up/Down", "Enter", "`"};
  const char* labelsEs[] = {"Ajustes", "Borrar chat", "Mover chat", "Enviar", "Volver"};
  const char* labelsEn[] = {"Settings", "Clear chat", "Scroll chat", "Send", "Back"};
  for (size_t i = 0; i < 5; ++i) {
    const int y = 27 + static_cast<int>(i) * 18;
    display.setTextColor(kColorText, kColorBackground);
    display.drawString(keys[i], 8, y);
    display.setTextColor(kColorMuted, kColorBackground);
    display.drawRightString(english_ ? labelsEn[i] : labelsEs[i], 232, y);
  }
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(tr("`: volver", "`: back"), 120, 120);
  present();
}

void CardGPTUI::showError(const String& title, const String& detail, const String& footer) {
  display.fillScreen(kColorBackground);
  drawHeader("ERROR");
  display.setTextColor(kColorDanger, kColorBackground);
  useBodyFont();
  display.drawString(title, 8, 26);
  drawWrapped(detail, 8, 47, 224, kColorText, 5, 14);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(footer.isEmpty()
                               ? tr("Enter: reintentar  `: atrás", "Enter: retry  `: back")
                               : footer,
                           120, 120);
  present();
}

void CardGPTUI::showNotice(const String& title, const String& detail, const String& footer) {
  display.fillScreen(kColorBackground);
  drawHeader();
  display.setTextColor(kColorText, kColorBackground);
  useBodyFont();
  display.drawString(title, 8, 28);
  drawWrapped(detail, 8, 50, 224, 0xce59, 5, 14);
  display.setTextColor(kColorMuted, kColorBackground);
  display.drawCentreString(footer, 120, 120);
  present();
}

void CardGPTUI::drawWrapped(const String& text, int x, int y, int width, uint16_t color,
                            int maxLines, int lineHeight) {
  display.setTextColor(color, kColorBackground);
  String line;
  size_t offset = 0;
  int lineIndex = 0;
  while (offset < text.length() && lineIndex < maxLines) {
    const size_t charLength = nextUtf8(text, offset);
    const String character = text.substring(offset, offset + charLength);
    if (character == "\n") {
      display.drawString(line, x, y + lineIndex * lineHeight);
      line = "";
      ++lineIndex;
      offset += charLength;
      continue;
    }
    const String candidate = line + character;
    if (!line.isEmpty() && display.textWidth(candidate) > width) {
      const int split = line.lastIndexOf(' ');
      if (split > 0) {
        display.drawString(line.substring(0, split), x, y + lineIndex * lineHeight);
        line = line.substring(split + 1) + character;
      } else {
        display.drawString(line, x, y + lineIndex * lineHeight);
        line = character;
      }
      ++lineIndex;
    } else {
      line = candidate;
    }
    offset += charLength;
  }
  if (!line.isEmpty() && lineIndex < maxLines) display.drawString(line, x, y + lineIndex * lineHeight);
}

String CardGPTUI::tailToWidth(const String& text, int width) {
  String result = text;
  while (display.textWidth(result) > width && !result.isEmpty()) {
    result.remove(0, nextUtf8(result, 0));
  }
  return result;
}

String CardGPTUI::headToWidth(const String& text, int width) {
  if (display.textWidth(text) <= width) return text;
  String result = text;
  const String ellipsis = "…";
  while (!result.isEmpty() && display.textWidth(result + ellipsis) > width) {
    size_t offset = result.length() - 1;
    while (offset > 0 && (static_cast<uint8_t>(result[offset]) & 0xc0) == 0x80) --offset;
    result.remove(offset);
  }
  return result + ellipsis;
}

String CardGPTUI::compactModelName(const String& displayName) const {
  String compact = displayName;
  if (compact.startsWith("GPT-")) compact.remove(0, 4);
  compact.replace('-', ' ');
  return compact;
}

void CardGPTUI::appendWrapped(DisplayLine* lines, size_t capacity, size_t& count,
                              const String& text, bool fromUser, bool role, int width) {
  if (count >= capacity) return;
  if (role) {
    lines[count++] = {text, fromUser, true};
    return;
  }
  String line;
  size_t offset = 0;
  while (offset < text.length() && count < capacity) {
    const size_t charLength = nextUtf8(text, offset);
    const String character = text.substring(offset, offset + charLength);
    if (character == "\n") {
      lines[count++] = {line, fromUser, false};
      line = "";
      offset += charLength;
      continue;
    }
    const String candidate = line + character;
    if (!line.isEmpty() && display.textWidth(candidate) > width) {
      const int split = line.lastIndexOf(' ');
      if (split > 0) {
        lines[count++] = {line.substring(0, split), fromUser, false};
        line = line.substring(split + 1) + character;
      } else {
        lines[count++] = {line, fromUser, false};
        line = character;
      }
    } else {
      line = candidate;
    }
    offset += charLength;
  }
  if (!line.isEmpty() && count < capacity) lines[count++] = {line, fromUser, false};
}

size_t CardGPTUI::nextUtf8(const String& text, size_t offset) const {
  if (offset >= text.length()) return 0;
  const uint8_t c = static_cast<uint8_t>(text[offset]);
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xe0) == 0xc0) return std::min<size_t>(2, text.length() - offset);
  if ((c & 0xf0) == 0xe0) return std::min<size_t>(3, text.length() - offset);
  if ((c & 0xf8) == 0xf0) return std::min<size_t>(4, text.length() - offset);
  return 1;
}

}  // namespace cardgpt
