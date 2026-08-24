import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { shouldAdmitMiner } from '../../gnfp/src/gnfp_share_guard.js';
import { shouldAdmitMiner as nodeAdmit } from '../../gnfp-node/src/miner_admit.js';
import { gnfpWorkHash } from '../../gnfp-mine/src/hash_share.js';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const src = fs.readFileSync(path.join(root, 'src/gnfp_cminer.c'), 'utf8');
assert.match(src, /#define CLIENT "GNFPHash"/);
assert.match(src, /#define VERSION "1\.1\.2"/);
assert.doesNotMatch(src, /#define VERSION "1\.1\.1"/);
assert.doesNotMatch(src, /#define MAX_THREADS\s+\d+/);
assert.doesNotMatch(src, /\bMAX_THREADS\b/);
assert.match(src, /honor_threads\s*\(/);
assert.match(src, /no hardcoded farm-size lid/);
assert.doesNotMatch(src, /honor_threads[\s\S]{0,400}256/);
assert.match(src, /#define FEE_PCT 5/);
assert.match(src, /#define FEE_EVERY 20/);
assert.match(src, /gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1/);
assert.match(src, /threads=1/);

const pool = shouldAdmitMiner({ client: 'GNFPHash', version: '1.1.2' });
assert.equal(pool.ok, true, JSON.stringify(pool));
assert.equal(nodeAdmit({ client: 'GNFPHash', version: '1.1.2' }).ok, true);
assert.equal(shouldAdmitMiner({ client: 'GNFPHash', version: '1.1.1' }).ok, true);
assert.equal(shouldAdmitMiner({ client: 'GNFPHash', version: '1.0.3' }).ok, false);
assert.equal(nodeAdmit({ client: 'GNFPHash', version: '1.0.3' }).ok, false);
assert.equal(shouldAdmitMiner({ client: 'gnfp-mine', version: '1.1.2' }).ok, false);

const want = '986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb';
assert.equal(gnfpWorkHash('test-prework', '0000000000000001', ''), want);
console.log('admit ok client=GNFPHash version=1.1.2', pool);
console.log('official gnfpWorkHash selftest', want);
