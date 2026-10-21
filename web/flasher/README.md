# Flasher de samilososami

Aplicación estática multipágina con Vite y TypeScript. No necesita backend, base de datos ni credenciales del usuario.

- `/`: catálogo de dispositivos.
- `/cardputer/`: instalador híbrido de CardGPT por USB.
- `/firmware/CardGPT.bin`: aplicación para M5Launcher.
- `/firmware/CardGPT-full.bin`: imagen independiente de 8 MiB.

## Desarrollo

```bash
npm ci
npm run firmware
npm run dev
```

`npm run build` verifica ambos binarios, ejecuta las pruebas del instalador, comprueba TypeScript y genera `dist/`. `npm run firmware` prepara los archivos que usan las pruebas.

## Selección del modo

`src/partitions.ts` valida el MD5 y los límites de la tabla. Reconoce M5Launcher por su partición protegida `app0` de tipo TEST, `otadata`, NVS y una cabecera ESP32-S3 válida. Si existe CardGPT, la actualiza; si no, crea una OTA en el primer rango libre alineado y asigna el siguiente subtipo. No reemplaza, mueve ni reduce otras particiones. Sin hueco, bloquea la operación.

`src/installer.ts` escribe primero la aplicación y verifica su MD5; solo entonces registra una ranura nueva y verifica el sector de tabla. Si no existe un M5Launcher compatible, la interfaz ofrece una instalación limpia separada. Se exige ESP32-S3, 8 MB, stub activo y registros de seguridad legibles sin Secure Boot ni contador de cifrado provisionado. No se escribe ningún eFuse.

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

Los binarios no se versionan en Git: en una compilación limpia se descargan desde GitHub Releases y se validan antes de publicar. `firmware.json` fija cada release, nombre, tamaño y hash; lo comparten cliente, pruebas y script de descarga. Este genera también `SHA256SUMS` para ambos formatos.

## Límite de las pruebas

Las pruebas automáticas usan ambos binarios reales y una flash simulada. Cubren instalación y actualización con M5Launcher, preservación byte a byte de NVS, launcher, otras apps y datos, falta de espacio, carrera en la tabla, corrupción, chip/memoria/protecciones incorrectos y fallos de escritura o verificación. También prueban por separado la instalación limpia. No sustituyen un flasheo físico con cada versión de launcher y navegador.
