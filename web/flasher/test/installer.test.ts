import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import type { ESPLoader } from 'esptool-js';
import {
  manifest,
  sha256,
  validateApplication,
  validateFactoryImage,
} from '../src/firmware.ts';
import {
  CRYPT_CNT_REGISTER,
  SECURE_BOOT_REGISTER,
  assertCompatibleDevice,
  discoverInstallPlan,
  installFactoryImage,
  installIntoLauncher,
} from '../src/installer.ts';
import {
  FLASH_SIZE,
  PARTITION_TABLE_OFFSET,
  buildPartitionTable,
  parsePartitionTable,
  type Partition,
} from '../src/partitions.ts';

const application = new Uint8Array(await readFile(new URL('../public/firmware/CardGPT.bin', import.meta.url)));
const factory = new Uint8Array(await readFile(new URL('../public/firmware/CardGPT-full.bin', import.meta.url)));
const md5 = (bytes: Uint8Array): string => createHash('md5').update(bytes).digest('hex');

const launcherEntries = (cardgpt = false): Partition[] => [
  { label: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000, flags: 0 },
  { label: 'otadata', type: 1, subtype: 0, offset: 0xe000, size: 0x2000, flags: 0 },
  { label: 'app0', type: 0, subtype: 0x20, offset: 0x10000, size: 0x150000, flags: 0 },
  { label: 'coredump', type: 1, subtype: 3, offset: 0x160000, size: 0x10000, flags: 0 },
  { label: 'Bruce', type: 0, subtype: 0x10, offset: 0x300000, size: 0x200000, flags: 0 },
  ...(cardgpt ? [{ label: 'CardGPT', type: 0, subtype: 0x11, offset: 0x500000, size: 0x200000, flags: 0 }] : []),
  { label: 'storage', type: 1, subtype: 0x81, offset: 0x700000, size: 0x100000, flags: 0 },
];

type Options = {
  entries?: Partition[] | null;
  chip?: string;
  flashSize?: string;
  stub?: boolean;
  crypt?: number;
  secure?: number;
  fail?: 'security' | 'erase' | 'write-app' | 'write-table' | 'verify-app' | 'verify-table' | 'verify-factory';
};

function fakeDevice(options: Options = {}) {
  const events: string[] = [];
  const flash = new Uint8Array(FLASH_SIZE).fill(0xff);
  const entries = options.entries === undefined ? launcherEntries() : options.entries;
  if (entries) {
    flash.set(buildPartitionTable(entries), PARTITION_TABLE_OFFSET);
    const launcher = entries.find(entry => entry.label === 'app0' && entry.subtype === 0x20);
    if (launcher) {
      flash.fill(0x41, launcher.offset, launcher.offset + launcher.size);
      flash[launcher.offset] = 0xe9;
      flash[launcher.offset + 12] = 9;
      flash[launcher.offset + 13] = 0;
    }
    const bruce = entries.find(entry => entry.label === 'Bruce');
    if (bruce) flash.fill(0xb4, bruce.offset, bruce.offset + bruce.size);
    const nvs = entries.find(entry => entry.label === 'nvs');
    if (nvs) flash.fill(0x6e, nvs.offset, nvs.offset + nvs.size);
    const storage = entries.find(entry => entry.label === 'storage');
    if (storage) flash.fill(0x73, storage.offset, storage.offset + storage.size);
  }
  const device = {
    chip: { CHIP_NAME: options.chip ?? 'ESP32-S3' } as ESPLoader['chip'],
    IS_STUB: options.stub ?? true,
    async detectFlashSize() { events.push('size'); return options.flashSize ?? '8MB'; },
    async readReg(address: number) {
      events.push('security');
      if (options.fail === 'security') throw new Error('security read failed');
      if (address === CRYPT_CNT_REGISTER) return options.crypt ?? 0;
      assert.equal(address, SECURE_BOOT_REGISTER);
      return options.secure ?? 0;
    },
    async readFlash(address: number, size: number) {
      events.push(`read:${address.toString(16)}:${size.toString(16)}`);
      return flash.slice(address, address + size);
    },
    async eraseFlash() {
      events.push('erase');
      if (options.fail === 'erase') throw new Error('erase failed');
      flash.fill(0xff);
      return 0;
    },
    async writeFlash(config: Parameters<ESPLoader['writeFlash']>[0]) {
      const entry = config.fileArray[0];
      const isTable = entry.address === PARTITION_TABLE_OFFSET;
      const isFactory = entry.address === 0 && entry.data.length === FLASH_SIZE;
      events.push(isTable ? 'write-table' : isFactory ? 'write-factory' : 'write-app');
      assert.equal(config.eraseAll, false);
      assert.equal(config.flashMode, 'keep');
      assert.equal(config.flashFreq, 'keep');
      assert.equal(config.flashSize, 'keep');
      assert.equal(config.calculateMD5Hash?.(entry.data), md5(entry.data));
      if ((isTable && options.fail === 'write-table') ||
          (!isTable && !isFactory && options.fail === 'write-app')) throw new Error('write failed');
      flash.set(entry.data, entry.address);
      config.reportProgress?.(0, entry.data.length, entry.data.length);
    },
    async flashMd5sum(address: number, size: number) {
      const isTable = address === PARTITION_TABLE_OFFSET;
      const isFactory = address === 0 && size === FLASH_SIZE;
      events.push(isTable ? 'verify-table' : isFactory ? 'verify-factory' : 'verify-app');
      if ((isTable && options.fail === 'verify-table') ||
          (isFactory && options.fail === 'verify-factory') ||
          (!isTable && !isFactory && options.fail === 'verify-app')) return '0'.repeat(32);
      return md5(flash.slice(address, address + size)).toUpperCase();
    },
  };
  return { device, events, flash };
}

