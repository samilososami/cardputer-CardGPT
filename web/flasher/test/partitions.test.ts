import test from 'node:test';
import assert from 'node:assert/strict';
import {
  APP_ALIGNMENT,
  FLASH_SIZE,
  buildPartitionTable,
  parsePartitionTable,
  planLauncherInstall,
  type Partition,
} from '../src/partitions.ts';

const appSize = 1_588_480;
const baseEntries = (): Partition[] => [
  { label: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000, flags: 0 },
  { label: 'otadata', type: 1, subtype: 0, offset: 0xe000, size: 0x2000, flags: 0 },
  { label: 'app0', type: 0, subtype: 0x20, offset: 0x10000, size: 0x150000, flags: 0 },
  { label: 'coredump', type: 1, subtype: 3, offset: 0x160000, size: 0x10000, flags: 0 },
  { label: 'Bruce', type: 0, subtype: 0x10, offset: 0x300000, size: 0x200000, flags: 0 },
  { label: 'brucefs', type: 1, subtype: 0x82, offset: 0x500000, size: 0x100000, flags: 0 },
];

const deployedCardGptEntries = (): Partition[] => [
  { label: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000, flags: 0 },
  { label: 'otadata', type: 1, subtype: 0, offset: 0xe000, size: 0x2000, flags: 0 },
  { label: 'app0', type: 0, subtype: 0x20, offset: 0x10000, size: 0x150000, flags: 0 },
  { label: 'coredump', type: 1, subtype: 3, offset: 0x160000, size: 0x10000, flags: 0 },
  { label: 'storage', type: 1, subtype: 0x81, offset: 0x170000, size: 0x70000, flags: 0 },
  { label: 'CardGPT', type: 0, subtype: 0x10, offset: 0x1e0000, size: 0x200000, flags: 0 },
  { label: 'spiffs', type: 1, subtype: 0x82, offset: 0x790000, size: 0x70000, flags: 0 },
];

test('partition table serializer is checksum-valid and lossless', () => {
  const entries = baseEntries();
  assert.deepEqual(parsePartitionTable(buildPartitionTable(entries)), entries);
});

test('adds CardGPT to the first genuinely free aligned range', () => {
  const source = buildPartitionTable(baseEntries());
  const plan = planLauncherInstall(source, appSize);
  assert.ok(plan);
  assert.equal(plan.action, 'install');
  assert.equal(plan.target.label, 'CardGPT');
  assert.equal(plan.target.offset, 0x170000);
  assert.equal(plan.target.size, Math.ceil(appSize / APP_ALIGNMENT) * APP_ALIGNMENT);
  assert.equal(plan.target.subtype, 0x11);
  assert.equal(plan.tableChanged, true);
  const after = parsePartitionTable(plan.targetTable);
  assert.ok(after.some(partition => partition.label === 'Bruce' && partition.offset === 0x300000));
  assert.ok(after.some(partition => partition.label === 'brucefs' && partition.offset === 0x500000));
});

test('updates the deployed CardGPT layout at 0x1e0000 without touching its table', () => {
  const source = buildPartitionTable(deployedCardGptEntries());
  const plan = planLauncherInstall(source, appSize);
  assert.ok(plan);
  assert.equal(plan.action, 'update');
  assert.equal(plan.target.offset, 0x1e0000);
  assert.equal(plan.target.size, 0x200000);
  assert.equal(plan.tableChanged, false);
  assert.deepEqual(plan.targetTable, source);
});

for (const label of ['CardGPT', 'cardgp']) {
  test(`updates an existing ${label} partition without rewriting the table`, () => {
    const entries = [...baseEntries(), {
      label, type: 0, subtype: 0x11, offset: 0x600000, size: 0x200000, flags: 0,
    }];
    const source = buildPartitionTable(entries);
    const plan = planLauncherInstall(source, appSize);
    assert.ok(plan);
    assert.equal(plan.action, 'update');
    assert.equal(plan.target.offset, 0x600000);
    assert.equal(plan.tableChanged, false);
    assert.deepEqual(plan.targetTable, source);
  });
}

test('does not mistake a normal factory layout for M5Launcher', () => {
  const entries = baseEntries();
  entries[2] = { ...entries[2], label: 'factory', subtype: 0 };
  assert.equal(planLauncherInstall(buildPartitionTable(entries), appSize), null);
});

test('refuses to overwrite another firmware when there is no free space', () => {
  const entries = baseEntries().slice(0, 4);
  entries.push({ label: 'Nemo', type: 0, subtype: 0x10, offset: 0x170000, size: 0x690000, flags: 0 });
  assert.throws(
    () => planLauncherInstall(buildPartitionTable(entries), appSize),
    /No hay espacio libre.*sin borrar otro firmware/,
  );
});

test('rejects a too-small existing CardGPT slot instead of moving or replacing apps', () => {
  const entries = [...baseEntries(), {
    label: 'CardGPT', type: 0, subtype: 0x11, offset: 0x600000, size: 0x100000, flags: 0,
  }];
  assert.throws(() => planLauncherInstall(buildPartitionTable(entries), appSize), /demasiado pequeña/);
});

test('rejects incompatible or ambiguous launcher layouts', () => {
  const missingNvs = baseEntries().filter(entry => entry.label !== 'nvs');
  assert.throws(() => planLauncherInstall(buildPartitionTable(missingNvs), appSize), /NVS/);
  const duplicated = [...baseEntries(), {
    label: 'CardGPT', type: 0, subtype: 0x11, offset: 0x600000, size: 0x190000, flags: 0,
  }, {
    label: 'cardgp', type: 0, subtype: 0x12, offset: 0x790000, size: 0x70000, flags: 0,
  }];
  assert.throws(() => planLauncherInstall(buildPartitionTable(duplicated), appSize), /varias particiones/);
});

test('rejects corruption, overlap and addresses beyond 8 MB', () => {
  const corrupt = buildPartitionTable(baseEntries());
  corrupt[15] ^= 1;
  assert.throws(() => parsePartitionTable(corrupt), /checksum/);
  const overlap = baseEntries();
  overlap[4] = { ...overlap[4], offset: 0x10000 };
  assert.throws(() => parsePartitionTable(buildPartitionTable(overlap)), /solapadas/);
  const outside = baseEntries();
  outside[4] = { ...outside[4], offset: FLASH_SIZE - 0x10000 };
  assert.throws(() => parsePartitionTable(buildPartitionTable(outside)), /límites/);
});
