import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
const target = new URL('../public/firmware/CardGPT.bin', import.meta.url);
const hash = '5b89f2140a08ca197fda0f0f1de116e016b45e61635561bdc0bec874dc2f1067';
let data;
try { data = await readFile(target); } catch { /* Fresh checkout. */ }
if (!data) {
  const response = await fetch('https://github.com/samilososami/cardputer-CardGPT/releases/download/v0.2.8/CardGPT.bin');
  if (!response.ok) throw new Error(`No se pudo descargar CardGPT: HTTP ${response.status}`);
  data = Buffer.from(await response.arrayBuffer());
}
if (data.length !== 1588480 || createHash('sha256').update(data).digest('hex') !== hash) {
  throw new Error('CardGPT.bin no coincide con la release v0.2.8. No se publicará.');
}
await mkdir(new URL('../public/firmware/', import.meta.url), { recursive: true });
await writeFile(target, data);
console.log('CardGPT.bin v0.2.8 verificado: 1.588.480 bytes.');
