import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { manifest, RELEASE_URL, validateFirmware } from '../src/firmware.ts';

const directory = new URL('../public/firmware/', import.meta.url);
const target = new URL(manifest.filename, directory);
let data: Uint8Array | undefined;
try {
  const cached = await readFile(target);
  await validateFirmware(cached);
  data = cached;
} catch { /* Old app-only file, stale version or fresh checkout: fetch the pinned factory release. */ }
if (!data) {
  const response = await fetch(RELEASE_URL);
  if (!response.ok) throw new Error(`No se pudo descargar CardGPT: HTTP ${response.status}`);
  data = new Uint8Array(await response.arrayBuffer());
}
await validateFirmware(data);
await mkdir(directory, { recursive: true });
await writeFile(target, data);
await writeFile(new URL('SHA256SUMS', directory), `${manifest.sha256}  ${manifest.filename}\n`);
console.log(`${manifest.filename} ${manifest.release}: imagen completa de ${data.length} bytes verificada.`);
