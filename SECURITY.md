# Seguridad

No publiques tokens OAuth, contraseñas, códigos de dispositivo activos ni volcados de NVS o de flash en issues, pull requests o capturas. Para avisar de una vulnerabilidad, utiliza la opción privada de reporte de seguridad de GitHub cuando esté disponible; en caso contrario, contacta al autor por los canales de su web sin adjuntar secretos en público.

## Modelo de protección

- TLS valida certificados; no se emplea `setInsecure()`.
- Las envolturas AES-GCM de NVS evitan texto plano trivial, pero su clave deriva de la MAC y de un contexto público. **No resisten extracción física ni sustituyen Flash Encryption/Secure Boot**. La release no provisiona estas protecciones.
- Antes de ceder un dispositivo, cierra la sesión, revoca el acceso desde tu cuenta y elimina las credenciales almacenadas. Un borrado completo de flash también elimina M5Launcher: hazlo solo con un procedimiento de recuperación preparado.
- El flasher solo escribe una aplicación CardGPT ya identificada. No efectúa borrados completos ni modifica eFuses, tabla de particiones o selección OTA.
- Las comprobaciones de chip y particiones no identifican de manera criptográfica el modelo físico. Confirma siempre que conectas un **Cardputer ADV**.

Este firmware experimental y su flasher se distribuyen sin garantía. Conserva una copia de seguridad de tus datos antes de instalar.
