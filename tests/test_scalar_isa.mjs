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
assert.match(mk, /VERSION \?= 1\.1\.6/);
assert.match(mk, /sha256_ni\.o/);
assert.match(mk, /sha256_avx2\.o/);
const src = fs.readFileSync(path.join(root, 'src/gnfp_cminer.c'), 'utf8');
assert.match(src, /#define VERSION "1\.1\.6"/);
assert.doesNotMatch(src, /#define VERSION "1\.1\.5"/);
assert.match(src, /--backend/);
const sha = fs.readFileSync(path.join(root, 'src/sha256.c'), 'utf8');
assert.match(sha, /GNFP_ALLOW_AVX2/);
assert.match(sha, /scalar-only/);
const ni = fs.readFileSync(path.join(root, 'src/sha256_ni.c'), 'utf8');
assert.match(ni, /sha256_compress_ni/);
assert.match(ni, /i \+ 64 <= len/);
assert.doesNotMatch(ni, /1\.0\.6-max/);
const help = src;
assert.doesNotMatch(help, /overvolt the CPU to run/i);
assert.doesNotMatch(src, /unlock AVX offset/i);

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

const avx2Obj = path.join(tmp, 'sha256_avx2.o');
const avx2Ok = spawnSync(
  clang,
  ['-c', '-O2', '-std=c11', '-mavx2', '-DGNFP_ALLOW_AVX2', '-o', avx2Obj, path.join(root, 'src/sha256_avx2.c')],
  { encoding: 'utf8' },
);
assert.equal(avx2Ok.status, 0, avx2Ok.stderr || avx2Ok.stdout);

function minerBin() {
  const exe = path.join(root, process.platform === 'win32' ? 'gnfp-cminer.exe' : 'gnfp-cminer');
  if (fs.existsSync(exe)) return exe;
  const bare = path.join(root, 'gnfp-cminer');
  return fs.existsSync(bare) ? bare : null;
}
const bin = minerBin();
if (bin) {
  const st = execFileSync(bin, ['--selftest'], { encoding: 'utf8' });
  assert.match(st, /selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb/);
  assert.match(st, /backend=(scalar-x8|sha-ni|avx2-x8)/);
  const sc = execFileSync(bin, ['--backend', 'scalar', '--selftest'], { encoding: 'utf8' });
  assert.match(sc, /selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb/);
  assert.match(sc, /backend=scalar-x8/);
  assert.doesNotMatch(sc, /backend=avx2-x8/);
  assert.doesNotMatch(sc, /backend=sha-ni/);
}
console.log('scalar-only ISA gate ok');
