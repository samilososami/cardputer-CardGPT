# Flasher de samilososami

Aplicación estática multipágina con Vite y TypeScript. No necesita backend, base de datos ni credenciales del usuario.

- `/`: catálogo de dispositivos.
- `/cardputer/`: descarga de CardGPT y actualización USB de instalaciones compatibles.
- `/firmware/CardGPT.bin`: asset verificado contra la release pública.

## Desarrollo

```bash
npm ci
npm run firmware
npm run dev
```

`npm run build` ejecuta las pruebas de seguridad de particiones, verifica el binario, comprueba TypeScript y genera `dist/`.

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

El binario no se versiona en Git: en una compilación limpia se descarga desde GitHub Releases y se valida antes de publicar. Al actualizar la versión deben cambiarse de forma coordinada la URL y hash del script de descarga, tamaño y hash del cliente, contenido de las páginas y SHA256SUMS. Nunca publiques un firmware sin comprobar la imagen y sus límites.

## Límite de las pruebas

Las pruebas automáticas cubren tablas válidas, destinos diferentes, corrupción, espacio insuficiente, solapamientos, flags, particiones de datos y NVS ausente. No sustituyen un flasheo físico con cada combinación de dispositivo, bootloader y navegador. Verifica una nueva integración USB en hardware antes de afirmar que ha sido probada.
