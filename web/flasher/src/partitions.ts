import SparkMD5 from 'spark-md5';

export const FLASH_SIZE = 8 * 1024 * 1024;
export const PARTITION_TABLE_OFFSET = 0x8000;
export const PARTITION_TABLE_SIZE = 0x1000;
export const APP_ALIGNMENT = 0x10000;

export type Partition = {
  type: number;
  subtype: number;
  offset: number;
  size: number;
  label: string;
  flags: number;
};

export type LauncherInstallPlan = {
  mode: 'launcher';
  action: 'install' | 'update';
  launcher: Partition;
  target: Partition;
  sourceTable: Uint8Array;
  targetTable: Uint8Array;
  tableChanged: boolean;
};

const alignUp = (value: number, alignment: number): number =>
  Math.ceil(value / alignment) * alignment;

const hexToBytes = (hex: string): Uint8Array => {
  const output = new Uint8Array(hex.length / 2);
  for (let index = 0; index < output.length; index += 1) {
    output[index] = Number.parseInt(hex.slice(index * 2, index * 2 + 2), 16);
  }
  return output;
};

const md5 = (bytes: Uint8Array): string =>
  SparkMD5.ArrayBuffer.hash(bytes.slice().buffer);

export function parsePartitionTable(bytes: Uint8Array): Partition[] {
  if (bytes.length !== PARTITION_TABLE_SIZE) {
    throw new Error('Lectura incompleta de la tabla de particiones.');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const partitions: Partition[] = [];
  let checksumVerified = false;

  for (let at = 0; at + 32 <= bytes.length; at += 32) {
    const magic = view.getUint16(at, true);
    if (magic === 0xebeb) {
      const expected = Array.from(bytes.slice(at + 16, at + 32), byte =>
        byte.toString(16).padStart(2, '0')).join('');
      if (md5(bytes.subarray(0, at)) !== expected) {
        throw new Error('La tabla de particiones tiene un checksum incorrecto.');
      }
      if (!bytes.slice(at + 32).every(byte => byte === 0xff)) {
        throw new Error('Hay datos inesperados después de la tabla de particiones.');
      }
      checksumVerified = true;
      break;
    }
    if (magic !== 0x50aa) {
      throw new Error('Tabla de particiones no reconocida.');
    }

    const labelBytes = bytes.slice(at + 12, at + 28);
    const nul = labelBytes.indexOf(0);
    const label = new TextDecoder().decode(labelBytes.slice(0, nul < 0 ? 16 : nul));
    const partition: Partition = {
      type: bytes[at + 2],
      subtype: bytes[at + 3],
      offset: view.getUint32(at + 4, true),
      size: view.getUint32(at + 8, true),
      label,
      flags: view.getUint32(at + 28, true),
    };
    if (!partition.label || !partition.size || partition.offset < 0x9000 ||
        partition.offset % 0x1000 !== 0 || partition.size % 0x1000 !== 0 ||
        partition.offset + partition.size > FLASH_SIZE) {
      throw new Error('La tabla contiene una partición fuera de los límites seguros.');
    }
    if (partitions.some(existing =>
      partition.offset < existing.offset + existing.size &&
      existing.offset < partition.offset + partition.size)) {
      throw new Error('La tabla contiene particiones solapadas.');
    }
    partitions.push(partition);
  }

  if (!checksumVerified) {
    throw new Error('No se ha podido verificar el checksum de la tabla de particiones.');
  }
  return partitions;
}

export function buildPartitionTable(partitions: Partition[]): Uint8Array {
  if (partitions.length > 94) throw new Error('La tabla de particiones está llena.');
  const output = new Uint8Array(PARTITION_TABLE_SIZE).fill(0xff);
  const view = new DataView(output.buffer);
  const encoder = new TextEncoder();
  partitions.forEach((partition, index) => {
    const at = index * 32;
    const label = encoder.encode(partition.label);
    if (!partition.label || label.length > 15) throw new Error('Nombre de partición no válido.');
    output.fill(0, at, at + 32);
    view.setUint16(at, 0x50aa, true);
    output[at + 2] = partition.type;
    output[at + 3] = partition.subtype;
    view.setUint32(at + 4, partition.offset, true);
    view.setUint32(at + 8, partition.size, true);
    output.set(label, at + 12);
    view.setUint32(at + 28, partition.flags, true);
  });
  const md5At = partitions.length * 32;
  output[md5At] = 0xeb;
  output[md5At + 1] = 0xeb;
  output.fill(0xff, md5At + 2, md5At + 16);
  output.set(hexToBytes(md5(output.subarray(0, md5At))), md5At + 16);
  return output;
}

const isOtaApp = (partition: Partition): boolean =>
  partition.type === 0 && partition.subtype >= 0x10 && partition.subtype <= 0x1f;

const findFreeRange = (partitions: Partition[], requiredSize: number): number | null => {
  const sorted = [...partitions].sort((left, right) => left.offset - right.offset);
  let cursor = 0x10000;
  for (const partition of sorted) {
    if (partition.offset + partition.size <= cursor) continue;
    const candidate = alignUp(cursor, APP_ALIGNMENT);
    if (candidate + requiredSize <= partition.offset) return candidate;
    cursor = Math.max(cursor, partition.offset + partition.size);
  }
  const candidate = alignUp(cursor, APP_ALIGNMENT);
  return candidate + requiredSize <= FLASH_SIZE ? candidate : null;
};

/**
 * Reproduces M5Launcher's safe, non-destructive path: update CardGPT if it is
 * already present, otherwise append one OTA partition in genuinely free flash.
 * Existing partitions are never resized, moved, renamed or removed.
 */
export function planLauncherInstall(
  sourceTable: Uint8Array,
  applicationSize: number,
): LauncherInstallPlan | null {
  const partitions = parsePartitionTable(sourceTable);
  const launchers = partitions.filter(partition =>
    partition.type === 0 && partition.subtype === 0x20 &&
    partition.label.toLowerCase() === 'app0');
  if (launchers.length === 0) return null;
  if (launchers.length !== 1 || launchers[0].offset !== 0x10000 || launchers[0].flags !== 0) {
    throw new Error('Se ha detectado un layout de launcher no compatible. No se modificará la flash.');
  }
  if (!partitions.some(partition => partition.type === 1 && partition.subtype === 0 && partition.size >= 0x2000)) {
    throw new Error('M5Launcher no tiene una partición otadata compatible.');
  }
  if (!partitions.some(partition => partition.type === 1 && partition.subtype === 2 && partition.size >= 0x5000)) {
    throw new Error('M5Launcher no tiene una partición NVS compatible.');
  }

  const requiredSize = alignUp(applicationSize, APP_ALIGNMENT);
  const existing = partitions.filter(partition =>
    ['cardgpt', 'cardgp'].includes(partition.label.toLowerCase()));
  if (existing.length > 1) {
    throw new Error('Hay varias particiones que parecen CardGPT. No se modificará ninguna.');
  }
  if (existing.length === 1) {
    const target = existing[0];
    if (!isOtaApp(target) || target.flags !== 0 || target.offset % APP_ALIGNMENT !== 0) {
      throw new Error('La partición CardGPT existente no es una aplicación OTA segura.');
    }
    if (target.size < requiredSize) {
      throw new Error('La partición CardGPT existente es demasiado pequeña. Reinstálala desde M5Launcher.');
    }
    return {
      mode: 'launcher', action: 'update', launcher: launchers[0], target,
      sourceTable: sourceTable.slice(), targetTable: sourceTable.slice(), tableChanged: false,
    };
  }

  const usedSubtypes = new Set(partitions.filter(isOtaApp).map(partition => partition.subtype));
  let subtype = 0x10;
  while (subtype <= 0x1f && usedSubtypes.has(subtype)) subtype += 1;
  if (subtype > 0x1f) {
    throw new Error('M5Launcher ya utiliza todas las ranuras OTA disponibles. No se borrará ningún firmware.');
  }
  const offset = findFreeRange(partitions, requiredSize);
  if (offset === null) {
    throw new Error('No hay espacio libre para CardGPT sin borrar otro firmware. Libera una app desde M5Launcher y vuelve a intentarlo.');
  }
  const target: Partition = {
    type: 0, subtype, offset, size: requiredSize, label: 'CardGPT', flags: 0,
  };
  const targetPartitions = [...partitions, target].sort((left, right) => left.offset - right.offset);
  return {
    mode: 'launcher', action: 'install', launcher: launchers[0], target,
    sourceTable: sourceTable.slice(), targetTable: buildPartitionTable(targetPartitions), tableChanged: true,
  };
}
