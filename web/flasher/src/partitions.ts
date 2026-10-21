import SparkMD5 from 'spark-md5';

export type Partition = { type: number; subtype: number; offset: number; size: number; label: string; flags: number };
export const FLASH_SIZE = 8 * 1024 * 1024;
export const FIRMWARE_SIZE = 1_588_480;

/** Fail closed: we never infer a writable destination from an empty address. */
export function findCardGPTPartition(bytes: Uint8Array): Partition {
  if (bytes.length !== 0xc00) throw new Error('Lectura incompleta de la tabla de particiones.');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const partitions: Partition[] = [];
  let checksumVerified = false;
  for (let at = 0; at + 32 <= bytes.length; at += 32) {
    const magic = view.getUint16(at, true);
    if (magic === 0xebeb) {
      const digest = SparkMD5.ArrayBuffer.hash(new Uint8Array(bytes.subarray(0, at)).buffer);
      const expected = Array.from(bytes.slice(at + 16, at + 32), b => b.toString(16).padStart(2, '0')).join('');
      if (digest !== expected) throw new Error('La tabla de particiones tiene un checksum incorrecto.');
      if (!bytes.slice(at + 32).every(b => b === 0xff)) throw new Error('Datos inesperados después de la tabla de particiones.');
      checksumVerified = true;
      break;
    }
    if (magic !== 0x50aa) throw new Error('Tabla de particiones no reconocida. No se escribirá en el dispositivo.');
    const labelBytes = bytes.slice(at + 12, at + 28);
    const nul = labelBytes.indexOf(0);
    const p: Partition = {
      type: bytes[at + 2], subtype: bytes[at + 3], offset: view.getUint32(at + 4, true),
      size: view.getUint32(at + 8, true), label: new TextDecoder().decode(labelBytes.slice(0, nul < 0 ? 16 : nul)),
      flags: view.getUint32(at + 28, true),
    };
    if (!p.size || p.offset < 0x9000 || p.offset % 0x1000 || p.size % 0x1000 || p.offset + p.size > FLASH_SIZE) {
      throw new Error('La tabla contiene una partición fuera de los límites seguros.');
    }
    if (partitions.some(q => p.offset < q.offset + q.size && q.offset < p.offset + p.size)) {
      throw new Error('La tabla contiene particiones solapadas.');
    }
    partitions.push(p);
  }
  if (!checksumVerified) throw new Error('No se ha podido verificar el checksum de la tabla de particiones.');
  const matches = partitions.filter(p => p.label === 'CardGPT');
  if (matches.length !== 1) throw new Error('No hay una única partición CardGPT. Haz la primera instalación desde M5Launcher.');
  const target = matches[0];
  if (target.type !== 0 || target.subtype < 0x10 || target.subtype > 0x1f || target.offset < 0x10000 || target.offset % 0x10000) {
    throw new Error('CardGPT no es una partición de aplicación OTA compatible.');
  }
  if (target.flags !== 0) throw new Error('No se admite una partición cifrada o con flags especiales.');
  if (target.size < Math.ceil(FIRMWARE_SIZE / 4096) * 4096) throw new Error('No hay espacio suficiente en la partición CardGPT.');
  if (!partitions.some(p => p.type === 1 && p.subtype === 2 && p.size >= 0x5000)) {
    throw new Error('No se encuentra NVS compatible para las preferencias de CardGPT.');
  }
  return target;
}
