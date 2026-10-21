<div align="center">

<img src="assets/cardgpt-splash-240x135.png" alt="CardGPT — pantalla de arranque" width="480" />

# CardGPT

**Tus conversaciones de texto, en un M5Stack Cardputer ADV.**

[Instalar y descargar](https://flasher.samilososami.com/cardputer/) · [Conocer el proyecto](https://samilososami.com/tools/cardputer/cardgpt/) · [Última versión](https://github.com/samilososami/cardputer-CardGPT/releases/latest)

![Versión](https://img.shields.io/github/v/release/samilososami/cardputer-CardGPT?color=596448&label=firmware)
![Licencia](https://img.shields.io/badge/licencia-MIT-596448)
![Hardware](https://img.shields.io/badge/hardware-Cardputer_ADV-292d29)

</div>

CardGPT convierte el Cardputer ADV en un cliente de conversación portátil: enciendes, te conectas a una red Wi-Fi, vinculas tu cuenta mediante el inicio de sesión por dispositivo de Codex y empiezas a escribir. Una interfaz de texto adaptada a su pantalla de 240 × 135 píxeles, sin móvil como intermediario y sin depender de una tarjeta microSD para funcionar.

> **Proyecto independiente y experimental.** No está afiliado, patrocinado ni respaldado por OpenAI, ChatGPT o M5Stack. Usa servicios de Codex no destinados a una API pública estable para este dispositivo: el acceso, los modelos y las cuotas dependen de la cuenta, de sus permisos y de cambios del servicio. No se garantiza compatibilidad futura ni acceso gratuito o ilimitado.

## Qué puedes hacer

- **Conversar por texto**, con respuestas progresivas, historial de la sesión y desplazamiento con el teclado.
- **Conectar redes visibles u ocultas**: escaneo Wi-Fi y entrada manual de SSID y contraseña.
- **Iniciar sesión con un QR y un código de dispositivo**. No tienes que introducir una API key en el firmware.
- **Escoger modelo y nivel de razonamiento** entre las opciones que devuelve el catálogo de tu cuenta. No hay una lista de modelos prometida para todos los planes.
- **Usar español o inglés**, con preferencia guardada y confirmación antes del reinicio.
- **Conservar Wi-Fi, sesión y ajustes en la flash interna**; el chat permanece en RAM y se pierde al reiniciar.
- **Leer con claridad**: tildes, ¿ y ¡, tipografía adaptada, paleta oscura original y dibujo mediante framebuffer.
- **Ver la batería sin saltos continuos**: muestra una estimación suavizada cada 30 segundos.

La versión actual es **exclusivamente de texto**. No incluye micrófono, dictado, voz ni integración Realtime. El audio del dispositivo permanece desactivado. Los emojis se filtran para evitar glifos vacíos en pantalla.

## Compatibilidad

| Requisito | Detalle |
| --- | --- |
| Dispositivo probado | M5Stack **Cardputer ADV** |
| Chip y memoria | ESP32-S3, flash de 8 MB |
| Red | Wi-Fi de 2,4 GHz con acceso a Internet |
| Cuenta | Cuenta con acceso al servicio de Codex utilizado por el firmware |
| Instalador web | Chromium de escritorio con Web Serial, por ejemplo Chrome o Edge |
| Cable | USB de datos, no solo de carga |

El Cardputer original y otras placas no se consideran compatibles por el mero hecho de usar ESP32-S3. No instales esta imagen en ellas sin adaptar y verificar el hardware.

## Instalación

### Directamente desde el navegador

CardGPT se instala como un firmware completo e independiente. **No necesitas M5Launcher, ningún firmware previo ni preparar particiones.** El mismo proceso sirve para la primera instalación y para reinstalar.

> **Instalación limpia: se borra toda la flash interna.** Se eliminan el firmware anterior, cualquier launcher, otros firmwares, la sesión, las redes Wi-Fi y los ajustes. Guarda lo importante antes de continuar. Tendrás que configurar la Wi-Fi y vincular la cuenta de nuevo.

1. Abre **[flasher.samilososami.com/cardputer](https://flasher.samilososami.com/cardputer/)** en Chrome, Edge u otro Chromium de escritorio con Web Serial.
2. Conecta el **Cardputer ADV** con un cable USB de datos y cierra los monitores serie.
3. Pulsa **Conectar por USB** y elige su puerto. La web comprueba ESP32-S3, los 8 MB de flash, las protecciones del chip y la integridad del archivo.
4. Lee el aviso, marca la casilla y pulsa **Borrar e instalar CardGPT**. Una segunda confirmación precede al borrado.
5. Espera a que termine la verificación sin desconectar el cable. CardGPT arrancará directamente; si el reinicio automático no funciona, apaga y enciende el equipo.

No necesitas microSD ni para instalar ni para usar CardGPT. La web no consulta ni depende de la distribución anterior de memoria. Rechaza chips con Secure Boot o cifrado configurado; no modifica eFuses ni permite forzar estas protecciones.

**Si no aparece el puerto:** sigue el [modo descarga oficial del Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv): interruptor lateral en OFF, mantén G0 antes de encender, aplica alimentación y suelta G0. Reconecta desde la web. En Linux, comprueba los permisos del puerto serie.

### Descargar e instalar con otra herramienta

- **[CardGPT.bin — imagen completa](https://github.com/samilososami/cardputer-CardGPT/releases/download/v0.2.8-standalone.1/CardGPT.bin)**: 8.388.608 bytes; contiene bootloader, tabla, selector de arranque y aplicación. Dirección de escritura: **`0x0`**.
- **[SHA256SUMS](https://github.com/samilososami/cardputer-CardGPT/releases/download/v0.2.8-standalone.1/SHA256SUMS)**: verifica el archivo antes de instalar.
- SHA-256: `3306b8aa987912389ca848c7e517c3c7d06cef369c03ef0890a62e1d305ec9d0`.

Para usuarios de [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html), sustituye `PUERTO` por el puerto del Cardputer. **Estos comandos borran todos los datos del dispositivo seleccionado:**

```bash
esptool --chip esp32s3 --port PUERTO erase-flash
esptool --chip esp32s3 --port PUERTO write-flash 0x0 CardGPT.bin
```

No uses `--force` si esptool detecta protecciones. Si se interrumpe la escritura, entra en modo descarga y repite la instalación completa.

> La release histórica `v0.2.8` contiene un archivo de aplicación de 1.588.480 bytes, **no apto para escribir en `0x0`**. Usa la publicación **`v0.2.8-standalone.1`** o el instalador actual. Son formatos diferentes aunque ambos se llamen `CardGPT.bin`.

## Primer arranque

1. **Wi-Fi.** Elige una red o «Añadir red wifi» para introducir un SSID oculto. Escribe su contraseña, si corresponde.
2. **TLS y cuenta.** Espera a que termine la conexión. Abre la dirección que muestra el Cardputer o escanea su QR, e introduce el código tal como aparece. Completa la autorización en el navegador.
3. **Catálogo.** CardGPT consulta los modelos disponibles para esa cuenta. Puedes cambiar el modelo y su modo de razonamiento desde Ajustes.
4. **Chat.** Escribe tu mensaje y pulsa Enter. Las respuestas se van mostrando a medida que llegan.

Las cuotas y errores de servicio no se eluden. Si la cuenta no tiene acceso, ha agotado su cuota o el servidor cambia, el firmware puede no completar la conexión. No compartas capturas de códigos activos, tokens ni volcados de flash.

## Atajos

| Tecla | Acción |
| --- | --- |
| **Enter** | Enviar el prompt o confirmar la opción seleccionada |
| **Esc** (`Fn` + `` ` ``) | Abrir Ajustes desde el chat |
| **Ctrl + ,** | Acceso alternativo a Ajustes |
| <kbd>`</kbd> sin Fn | Volver/salir de los menús |
| **Fn + ↑ / ↓** | Desplazarse por el historial del chat |
| **↑ / ↓** sin Fn en menús | Mover la selección sin mantener Fn |
| **Ctrl + L** | Vaciar el chat y el prompt cuando el chat está listo para entrada |

También están disponibles en **Ajustes → Atajos / Shortcuts**. El procesamiento de solicitudes puede ocupar temporalmente la entrada de teclado; no se promete cancelación durante una respuesta.

## Privacidad y seguridad

El firmware se comunica directamente con los servicios de OpenAI mediante TLS validado. El flasher no pide tu contraseña Wi-Fi, cuenta de OpenAI ni tokens: solo transfiere el binario a través del puerto USB que autorizas.

Las credenciales y preferencias se almacenan en NVS con envolturas AES-256-GCM. **Esto no equivale a almacenamiento seguro frente a acceso físico**: la clave se deriva de datos del dispositivo y un contexto público, sin un secreto de hardware provisionado. Quien pueda extraer la flash y conocer el dispositivo podría recuperar los datos. Revisa [SECURITY.md](SECURITY.md) antes de ceder o vender el equipo.

Apagar o reiniciar conserva Wi-Fi, sesión y ajustes. **Reinstalar desde este flasher los borra**, porque el procedimiento es siempre una instalación limpia de toda la flash. El historial del chat solo permanece en RAM.

El contenido de tus mensajes se envía al servicio conectado y queda sujeto a sus condiciones. El flasher no incorpora analítica propia ni telemetría de conversaciones; el proveedor de alojamiento puede conservar sus registros HTTP habituales.

## Compilar el firmware

Requisitos: Linux/macOS, Bash, Python 3, Git y `arduino-cli` en el `PATH`. Todos los comandos se ejecutan como usuario normal; también funcionan como root. Los datos de Arduino quedan dentro del proyecto, sin rutas a un usuario concreto.

```bash
git clone https://github.com/samilososami/cardputer-CardGPT.git
cd cardputer-CardGPT
bash scripts/setup.sh
bash scripts/build.sh
```

El resultado es `dist/CardGPT.bin`, una **imagen completa de 8 MB para `0x0`**, y `dist/SHA256SUMS`. El empaquetado usa el esquema estándar `default_8MB` de Arduino-ESP32 y valida bootloader, aplicación, tabla, selector de arranque, checksums y zonas vacías. La imagen de aplicación intermedia de `build/` no es el archivo de instalación.

Se fija Arduino-ESP32 **3.3.11**, M5Unified **0.2.27**, M5GFX **0.2.34** y ArduinoJson **7.4.2**. La copia de M5Cardputer utilizada está incluida en `lib/`. El bitmap de arranque ya está generado; regenerarlo requiere Pillow y qrcode, y se hace con `scripts/generate_splash.py`.

La publicación `v0.2.8-standalone.1` cambia el **paquete de instalación**, no la aplicación ni su estética: conserva dentro, byte a byte, la aplicación v0.2.8 aprobada en el dispositivo. Una compilación local puede diferir por marcas de tiempo, rutas y herramientas. El formato completo y el flujo USB cuentan con validación offline y pruebas automatizadas; este cambio no se ha probado mediante un nuevo flasheo físico.

## Ejecutar el flasher localmente

```bash
cd web/flasher
npm ci
npm run firmware
npm run dev -- --host 127.0.0.1
```

`npm run firmware` descarga únicamente el asset público de la release y verifica su SHA-256. No carga credenciales. Para producción: `npm test && npm run build`; la salida está en `web/flasher/dist/`. Web Serial necesita HTTPS o `localhost`.

## Estructura

```text
firmware/CardGPT/   Aplicación, interfaz, conexión y almacenamiento
lib/M5Cardputer/    Biblioteca de hardware utilizada
assets/            Splash y QR
scripts/           Preparación y compilación portables
docs/              Notas de versión y arquitectura
web/flasher/       Instalador Web Serial y catálogo web
```

## Desarrollo y límites

El objetivo es mantener un cliente ligero y legible en una pantalla pequeña, no reproducir toda la aplicación ChatGPT. El historial y el tamaño del contexto están limitados por la memoria; no hay adjuntos, sincronización de conversaciones con ChatGPT, Markdown completo ni reproducción de voz.

Para proponer mejoras, abre una issue con versión, modelo exacto del dispositivo, comportamiento esperado y pasos para reproducirlo. **Elimina todos los datos personales y credenciales** de los registros. Las contribuciones se reciben mediante pull request; consulta [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor y licencia

**Hecho por Sami González Kamel · [ig:samilososami](https://www.instagram.com/samilososami/)**

[samilososami.com](https://samilososami.com/) · [Colección Cardputer](https://samilososami.com/tools/cardputer/)

Código original bajo [licencia MIT](LICENSE). Bibliotecas y marcas de terceros conservan sus derechos: [créditos y licencias](THIRD_PARTY.md).
