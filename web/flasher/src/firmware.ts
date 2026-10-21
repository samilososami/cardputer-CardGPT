import manifest from '../firmware.json';

export { manifest };
export const APPLICATION_URL = `/firmware/${manifest.application.filename}`;
export const FACTORY_URL = `/firmware/${manifest.factory.filename}`;
export const APPLICATION_RELEASE_URL = `https://github.com/samilososami/cardputer-CardGPT/releases/download/${manifest.application.release}/${manifest.application.asset}`;
export const FACTORY_RELEASE_URL = `https://github.com/samilososami/cardputer-CardGPT/releases/download/${manifest.factory.release}/${manifest.factory.asset}`;

export async function sha256(data: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', data.slice().buffer);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}

export async function validateApplication(image: Uint8Array): Promise<void> {
  if (image.length !== manifest.application.size) throw new Error('El binario de aplicación de CardGPT tiene un tamaño inesperado.');
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  if (image[0] !== 0xe9 || view.getUint16(12, true) !== 9) {
    throw new Error('La aplicación no es una imagen ESP32-S3 válida.');
  }
  if (await sha256(image) !== manifest.application.sha256) {
    throw new Error('El SHA-256 de la aplicación no coincide. No se escribirá el dispositivo.');
  }
}

export async function validateFactoryImage(image: Uint8Array): Promise<void> {
  if (image.length !== manifest.factory.size) throw new Error('Se requiere la imagen completa de CardGPT de 8 MB.');
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  for (const offset of [0, manifest.factory.applicationOffset]) {
    if (image[offset] !== 0xe9 || view.getUint16(offset + 12, true) !== 9) {
      throw new Error('Bootloader o aplicación ESP32-S3 no válidos.');
    }
  }
  if (await sha256(image) !== manifest.factory.sha256) {
    throw new Error('El SHA-256 de la imagen completa no coincide. No se borrará ni escribirá el dispositivo.');
  }
  const application = image.slice(
    manifest.factory.applicationOffset,
    manifest.factory.applicationOffset + manifest.application.size,
  );
  if (await sha256(application) !== manifest.application.sha256) {
    throw new Error('La aplicación incluida en la imagen completa no coincide con CardGPT v0.2.8.');
  }
}
