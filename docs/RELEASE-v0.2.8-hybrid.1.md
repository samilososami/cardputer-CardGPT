# CardGPT v0.2.8 · Instalador híbrido

Esta publicación reúne los dos formatos de CardGPT v0.2.8 y estrena la instalación web que **conserva M5Launcher, sus aplicaciones y NVS cuando ya están presentes**.

## Qué hace el instalador

- Detecta el ESP32-S3, los 8 MB de flash, las protecciones y la tabla antes de habilitar la escritura.
- Con M5Launcher, actualiza CardGPT en su ranura real o crea una OTA nueva en espacio libre.
- No mueve, reduce, renombra ni elimina otras particiones para conseguir sitio.
- Conserva M5Launcher, los demás firmwares, Wi-Fi, sesión y ajustes.
- Si no hay espacio, cancela y pide liberar una aplicación desde M5Launcher.
- Si no existe M5Launcher, ofrece por separado una instalación limpia e independiente con una confirmación específica.

No hace falta crear antes una partición llamada CardGPT. Una instalación nueva escribe y verifica la aplicación antes de registrar su entrada en la tabla.

## Descargas

- **`CardGPT.bin`** — aplicación para M5Launcher, 1.588.480 bytes. No escribir en `0x0`.
- **`CardGPT-full.bin`** — imagen independiente de 8.388.608 bytes para `0x0`; borra toda la flash.
- **`SHA256SUMS`** — hashes de ambos archivos.

```text
5b89f2140a08ca197fda0f0f1de116e016b45e61635561bdc0bec874dc2f1067  CardGPT.bin
3306b8aa987912389ca848c7e517c3c7d06cef369c03ef0890a62e1d305ec9d0  CardGPT-full.bin
```

Los bytes de la aplicación son exactamente los de CardGPT v0.2.8 aprobados previamente; no se ha cambiado la interfaz ni el firmware interno.

[Instalar por USB](https://flasher.samilososami.com/cardputer/) · [Guía completa](https://samilososami.com/tools/cardputer/cardgpt/)

## Verificación y límites

Las pruebas automatizadas cubren instalación y actualización, preservación byte a byte de NVS, M5Launcher, otras aplicaciones y particiones de datos, falta de espacio, cambios concurrentes de tabla, fallos de escritura/verificación y la ruta independiente. El navegador se ha validado en Chromium de escritorio y viewport móvil. No se ha realizado un nuevo flasheo físico para este cambio del instalador.

Los layouts reconocidos como M5Launcher pero antiguos, ambiguos o dañados se bloquean en lugar de ofrecer un borrado completo. Solo para **M5Stack Cardputer ADV con ESP32-S3 y 8 MB**. Proyecto independiente, no afiliado a OpenAI, ChatGPT ni M5Stack.
