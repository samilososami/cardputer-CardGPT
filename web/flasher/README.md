# Flasher de samilososami

Aplicación estática multipágina con Vite y TypeScript. No necesita backend, base de datos ni credenciales del usuario.

- `/`: catálogo de dispositivos.
- `/cardputer/`: descarga e instalación completa de CardGPT por USB, sin firmware previo.
- `/firmware/CardGPT.bin`: asset verificado contra la release pública.

## Desarrollo

```bash
npm ci
npm run firmware
npm run dev
```

`npm run build` verifica el binario completo, ejecuta las pruebas del instalador, comprueba TypeScript y genera `dist/`. `npm run firmware` prepara el archivo que usan las pruebas.

## Instalación limpia

La imagen ocupa 8 MB y se escribe desde `0x0`. No se consulta la tabla anterior del dispositivo. Tras dos confirmaciones en la interfaz, el proceso es: validar SHA-256 y dispositivo → borrar toda la flash → escribir → verificar el MD5 completo → reiniciar. **Se pierden todos los firmwares, launchers, Wi-Fi, sesión y ajustes anteriores.**

El módulo `src/installer.ts` separa el flujo de hardware de la interfaz para probar los rechazos y el orden de operaciones. No se borra si falla la validación del archivo o del dispositivo. Se exige ESP32-S3, 8 MB, stub activo y registros de seguridad legibles sin Secure Boot ni contador de cifrado provisionado. No se escribe ningún eFuse. La identificación del chip no sustituye la confirmación humana del modelo Cardputer ADV.

## Vercel

El proyecto oficial está conectado al repositorio de GitHub y usa:

| Ajuste | Valor |
| --- | --- |
| Root Directory | `web/flasher` |
| Framework | Vite |
| Build Command | `npm run build` |
| Output Directory | `dist` |
| Rama de producción | `main` |

Los cambios en la rama de producción se despliegan mediante la integración Git de Vercel. Para un fork, crea un proyecto propio con estos ajustes; no necesitas acceso a la cuenta del autor. `vercel.json` está dentro de esta carpeta y define las rutas, cabeceras de seguridad y el nombre de descarga.

El binario no se versiona en Git: en una compilación limpia se descarga desde GitHub Releases y se valida antes de publicar. `firmware.json` fija la release, el formato completo, el tamaño y los hashes; lo comparten el cliente, las pruebas y el script de descarga. Este último genera también SHA256SUMS. Al actualizar el paquete deben actualizarse el manifiesto y el contenido de las páginas. Publica primero el asset de la release y después despliega la web. Nunca publiques un firmware sin comprobar la imagen y sus límites.

## Límite de las pruebas

Las pruebas automáticas usan la imagen real y un dispositivo simulado: archivo completo, aplicación idéntica, NVS vacía, rechazo del antiguo archivo de aplicación, corrupción, chip/memoria/protecciones incorrectos, fallo de lectura de seguridad, distintos estados previos de flash, fallo de borrado, escritura y verificación. No sustituyen un flasheo físico con cada combinación de dispositivo y navegador. Verifica una nueva integración USB en hardware antes de afirmar que ha sido probada.
