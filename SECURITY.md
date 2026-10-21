# Seguridad

No publiques tokens OAuth, contraseñas, códigos de dispositivo activos ni volcados de NVS o de flash en issues, pull requests o capturas. Para avisar de una vulnerabilidad, utiliza la opción privada de reporte de seguridad de GitHub cuando esté disponible; en caso contrario, contacta al autor por los canales de su web sin adjuntar secretos en público.

## Modelo de protección

- TLS valida certificados; no se emplea `setInsecure()`.
- Las envolturas AES-GCM de NVS evitan texto plano trivial, pero su clave deriva de la MAC y de un contexto público. **No resisten extracción física ni sustituyen Flash Encryption/Secure Boot**. La release no provisiona estas protecciones.
- Antes de ceder un dispositivo, cierra la sesión y revoca el acceso desde tu cuenta. La instalación limpia borra la flash interna, pero no revoca por sí misma las sesiones en el servicio remoto.
- El flasher **borra toda la flash** y escribe una imagen completa: bootloader, tabla, selector de arranque y aplicación. Elimina todos los firmwares anteriores, launchers, NVS, redes, sesión y preferencias. Hay una casilla y una segunda confirmación explícita antes de borrar.
- Antes de borrar se comprueban tamaño y SHA-256 de la imagen, chip ESP32-S3, flash de 8 MB, cargador USB y registros de seguridad. Se rechaza Secure Boot o cualquier contador de cifrado provisionado; un error de lectura bloquea el proceso. **No se modifican eFuses ni se fuerzan protecciones.**
- Al finalizar se compara el MD5 de los 8 MB escritos. Si falla el borrado, no se escribe; si falla la escritura o la verificación, no se declara instalado. Una interrupción puede dejar el equipo sin firmware arrancable: hay que entrar con G0 en modo descarga y repetir la instalación completa.
- Las comprobaciones de chip y memoria no identifican de manera criptográfica el modelo físico. Confirma siempre que conectas un **Cardputer ADV**. El flasher no necesita leer una tabla de particiones preexistente ni extrae credenciales del dispositivo.

Este firmware experimental y su flasher se distribuyen sin garantía. Conserva una copia de seguridad de tus datos antes de instalar.
