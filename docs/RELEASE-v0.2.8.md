# CardGPT v0.2.8

> **Publicación histórica: solo aplicación.** Para una instalación completa por USB usa [v0.2.8-standalone.1](https://github.com/samilososami/cardputer-CardGPT/releases/tag/v0.2.8-standalone.1). El archivo de esta release no sirve para escribir en `0x0`. Se conserva sin cambios para mantener su integridad y procedencia.

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

El instalador que acompañó originalmente esta release era un actualizador de aplicaciones existentes. El instalador público actual utiliza el nuevo paquete completo y borra toda la flash. Usa la publicación enlazada arriba para las instrucciones vigentes.

Proyecto independiente, no afiliado a OpenAI, ChatGPT ni M5Stack. Acceso, modelos y cuota sujetos a la cuenta y al servicio.
