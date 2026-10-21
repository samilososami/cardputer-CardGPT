# Seguridad

No publiques tokens OAuth, contraseñas, códigos de dispositivo activos ni volcados de NVS o de flash en issues, pull requests o capturas. Para avisar de una vulnerabilidad, utiliza la opción privada de reporte de seguridad de GitHub cuando esté disponible; en caso contrario, contacta al autor por los canales de su web sin adjuntar secretos en público.

## Modelo de protección

- TLS valida certificados; no se emplea `setInsecure()`.
- Las envolturas AES-GCM de NVS evitan texto plano trivial, pero su clave deriva de la MAC y de un contexto público. **No resisten extracción física ni sustituyen Flash Encryption/Secure Boot**. La release no provisiona estas protecciones.
- Antes de ceder un dispositivo, cierra la sesión y revoca el acceso desde tu cuenta. La instalación limpia borra la flash interna, pero no revoca por sí misma las sesiones en el servicio remoto.
- El flasher lee primero chip, protecciones y tabla de particiones. Con un M5Launcher compatible solo actualiza CardGPT o crea una partición OTA en espacio libre; nunca mueve, redimensiona ni elimina otras particiones para conseguir espacio. NVS y las demás aplicaciones quedan fuera de los rangos de escritura.
- Una ranura nueva se registra en la tabla únicamente después de escribir y verificar CardGPT. Si la tabla cambia entre la revisión y la confirmación, si no hay espacio, si el layout es ambiguo o si falla una comprobación, la operación se cancela.
- Si no se detecta M5Launcher, la interfaz ofrece por separado una **instalación limpia** que borra los 8 MB y exige avisos específicos. Esta ruta elimina firmware, NVS, redes, sesión y preferencias; no se ofrece como salida automática ante un M5Launcher reconocido pero incompatible.
- Antes de escribir se comprueban tamaño y SHA-256 del archivo, chip ESP32-S3, flash de 8 MB, cargador USB y registros de seguridad. Se rechaza Secure Boot o cualquier contador de cifrado provisionado; un error de lectura bloquea el proceso. **No se modifican eFuses ni se fuerzan protecciones.**
- Se compara el MD5 de CardGPT y, cuando cambia, de la tabla completa. En modo limpio se verifica la imagen completa. Una interrupción durante esta última ruta puede dejar el equipo sin firmware arrancable: entra con G0 y repite.
- Las comprobaciones no identifican criptográficamente el modelo físico. Confirma siempre que conectas un **Cardputer ADV**. El flasher no extrae NVS ni credenciales; solo lee la tabla y la cabecera de M5Launcher para decidir el modo.

Este firmware experimental y su flasher se distribuyen sin garantía. Conserva una copia de seguridad de tus datos antes de instalar.
