import type { ESPLoader } from 'esptool-js';
import SparkMD5 from 'spark-md5';
import { manifest, validateFirmware } from './firmware.ts';

type Device = Pick<ESPLoader, 'chip' | 'IS_STUB' | 'detectFlashSize' | 'readReg'>;
type Installer = Device & Pick<ESPLoader, 'eraseFlash' | 'writeFlash' | 'flashMd5sum'>;
export type InstallPhase = 'validating' | 'erasing' | 'writing' | 'verifying';

// ESP32-S3 BLOCK0 read registers, per Espressif esptool/targets/esp32s3.py.
// Reads only. Never write eFuses or override security restrictions.
export const CRYPT_CNT_REGISTER = 0x60007034;
export const SECURE_BOOT_REGISTER = 0x60007038;

export async function assertCompatibleDevice(device: Device): Promise<void> {
  if (device.chip?.CHIP_NAME !== 'ESP32-S3') throw new Error('Chip incompatible. Se requiere ESP32-S3.');
  if (await device.detectFlashSize() !== '8MB') throw new Error('Flash incompatible. Se requieren 8 MB.');
  if (!device.IS_STUB) throw new Error('No se ha iniciado el cargador USB. Reconecta en modo descarga.');
  const crypt = await device.readReg(CRYPT_CNT_REGISTER);
  const secure = await device.readReg(SECURE_BOOT_REGISTER);
  if (!Number.isInteger(crypt) || !Number.isInteger(secure)) throw new Error('No se pudo comprobar la seguridad del chip.');
  // Refuse any provisioned crypt counter, even the development/disabled states.
  if ((crypt & (7 << 18)) !== 0 || (secure & (1 << 20)) !== 0) {
    throw new Error('Secure Boot o configuración de cifrado detectados. No se admite el borrado de este dispositivo.');
  }
}

/** Called only after the UI's explicit destructive confirmation. No old layout is required. */
export async function installFactoryImage(
  device: Installer,
  image: Uint8Array,
  phase: (value: InstallPhase) => void,
  progress: (percent: number) => void,
): Promise<void> {
  phase('validating');
  await validateFirmware(image);
  await assertCompatibleDevice(device);
  phase('erasing');
  await device.eraseFlash();
  phase('writing');
  await device.writeFlash({
    fileArray: [{ data: image, address: manifest.offset }],
    flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
    // Erasure is explicit above, so its failure cannot silently become a write.
    eraseAll: false, compress: true,
    reportProgress: (_index, written, total) => progress(written / total * 100),
    calculateMD5Hash: bytes => SparkMD5.ArrayBuffer.hash(bytes.slice().buffer),
  });
  phase('verifying');
  const expected = SparkMD5.ArrayBuffer.hash(image.slice().buffer);
  const actual = await device.flashMd5sum(manifest.offset, image.length);
  if (actual.toLowerCase() !== expected) {
    throw new Error('La verificación final no coincide. Reconecta en modo descarga y repite la instalación.');
  }
}
