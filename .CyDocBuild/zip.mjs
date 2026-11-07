// Small deterministic ZIP writer (stored entries, UTF-8 names). No runtime dependency.
import { readdir, readFile, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
const table = Array.from({ length: 256 }, (_, value) => {
  for (let i = 0; i < 8; i++) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  return value >>> 0;
});
function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) value = table[(value ^ byte) & 255] ^ (value >>> 8);
  return (value ^ 0xffffffff) >>> 0;
}
export async function zipDirectory(directory, destination) {
  const entries = [];
  async function walk(path, prefix = '') {
    for (const item of (await readdir(path, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name, 'en'))) {
      if (item.name === '.cysynck-generated') continue;
      if (item.isDirectory()) {
        entries.push({ name: Buffer.from(prefix + item.name + '/'), bytes: Buffer.alloc(0) });
        await walk(join(path, item.name), prefix + item.name + '/');
      }
      else if (item.isFile()) entries.push({ name: Buffer.from(prefix + item.name), bytes: await readFile(join(path, item.name)) });
      else throw new Error('Links and special entries cannot enter the offline manual archive');
    }
  }
  await walk(directory);
  const chunks = [], central = [];
  let offset = 0, centralSize = 0;
  for (const { name, bytes } of entries) {
    const checksum = crc32(bytes), header = Buffer.alloc(30), record = Buffer.alloc(46);
    header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(0x800, 6);
    header.writeUInt16LE(0x21, 12); header.writeUInt32LE(checksum, 14);
    header.writeUInt32LE(bytes.length, 18); header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(name.length, 26);
    record.writeUInt32LE(0x02014b50); record.writeUInt16LE(20, 4); record.writeUInt16LE(20, 6);
    record.writeUInt16LE(0x800, 8); record.writeUInt16LE(0x21, 14); record.writeUInt32LE(checksum, 16);
    record.writeUInt32LE(bytes.length, 20); record.writeUInt32LE(bytes.length, 24); record.writeUInt16LE(name.length, 28);
    record.writeUInt32LE(offset, 42);
    chunks.push(header, name, bytes); central.push(record, name);
    offset += header.length + name.length + bytes.length; centralSize += record.length + name.length;
  }
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(centralSize, 12); end.writeUInt32LE(offset, 16);
  await writeFile(destination, Buffer.concat([...chunks, ...central, end]));
}
