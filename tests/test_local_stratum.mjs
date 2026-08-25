import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const bin = path.join(root, 'gnfp-cminer');
const USER = 'gnfp18ff7e8b2f0ef3e96f598231638aafd5a5abc490c.testc';
const FEE = 'gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1.fee';

const logins = [];
const submits = { main: 0, fee: 0 };
const feeJobIds = [];
const MAIN_JOB = 'main-t1';
const FEE_JOB = 'fee-t2';

function jobLine(jobId) {
  return `${JSON.stringify({
    jsonrpc: '2.0',
    method: 'job',
    id: jobId,
    jobId,
    height: 1,
    difficulty: 14,
    input: 'test-prework',
    coin: 'GNFP',
    algorithm: 'GNFPHash',
  })}\n`;
}

const server = net.createServer((sock) => {
  let buf = '';
  sock.setEncoding('utf8');
  sock.on('data', (chunk) => {
    buf += chunk;
    let idx;
    while ((idx = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, idx);
      buf = buf.slice(idx + 1);
      if (!line.trim()) continue;
      let msg;
      try {
        msg = JSON.parse(line);
      } catch {
        continue;
      }
      if (msg.method === 'login') {
        const login = String(msg.login || '');
        logins.push(login);
        sock.write(`${JSON.stringify({
          code: 0,
          description: 'Login Successful',
          id: 1,
          jsonrpc: '2.0',
          method: 'result',
          asset: 'GNFP',
        })}\n`);
        const fee = login.startsWith(FEE.split('.')[0]);
        sock.write(jobLine(fee ? FEE_JOB : MAIN_JOB));
      }
      if (msg.method === 'submit') {
        const login = String(msg.login || '');
        if (login.startsWith(FEE.split('.')[0])) {
          submits.fee += 1;
          feeJobIds.push(String(msg.jobId || msg.id || ''));
        } else submits.main += 1;
        sock.write(`${JSON.stringify({
          code: 1,
          description: 'accepted',
          id: msg.jobId || msg.id,
          jsonrpc: '2.0',
          method: 'result',
        })}\n`);
      }
    }
  });
});

await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const { port } = server.address();
const child = spawn(bin, [
  '--notls',
  '--pool', `127.0.0.1:${port}`,
  '--user', USER,
  '--threads', '2',
], { cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });

let out = '';
child.stdout.on('data', (d) => { out += d; });
child.stderr.on('data', (d) => { out += d; });

const deadline = Date.now() + 25000;
while (Date.now() < deadline) {
  if (logins.includes(FEE) && submits.main >= 1 && submits.fee >= 1) break;
  await new Promise((r) => setTimeout(r, 200));
}

child.kill('SIGTERM');
await new Promise((r) => child.once('close', r));
server.close();

assert.ok(logins.some((l) => l.startsWith('gnfp18ff7e8b2f0ef3e96f598231638aafd5a5abc490c')), logins);
assert.ok(logins.includes(FEE), `fee login missing: ${JSON.stringify(logins)}`);
assert.ok(logins[0] && !logins[0].startsWith(FEE.split('.')[0]), `fee must not login first (lazy connect): ${JSON.stringify(logins)}`);
assert.ok(logins.indexOf(FEE) > 0, `fee login must come after main: ${JSON.stringify(logins)}`);
assert.ok(submits.main >= 1, `no main shares: ${JSON.stringify(submits)} out=${out.slice(-800)}`);
assert.ok(submits.fee >= 1, `no fee shares (5% routing): ${JSON.stringify(submits)} out=${out.slice(-800)}`);
assert.ok(feeJobIds.every((id) => id === MAIN_JOB), `fee submits must use main jobId, not fee-session job: ${JSON.stringify(feeJobIds)}`);
assert.match(out, /declared 5% fee/);
assert.match(out, /fee login/);
assert.match(out, /offset=\d+\/20/);
console.log('local stratum ok', { logins, submits });
