import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import type { ESPLoader } from 'esptool-js';
import { manifest, sha256, validateFirmware } from '../src/firmware.ts';
import { assertCompatibleDevice, installFactoryImage, CRYPT_CNT_REGISTER, SECURE_BOOT_REGISTER } from '../src/installer.ts';

const image = new Uint8Array(await readFile(new URL('../public/firmware/CardGPT.bin', import.meta.url)));
const md5 = (bytes: Uint8Array): string => createHash('md5').update(bytes).digest('hex');

type Options = {
  chip?: string; flashSize?: string; stub?: boolean; crypt?: number; secure?: number;
  priorByte?: number; fail?: 'read' | 'erase' | 'write' | 'verify';
};
function fakeDevice(options: Options = {}) {
  const events: string[] = [];
  const flash = new Uint8Array(manifest.size).fill(options.priorByte ?? 0xa5);
  const device = {
    chip: { CHIP_NAME: options.chip ?? 'ESP32-S3' } as ESPLoader['chip'],
    IS_STUB: options.stub ?? true,
    async detectFlashSize() { events.push('size'); return options.flashSize ?? '8MB'; },
    async readReg(address: number) {
      events.push('security');
      if (options.fail === 'read') throw new Error('security read failed');
      if (address === CRYPT_CNT_REGISTER) return options.crypt ?? 0;
      assert.equal(address, SECURE_BOOT_REGISTER);
      return options.secure ?? 0;
    },
    // Intentionally no readFlash(): install cannot depend on a pre-existing table.
    async eraseFlash() {
      events.push('erase');
      if (options.fail === 'erase') throw new Error('erase failed');
      flash.fill(0xff);
      return 0;
    },
    async writeFlash(config: Parameters<ESPLoader['writeFlash']>[0]) {
      events.push('write');
      assert.deepEqual(events.slice(0, 4), ['size', 'security', 'security', 'erase']);
      assert.equal(config.eraseAll, false); // explicit erase must already have succeeded
      assert.equal(config.flashMode, 'keep');
      assert.equal(config.flashFreq, 'keep');
      assert.equal(config.flashSize, 'keep');
      assert.equal(config.compress, true);
      assert.equal(config.fileArray.length, 1);
      const entry = config.fileArray[0];
      assert.equal(entry.address, 0);
      assert.equal(entry.data.length, 0x800000);
      assert.equal(config.calculateMD5Hash?.(entry.data), md5(image));
      if (options.fail === 'write') throw new Error('write failed');
      flash.set(entry.data, entry.address);
      config.reportProgress?.(0, 4, 8);
      config.reportProgress?.(0, 8, 8);
    },
    async flashMd5sum(offset: number, size: number) {
      events.push('verify');
      assert.equal(offset, 0);
      assert.equal(size, manifest.size);
      return options.fail === 'verify' ? '0'.repeat(32) : md5(flash).toUpperCase();
    },
  };
  return { device, events, flash };
}

test('pinned full release, bootloader, table and application are present', async () => {
  await validateFirmware(image);
  assert.equal(image.length, 8 * 1024 * 1024);
  assert.equal(image[0], 0xe9);
  assert.equal(image[0x8000], 0xaa);
  assert.equal(image[0x8001], 0x50);
  assert.equal(image[0x10000], 0xe9);
  assert.equal(await sha256(image.slice(manifest.applicationOffset, manifest.applicationOffset + manifest.applicationSize)), manifest.applicationSha256);
  assert.ok(image.slice(0x9000, 0xe000).every(value => value === 0xff), 'NVS must be blank');
  assert.ok(image.slice(0x10000 + manifest.applicationSize).every(value => value === 0xff), 'unused flash must be blank');
});
test('old application-only download is never accepted at 0x0', async () => {
  await assert.rejects(validateFirmware(image.slice(0x10000, 0x10000 + manifest.applicationSize)), /completa/);
});
test('truncated or oversized factory files are rejected', async () => {
  await assert.rejects(validateFirmware(image.slice(0, -1)), /completa/);
  await assert.rejects(validateFirmware(new Uint8Array(manifest.size + 1)), /completa/);
});
for (const offset of [0, 0x10000]) {
  test(`rejects invalid ESP header at ${offset}`, async () => {
    const bad = image.slice(); bad[offset] = 0;
    await assert.rejects(validateFirmware(bad), /no válidos/);
  });
  test(`rejects non-S3 chip ID at ${offset}`, async () => {
    const bad = image.slice(); bad[offset + 12] = 0;
    await assert.rejects(validateFirmware(bad), /no válidos/);
  });
}
for (const offset of [0x100, 0x8000, 0x9000, 0xe000, 0x10100, 0x7fffff]) {
  test(`corruption at 0x${offset.toString(16)} aborts before erasure`, async () => {
    const bad = image.slice(); bad[offset] ^= 1;
    const { device, events } = fakeDevice();
    await assert.rejects(installFactoryImage(device, bad, () => {}, () => {}), /SHA-256/);
    assert.deepEqual(events, []);
  });
}
for (const [label, options] of Object.entries({
  'other chip': { chip: 'ESP32-S2' }, 'wrong flash': { flashSize: '4MB' },
  'missing stub': { stub: false }, 'secure boot': { secure: 1 << 20 },
  'encryption': { crypt: 1 << 18 }, 'development encryption counter': { crypt: 3 << 18 },
  'invalid register': { crypt: NaN }, 'unreadable security': { fail: 'read' as const },
})) {
  test(`rejects ${label} without any flash write or erase`, async () => {
    const { device, events } = fakeDevice(options);
    await assert.rejects(installFactoryImage(device, image, () => {}, () => {}));
    assert.ok(!events.includes('erase'));
    assert.ok(!events.includes('write'));
  });
}
test('connection compatibility checks are non-destructive', async () => {
  const { device, events } = fakeDevice();
  await assertCompatibleDevice(device);
  assert.deepEqual(events, ['size', 'security', 'security']);
});
for (const priorByte of [0xff, 0x00, 0xa5]) {
  test(`clean install ignores arbitrary prior flash (0x${priorByte.toString(16)})`, async () => {
    const { device, events, flash } = fakeDevice({ priorByte });
    const phases: string[] = [], progress: number[] = [];
    await installFactoryImage(device, image, p => phases.push(p), n => progress.push(n));
    assert.deepEqual(events, ['size', 'security', 'security', 'erase', 'write', 'verify']);
    assert.deepEqual(phases, ['validating', 'erasing', 'writing', 'verifying']);
    assert.deepEqual(progress, [50, 100]);
    assert.deepEqual(flash, image);
    assert.ok(flash.slice(0x9000, 0xe000).every(value => value === 0xff));
  });
}
test('failed erase prevents write', async () => {
  const { device, events } = fakeDevice({ fail: 'erase' });
  await assert.rejects(installFactoryImage(device, image, () => {}, () => {}), /erase failed/);
  assert.ok(!events.includes('write'));
});
test('failed write is not declared verified', async () => {
  const { device, events } = fakeDevice({ fail: 'write' });
  await assert.rejects(installFactoryImage(device, image, () => {}, () => {}), /write failed/);
  assert.ok(!events.includes('verify'));
});
test('MD5 mismatch is an installation failure', async () => {
  const { device } = fakeDevice({ fail: 'verify' });
  await assert.rejects(installFactoryImage(device, image, () => {}, () => {}), /verificación final/);
});
