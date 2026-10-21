# CardGPT v0.2.8 · Instalación completa

**Una imagen, un cable y CardGPT.** Este paquete permite instalar el firmware directamente por USB en el M5Stack Cardputer ADV, sin M5Launcher, sin firmware previo y sin preparar particiones.

> Este es el paquete **independiente opcional**. El instalador web actual preserva M5Launcher cuando lo detecta y solo ofrece esta ruta limpia cuando no hay un launcher compatible.

## Qué cambia

- `CardGPT.bin` es ahora una **imagen completa de 8 MB para escribir en `0x0`**, con bootloader, tabla, selector de arranque y aplicación.
- El flasher realiza una instalación limpia, verifica el archivo antes del borrado y los 8 MB escritos al terminar.
- La casilla y el diálogo de confirmación avisan del borrado completo. Se comprueban chip, memoria y protecciones; no se modifican eFuses.
- Se actualizan la guía, la web del proyecto, la descarga y las instrucciones de recuperación.

## Qué no cambia

La aplicación sigue siendo **v0.2.8**: misma interfaz, paleta, funciones y comportamiento. El paquete contiene exactamente los **1.588.480 bytes** del firmware aprobado, sin recompilarlo. El sufijo `standalone.1` identifica este nuevo formato de instalación.

> **Se borran todos los firmwares y datos de la flash interna**, incluido cualquier launcher, la sesión, las redes Wi-Fi y los ajustes. Tras cada instalación habrá que configurar la red y vincular la cuenta otra vez. Apagar o reiniciar durante el uso normal no borra esos datos.

## Descarga

- `CardGPT.bin`: **8.388.608 bytes**, imagen completa, dirección **0x0**.
- `SHA256SUMS`: comprobación del archivo.
- SHA-256: `3306b8aa987912389ca848c7e517c3c7d06cef369c03ef0890a62e1d305ec9d0`.
- SHA-256 de la aplicación incluida: `5b89f2140a08ca197fda0f0f1de116e016b45e61635561bdc0bec874dc2f1067`.

**No confundas este archivo con el de la release histórica `v0.2.8`, que solo contiene la aplicación y no se puede escribir en 0x0.**

[Instalar por USB](https://flasher.samilososami.com/cardputer/) · [Guía del proyecto](https://samilososami.com/tools/cardputer/cardgpt/)

## Verificación y límites

Imagen y zonas vacías validadas offline; aplicación comparada byte a byte con la aprobada. Pruebas automatizadas del flujo de instalación, integridad, borrado, verificación y rechazos de seguridad. El paquete no procede de un volcado de dispositivo y no incluye credenciales. **No se ha realizado un nuevo flasheo físico para verificar esta instalación completa.**

Solo para **Cardputer ADV con ESP32-S3 y 8 MB**. Proyecto independiente, no afiliado a OpenAI, ChatGPT ni M5Stack. El acceso y la cuota dependen de la cuenta y del servicio.
