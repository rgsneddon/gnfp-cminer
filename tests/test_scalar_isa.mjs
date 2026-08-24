import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const mk = fs.readFileSync(path.join(root, 'Makefile'), 'utf8');
assert.doesNotMatch(mk, /CFLAGS \+= -mavx2/);
assert.doesNotMatch(mk, /CFLAGS \+= -msha/);
assert.doesNotMatch(mk, /^\s*CFLAGS \+= /m);
assert.match(mk, /VERSION \?= 1\.1\.5/);
const src = fs.readFileSync(path.join(root, 'src/gnfp_cminer.c'), 'utf8');
assert.match(src, /#define VERSION "1\.1\.5"/);
assert.doesNotMatch(src, /#define VERSION "1\.1\.4"/);
const sha = fs.readFileSync(path.join(root, 'src/sha256.c'), 'utf8');
assert.match(sha, /GNFP_ALLOW_AVX2/);
assert.match(sha, /scalar-only/);

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gnfp-scalar-'));
const obj = path.join(tmp, 'sha256.o');
const clang = process.env.CC || 'cc';
const archArgs = [];
if (process.platform === 'darwin') archArgs.push('-arch', 'x86_64');
const compile = spawnSync(
  clang,
  ['-c', '-O2', '-std=c11', ...archArgs, '-o', obj, path.join(root, 'src/sha256.c')],
  { encoding: 'utf8' },
);
assert.equal(compile.status, 0, compile.stderr || compile.stdout);
assert.ok(fs.existsSync(obj));
const dumped = spawnSync('sh', ['-c', `objdump -d ${JSON.stringify(obj)} 2>/dev/null || llvm-objdump -d ${JSON.stringify(obj)} 2>/dev/null || gobjdump -d ${JSON.stringify(obj)}`], {
  encoding: 'utf8',
  maxBuffer: 8 * 1024 * 1024,
});
const dis = `${dumped.stdout || ''}\n${dumped.stderr || ''}`;
assert.doesNotMatch(dis, /\bymm[0-9]+\b/);
assert.doesNotMatch(dis, /vinserti128/);
const avx2Try = spawnSync(
  clang,
  ['-c', '-O2', '-std=c11', '-mavx2', '-o', path.join(tmp, 'sha256-avx2.o'), path.join(root, 'src/sha256.c')],
  { encoding: 'utf8' },
);
assert.notEqual(avx2Try.status, 0, ' -mavx2 must fail the scalar-only #error');
assert.match(`${avx2Try.stderr || ''}\n${avx2Try.stdout || ''}`, /scalar-only|AVX2|mavx2/i);

const bin = path.join(root, 'gnfp-cminer');
if (fs.existsSync(bin) && fs.constants && true) {
  const st = execFileSync(bin, ['--selftest'], { encoding: 'utf8' });
  assert.match(st, /selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb/);
  assert.match(st, /backend=scalar-x8/);
  assert.doesNotMatch(st, /backend=avx2-x8/);
}
console.log('scalar-only ISA gate ok');