test('both pinned binaries are valid and contain the same CardGPT application', async () => {
  await validateApplication(application);
  await validateFactoryImage(factory);
  assert.equal(application.length, manifest.application.size);
  assert.equal(factory.length, manifest.factory.size);
  assert.equal(await sha256(application), manifest.application.sha256);
  assert.deepEqual(
    factory.slice(manifest.factory.applicationOffset, manifest.factory.applicationOffset + application.length),
    application,
  );
});

test('detects M5Launcher without writing or erasing', async () => {
  const { device, events } = fakeDevice();
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'launcher');
  assert.ok(!events.some(event => event.startsWith('write') || event === 'erase'));
});

test('new M5Launcher install changes only CardGPT and the partition-table sector', async () => {
  const { device, flash, events } = fakeDevice();
  const before = flash.slice();
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'launcher');
  if (plan.mode !== 'launcher') return;
  await installIntoLauncher(device, plan, application, () => {}, () => {});
  assert.ok(!events.includes('erase'));
  assert.deepEqual(flash.slice(0x9000, 0xe000), before.slice(0x9000, 0xe000), 'NVS changed');
  assert.deepEqual(flash.slice(0x10000, 0x160000), before.slice(0x10000, 0x160000), 'M5Launcher changed');
  assert.deepEqual(flash.slice(0x300000, 0x500000), before.slice(0x300000, 0x500000), 'Bruce changed');
  assert.deepEqual(flash.slice(0x700000, 0x800000), before.slice(0x700000, 0x800000), 'storage changed');
  assert.deepEqual(flash.slice(plan.target.offset, plan.target.offset + application.length), application);
  const table = parsePartitionTable(flash.slice(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + 0x1000));
  assert.ok(table.some(entry => entry.label === 'CardGPT' && entry.offset === plan.target.offset));
});

test('existing CardGPT update leaves even the partition table byte-identical', async () => {
  const { device, flash, events } = fakeDevice({ entries: launcherEntries(true) });
  const beforeTable = flash.slice(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + 0x1000);
  const beforeBruce = flash.slice(0x300000, 0x500000);
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'launcher');
  if (plan.mode !== 'launcher') return;
  assert.equal(plan.action, 'update');
  await installIntoLauncher(device, plan, application, () => {}, () => {});
  assert.deepEqual(flash.slice(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + 0x1000), beforeTable);
  assert.deepEqual(flash.slice(0x300000, 0x500000), beforeBruce);
  assert.ok(!events.includes('write-table'));
  assert.ok(!events.includes('erase'));
});

test('without M5Launcher discovery offers standalone but performs no erase', async () => {
  const entries = launcherEntries();
  entries[2] = { ...entries[2], label: 'factory', subtype: 0 };
  const { device, events } = fakeDevice({ entries });
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'standalone');
  assert.ok(!events.includes('erase'));
  assert.ok(!events.some(event => event.startsWith('write')));
});

test('standalone installation erases only after its separate explicit call', async () => {
  const { device, flash, events } = fakeDevice({ entries: null });
  await installFactoryImage(device, factory, () => {}, () => {});
  assert.ok(events.indexOf('erase') < events.indexOf('write-factory'));
  assert.deepEqual(flash, factory);
});

test('no free launcher space aborts without writes or erase', async () => {
  const entries = launcherEntries().slice(0, 4);
  entries.push({ label: 'Nemo', type: 0, subtype: 0x10, offset: 0x170000, size: 0x690000, flags: 0 });
  const { device, events } = fakeDevice({ entries });
  await assert.rejects(discoverInstallPlan(device), /No hay espacio libre/);
  assert.ok(!events.includes('erase'));
  assert.ok(!events.some(event => event.startsWith('write')));
});

test('application verification failure prevents partition-table registration', async () => {
  const { device, events } = fakeDevice({ fail: 'verify-app' });
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'launcher');
  if (plan.mode !== 'launcher') return;
  await assert.rejects(installIntoLauncher(device, plan, application, () => {}, () => {}), /verificación/);
  assert.ok(!events.includes('write-table'));
});

test('a changed table between review and confirmation aborts before write', async () => {
  const { device, flash, events } = fakeDevice();
  const plan = await discoverInstallPlan(device);
  assert.equal(plan.mode, 'launcher');
  if (plan.mode !== 'launcher') return;
  flash[PARTITION_TABLE_OFFSET + 12] ^= 1;
  await assert.rejects(installIntoLauncher(device, plan, application, () => {}, () => {}), /ha cambiado/);
  assert.ok(!events.some(event => event.startsWith('write')));
});

for (const [label, options] of Object.entries({
  'other chip': { chip: 'ESP32-S2' },
  'wrong flash': { flashSize: '4MB' },
  'missing stub': { stub: false },
  'secure boot': { secure: 1 << 20 },
  encryption: { crypt: 1 << 18 },
  'unreadable security': { fail: 'security' as const },
})) {
  test(`rejects ${label} without write or erase`, async () => {
    const { device, events } = fakeDevice(options);
    await assert.rejects(assertCompatibleDevice(device));
    assert.ok(!events.includes('erase'));
    assert.ok(!events.some(event => event.startsWith('write')));
  });
}
