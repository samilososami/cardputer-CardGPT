import manifest from '../firmware.json';

export { manifest };
export const FIRMWARE_URL = '/firmware/CardGPT.bin';
export const RELEASE_URL = `https://github.com/samilososami/cardputer-CardGPT/releases/download/${manifest.release}/${manifest.filename}`;

export async function sha256(data: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', data.slice().buffer);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}

/** Only the pinned full image is accepted, never an old application-only asset. */
export async function validateFirmware(image: Uint8Array): Promise<void> {
  if (image.length !== manifest.size) throw new Error('Se requiere la imagen completa de CardGPT de 8 MB.');
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  for (const offset of [0, manifest.applicationOffset]) {
    if (image[offset] !== 0xe9 || view.getUint16(offset + 12, true) !== 9) {
      throw new Error('Bootloader o aplicación ESP32-S3 no válidos.');
    }
  }
  if (await sha256(image) !== manifest.sha256) {
    throw new Error('El SHA-256 del firmware no coincide. No se borrará ni escribirá el dispositivo.');
  }
}
