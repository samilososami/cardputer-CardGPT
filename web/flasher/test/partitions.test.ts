import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { findCardGPTPartition } from '../src/partitions.ts';
type Entry = [string, number, number, number, number, number?];
const valid: Entry[] = [
  ['nvs', 1, 2, 0x9000, 0x5000], ['otadata', 1, 0, 0xe000, 0x2000],
  ['app0', 0, 0x20, 0x10000, 0x150000], ['CardGPT', 0, 0x10, 0x1e0000, 0x200000],
];
function table(entries = valid): Uint8Array {
  const b = Buffer.alloc(0xc00, 0xff);
  entries.forEach(([label,type,subtype,offset,size,flags=0],i) => {
    const at = i * 32;
    b.fill(0, at, at + 32); b.writeUInt16LE(0x50aa, at); b[at+2]=type; b[at+3]=subtype;
    b.writeUInt32LE(offset,at+4); b.writeUInt32LE(size,at+8); b.write(label,at+12,16); b.writeUInt32LE(flags,at+28);
  });
  const end=entries.length*32;
  b.writeUInt16LE(0xebeb,end);createHash('md5').update(b.subarray(0,end)).digest().copy(b,end+16);
  return b;
}
test('accepts approved layout and returns actual CardGPT address',()=>{assert.equal(findCardGPTPartition(table()).offset,0x1e0000)});
test('uses discovered destination instead of a fixed offset',()=>{const p=valid.map(e=>[...e] as Entry);p[3][3]=0x400000;assert.equal(findCardGPTPartition(table(p)).offset,0x400000)});
test('rejects missing CardGPT',()=>assert.throws(()=>findCardGPTPartition(table(valid.slice(0,3))),/primera instalación/));
test('rejects overlapping launcher',()=>{const p=valid.map(e=>[...e] as Entry);p[3][3]=0x10000;assert.throws(()=>findCardGPTPartition(table(p)),/solapadas/)});
test('rejects truncated or corrupt table',()=>{assert.throws(()=>findCardGPTPartition(new Uint8Array(8)),/incompleta/);const b=table();b[13]^=1;assert.throws(()=>findCardGPTPartition(b),/checksum/)});
test('rejects too small target',()=>{const p=valid.map(e=>[...e] as Entry);p[3][4]=0x100000;assert.throws(()=>findCardGPTPartition(table(p)),/espacio/)});
test('rejects encrypted target',()=>{const p=valid.map(e=>[...e] as Entry);p[3][5]=1;assert.throws(()=>findCardGPTPartition(table(p)),/cifrada/)});
test('rejects target outside flash',()=>{const p=valid.map(e=>[...e] as Entry);p[3][3]=0x700000;assert.throws(()=>findCardGPTPartition(table(p)),/límites/)});
test('rejects data partitions named CardGPT',()=>{const p=valid.map(e=>[...e] as Entry);p[3][1]=1;assert.throws(()=>findCardGPTPartition(table(p)),/OTA/)});
test('rejects missing NVS',()=>assert.throws(()=>findCardGPTPartition(table(valid.slice(1))),/NVS/));
test('rejects duplicate labels',()=>assert.throws(()=>findCardGPTPartition(table([...valid,['CardGPT',0,0x11,0x400000,0x200000]])),/única/));
test('rejects table without MD5 and unknown magic',()=>{const b=table();b.fill(0xff,valid.length*32);assert.throws(()=>findCardGPTPartition(b),/no reconocida/);assert.throws(()=>findCardGPTPartition(new Uint8Array(0xc00)),/no reconocida/)});
