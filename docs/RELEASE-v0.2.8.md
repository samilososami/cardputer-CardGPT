# CardGPT v0.2.8

> **Aplicación para M5Launcher.** El instalador web detecta el launcher, crea o actualiza su ranura sin borrar otros firmwares y usa este archivo. Para una instalación independiente existe también [v0.2.8-standalone.1](https://github.com/samilososami/cardputer-CardGPT/releases/tag/v0.2.8-standalone.1). Este binario no se escribe en `0x0`.

Primera publicación pública del firmware de texto para **M5Stack Cardputer ADV**.

## Incluido

- Asistente Wi-Fi, redes ocultas y vinculación de cuenta mediante código y QR.
- Chat de texto con respuestas progresivas, catálogo de modelos y modos de razonamiento según la cuenta.
- Español e inglés, confirmación de cambio de idioma, atajos y datos en flash.
- Paleta original, acentos y signos españoles, animaciones de espera y desplazamiento por el chat.
- Indicador de batería suavizado, actualización cada 30 segundos, puntos de pensamiento compactos y Ctrl+L para limpiar el chat.
- Sin voz, dictado ni Realtime; audio desactivado y emojis filtrados.

## Archivo

- **`CardGPT.bin`** — aplicación ESP32-S3, 1.588.480 bytes.
- **`SHA256SUMS`** — comprobación de integridad.
- SHA-256: `5b89f2140a08ca197fda0f0f1de116e016b45e61635561bdc0bec874dc2f1067`.

Es el mismo binario v0.2.8 probado y aprobado en el dispositivo, renombrado para que M5Launcher lo instale como **CardGPT**. No es una imagen completa: **no escribir en 0x0**.

[Instalador y descarga](https://flasher.samilososami.com/cardputer/) · [Guía completa](https://samilososami.com/tools/cardputer/cardgpt/)

El instalador público actual usa este binario cuando detecta M5Launcher. No exige una partición CardGPT previa: crea una OTA en espacio libre, o actualiza la existente, y bloquea la operación antes de sustituir otra app.

Proyecto independiente, no afiliado a OpenAI, ChatGPT ni M5Stack. Acceso, modelos y cuota sujetos a la cuenta y al servicio.
