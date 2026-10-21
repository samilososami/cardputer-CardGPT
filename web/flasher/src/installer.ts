import type { ESPLoader } from 'esptool-js';
import SparkMD5 from 'spark-md5';
import {
  manifest,
  validateApplication,
  validateFactoryImage,
} from './firmware.ts';
import {
  PARTITION_TABLE_OFFSET,
  PARTITION_TABLE_SIZE,
  planLauncherInstall,
  type LauncherInstallPlan,
} from './partitions.ts';

type Device = Pick<ESPLoader, 'chip' | 'IS_STUB' | 'detectFlashSize' | 'readReg'>;
type Reader = Device & Pick<ESPLoader, 'readFlash'>;
type Installer = Reader & Pick<ESPLoader, 'eraseFlash' | 'writeFlash' | 'flashMd5sum'>;

export type StandaloneInstallPlan = {
  mode: 'standalone';
  reason: string;
};
export type InstallPlan = LauncherInstallPlan | StandaloneInstallPlan;
export type InstallPhase =
  | 'validating'
  | 'writing-app'
  | 'verifying-app'
  | 'writing-table'
  | 'verifying-table'
  | 'erasing'
  | 'writing-factory'
  | 'verifying-factory';

// ESP32-S3 BLOCK0 read registers, per Espressif esptool/targets/esp32s3.py.
// Reads only. Never write eFuses or override security restrictions.
export const CRYPT_CNT_REGISTER = 0x60007034;
export const SECURE_BOOT_REGISTER = 0x60007038;

const md5 = (bytes: Uint8Array): string =>
  SparkMD5.ArrayBuffer.hash(bytes.slice().buffer);

const equalBytes = (left: Uint8Array, right: Uint8Array): boolean =>
  left.length === right.length && left.every((value, index) => value === right[index]);

export async function assertCompatibleDevice(device: Device): Promise<void> {
  if (device.chip?.CHIP_NAME !== 'ESP32-S3') throw new Error('Chip incompatible. Se requiere ESP32-S3.');
  if (await device.detectFlashSize() !== '8MB') throw new Error('Flash incompatible. Se requieren 8 MB.');
  if (!device.IS_STUB) throw new Error('No se ha iniciado el cargador USB. Reconecta en modo descarga.');
  const crypt = await device.readReg(CRYPT_CNT_REGISTER);
  const secure = await device.readReg(SECURE_BOOT_REGISTER);
  if (!Number.isInteger(crypt) || !Number.isInteger(secure)) {
    throw new Error('No se pudo comprobar la seguridad del chip.');
  }
  if ((crypt & (7 << 18)) !== 0 || (secure & (1 << 20)) !== 0) {
    throw new Error('Secure Boot o configuración de cifrado detectados. Este dispositivo no se modificará.');
  }
}

export async function discoverInstallPlan(device: Reader): Promise<InstallPlan> {
  await assertCompatibleDevice(device);
  const table = await device.readFlash(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE);
  let launcherPlan: LauncherInstallPlan | null;
  try {
    launcherPlan = planLauncherInstall(table, manifest.application.size);
  } catch (error) {
    // A damaged table that still advertises M5Launcher's protected TEST/app0
    // partition must fail closed instead of being offered a full erase.
    const signature = new TextEncoder().encode('app0');
    let hasLauncherSignature = false;
    for (let at = 0; at + 32 <= table.length; at += 32) {
      if (table[at + 2] === 0 && table[at + 3] === 0x20 &&
          signature.every((byte, labelIndex) => table[at + 12 + labelIndex] === byte)) {
        hasLauncherSignature = true;
        break;
      }
    }
    if (hasLauncherSignature) throw error;
    return { mode: 'standalone', reason: 'No se ha detectado un M5Launcher compatible.' };
  }
  if (!launcherPlan) {
    return { mode: 'standalone', reason: 'No se ha detectado M5Launcher.' };
  }
  const launcherHeader = await device.readFlash(launcherPlan.launcher.offset, 16);
  const launcherView = new DataView(
    launcherHeader.buffer,
    launcherHeader.byteOffset,
    launcherHeader.byteLength,
  );
  if (launcherHeader[0] !== 0xe9 || launcherView.getUint16(12, true) !== 9) {
    throw new Error('La partición de M5Launcher no contiene una aplicación ESP32-S3 válida. No se modificará la flash.');
  }
  return launcherPlan;
}

const writeOne = async (
  device: Installer,
  data: Uint8Array,
  address: number,
  progress: (percent: number) => void,
): Promise<void> => {
  await device.writeFlash({
    fileArray: [{ data, address }],
    flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
    eraseAll: false, compress: true,
    reportProgress: (_index, written, total) => progress(written / total * 100),
    calculateMD5Hash: bytes => md5(bytes),
  });
};

export async function installIntoLauncher(
  device: Installer,
  plan: LauncherInstallPlan,
  application: Uint8Array,
  phase: (value: InstallPhase) => void,
  progress: (percent: number) => void,
): Promise<void> {
  phase('validating');
  await validateApplication(application);
  await assertCompatibleDevice(device);
  const currentTable = await device.readFlash(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE);
  if (!equalBytes(currentTable, plan.sourceTable)) {
    throw new Error('La tabla de particiones ha cambiado desde la comprobación. Reconecta y vuelve a intentarlo.');
  }

  phase('writing-app');
  await writeOne(device, application, plan.target.offset, value =>
    progress(value * (plan.tableChanged ? 0.9 : 1)));
  phase('verifying-app');
  const applicationMd5 = await device.flashMd5sum(plan.target.offset, application.length);
  if (applicationMd5.toLowerCase() !== md5(application)) {
    throw new Error('La verificación de CardGPT no coincide. M5Launcher y los demás firmwares no se han modificado.');
  }

  if (plan.tableChanged) {
    phase('writing-table');
    await writeOne(device, plan.targetTable, PARTITION_TABLE_OFFSET, value =>
      progress(90 + value * 0.1));
    phase('verifying-table');
    const tableMd5 = await device.flashMd5sum(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE);
    if (tableMd5.toLowerCase() !== md5(plan.targetTable)) {
      throw new Error('CardGPT se escribió, pero no se pudo verificar su registro en M5Launcher. Reconecta y repite el proceso.');
    }
  }
  progress(100);
}

/** Only used when discovery found no compatible M5Launcher and the UI confirms a clean install. */
export async function installFactoryImage(
  device: Installer,
  image: Uint8Array,
  phase: (value: InstallPhase) => void,
  progress: (percent: number) => void,
): Promise<void> {
  phase('validating');
  await validateFactoryImage(image);
  await assertCompatibleDevice(device);
  phase('erasing');
  await device.eraseFlash();
  phase('writing-factory');
  await writeOne(device, image, manifest.factory.offset, progress);
  phase('verifying-factory');
  const actual = await device.flashMd5sum(manifest.factory.offset, image.length);
  if (actual.toLowerCase() !== md5(image)) {
    throw new Error('La verificación final no coincide. Reconecta en modo descarga y repite la instalación limpia.');
  }
}
