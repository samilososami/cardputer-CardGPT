# Créditos y componentes de terceros

CardGPT es un proyecto independiente de Sami González Kamel. No está afiliado, patrocinado ni respaldado por OpenAI, ChatGPT o M5Stack. Los nombres y logotipos pertenecen a sus respectivos titulares; la licencia MIT del código no concede derechos sobre sus marcas.

## Firmware

- [M5Cardputer](https://github.com/m5stack/M5Cardputer), M5Stack. Se incluye `lib/M5Cardputer` desde el commit `f1392858b9994c3547120e602a57d3553d16ab01`, con los avisos SPDX originales. Los archivos declaran MIT; Adafruit_TCA8418 conserva sus propios avisos. `library.properties` identifica esta copia como 1.1.2, mientras que `library.json` de upstream indica 1.1.1.
- [M5Unified](https://github.com/m5stack/M5Unified) 0.2.27 y [M5GFX](https://github.com/m5stack/M5GFX) 0.2.34, M5Stack. Instalados por Arduino Library Manager. Las fuentes gráficas incluidas mantienen las licencias declaradas por M5GFX.
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.4.2, Benoît Blanchon, MIT.
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32) 3.3.11, Espressif y contribuidores, bajo sus licencias LGPL y de terceros. Incluye ESP-IDF y mbedTLS.
- Certificados raíz públicos ISRG y Google Trust Services, usados para verificar TLS. No son claves privadas.
- Splash: adaptación gráfica para CardGPT del material de referencia aportado por el autor; incorpora el logotipo de OpenAI como referencia a la integración, no como marca propia.

## Flasher

- [esptool-js](https://github.com/espressif/esptool-js) 0.6.0, Espressif, Apache-2.0.
- [SparkMD5](https://github.com/satazor/js-spark-md5) 3.0.2, WTFPL.
- [Inter](https://github.com/rsms/inter) y [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono), SIL Open Font License 1.1. Los avisos se distribuyen junto a las fuentes en `web/flasher/public/fonts`.

Consulta las licencias y avisos de cada dependencia antes de redistribuir una compilación modificada.
