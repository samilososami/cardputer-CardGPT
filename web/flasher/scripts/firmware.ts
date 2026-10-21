import { readFile, mkdir, writeFile } from 'node:fs/promises';
import {
  APPLICATION_RELEASE_URL,
  FACTORY_RELEASE_URL,
  manifest,
  validateApplication,
  validateFactoryImage,
} from '../src/firmware.ts';

const directory = new URL('../public/firmware/', import.meta.url);
await mkdir(directory, { recursive: true });

const obtain = async (
  filename: string,
  releaseUrl: string,
  validate: (data: Uint8Array) => Promise<void>,
): Promise<Uint8Array> => {
  const target = new URL(filename, directory);
  try {
    const cached = new Uint8Array(await readFile(target));
    await validate(cached);
    return cached;
  } catch { /* Archivo ausente, antiguo o de otro formato: descarga la release fijada. */ }
  const response = await fetch(releaseUrl);
  if (!response.ok) throw new Error(`No se pudo descargar ${filename}: HTTP ${response.status}`);
  const data = new Uint8Array(await response.arrayBuffer());
  await validate(data);
  await writeFile(target, data);
  return data;
};

const application = await obtain(
  manifest.application.filename,
  APPLICATION_RELEASE_URL,
  validateApplication,
);
const factory = await obtain(
  manifest.factory.filename,
  FACTORY_RELEASE_URL,
  validateFactoryImage,
);
await writeFile(
  new URL('SHA256SUMS', directory),
  `${manifest.application.sha256}  ${manifest.application.filename}\n${manifest.factory.sha256}  ${manifest.factory.filename}\n`,
);
console.log(
  `CardGPT ${manifest.version}: aplicación ${application.length} bytes + imagen completa ${factory.length} bytes verificadas.`,
);
