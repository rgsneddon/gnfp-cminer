import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const md = fs.readFileSync(path.join(root, 'README.md'), 'utf8');
assert.match(md, /rvp-design\/gnfp_cminer/);
assert.match(md, /5%/);
assert.match(md, /dual-login|second connection/);
assert.match(md, /gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1/);
assert.match(md, /stripped Linux ELF|closed ELF/);
assert.match(md, /GNFPHash-v1/);
assert.match(md, /TLS/);
assert.match(md, /not\*\* the official miner|not the official miner/i);
assert.match(md, /1\.1\.0/);
assert.match(md, /private/);
console.log('README credit/fee/hash notes ok');
