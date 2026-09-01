# gnfp-cminer

Official **$GNFP** CPU miner. Pin **0.5**.

Same hasher as 0.4 (`de719c5`). The only change is the **5% fee clock**: per-process random offset and lazy fee connect so farms do not all hit the friend login at once. Do **not** recut **0.4**. Do **not** recut deprecated **1.1.2** / **1.1.6**.

The Node **GNFPHash** / `gnfp-mine` tree is **deprecated** as the miner everyone should pull. Use this C miner instead: https://github.com/rgsneddon/gnfp-cminer

- Coin: GNFP
- Algo: **GNFPHash** (same 8-round `GNFPHash-v1` work hash)
- Wire: `client=GNFPHash` `version=0.5` (live admit floor is **1.0.4+**; pin **0.4** / **0.5** also admit)
- TLS by default to `de.restoreprivacy.online:1474` (`--notls` for local plaintext)
- Declared **5%** dual-login fee (see below)
- **No `--threads` 256 clamp.** `--threads N` is this machine’s logical CPUs only (no hardcoded farm-size lid).
- Repo: https://github.com/rgsneddon/gnfp-cminer
- Releases: https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.5
- 8-way hash batch (`avx2-x8` on x86_64, `scalar-x8` elsewhere), `--bench`, in-flight share window (from the Windows v0.4 optimized miner)
- Fee socket opens on the **first fee share**, not at process start. Offset is `0..19` from process entropy so boxes do not lock-step.

Rebuild of [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer) (that GitHub tree is a stripped Linux ELF, not source).

```
./gnfp-cminer --user gnfp1YOURADDRESS.worker --threads 8
```

`--selftest` must print `selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb`.

## Downloads (0.5)

| OS | Pack | Binary in the pack |
| --- | --- | --- |
| Linux VPS / server | [gnfp-cminer-0.5-linux.tar.gz](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-linux.tar.gz) | `gnfp-cminer` (x86_64 ELF) |
| Windows | [gnfp-cminer-0.5-windows.zip](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-windows.zip) | `gnfp-cminer.exe` + `example.bat` |
| macOS | [gnfp-cminer-0.5-macos.dmg](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-macos.dmg) or [gnfp-cminer-0.5-macos.tar.gz](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-macos.tar.gz) | `gnfp-cminer` (arm64, notarized) |

Need a login: a `gnfp1…` payout address from [gnfp-wallet](https://github.com/rgsneddon/gnfp-wallet). Pool: [gnfp.restoreprivacy.online](https://gnfp.restoreprivacy.online). Stratum: `de.restoreprivacy.online:1474` (TLS). Use one unique `.worker` name per machine.

---

## Linux VPS

Use the **linux** pack (`gnfp-cminer-0.5-linux.tar.gz`). The macOS and Windows packs will not run on a Linux VPS. The ELF is **x86_64** and needs **glibc** + **OpenSSL 3** (`libssl.so.3`). Alpine musl will not run this binary — build from source there.

A fresh VPS often has none of the download tools. `curl: command not found` (or the same for `wget` / `tar`) means install the packages in step 2 **before** you try to fetch the pack.

| Package | Why |
| --- | --- |
| `curl` or `wget` | Download the pack from GitHub. Either one is enough; installing both is fine. |
| `tar` + `gzip` | Unpack `gnfp-cminer-0.5-linux.tar.gz`. |
| `ca-certificates` | GitHub HTTPS. Without this, curl/wget can fail with an SSL error. |
| `openssl` / `libssl3` | The ELF links `libssl.so.3` and `libcrypto.so.3` (TLS to the pool). |

Also: outbound **TCP 1474** to `de.restoreprivacy.online` (TLS is the default).

### 1. SSH in

Replace `user` and `vps.example.com` with your host:

```bash
ssh user@vps.example.com
```

### 2. Install packages

Debian / Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y curl wget tar gzip ca-certificates openssl
```

Fedora / RHEL / Alma / Rocky:

```bash
sudo dnf install -y curl wget tar gzip ca-certificates openssl
```

### 3. Get the pack onto the VPS

**Download on the VPS** (simplest). `curl` or `wget`:

```bash
curl -L -o gnfp-cminer.tgz \
  https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-linux.tar.gz
```

```bash
wget -O gnfp-cminer.tgz \
  https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-linux.tar.gz
```

**Or copy from your laptop** after you downloaded the linux pack at home.

From a Mac or Linux machine:

```bash
scp gnfp-cminer-0.5-linux.tar.gz user@vps.example.com:~/gnfp-cminer.tgz
ssh user@vps.example.com
```

From Windows (PowerShell):

```powershell
scp gnfp-cminer-0.5-linux.tar.gz user@vps.example.com:~/gnfp-cminer.tgz
ssh user@vps.example.com
```

### 4. Unpack and check

On the VPS, in the directory that has `gnfp-cminer.tgz`:

```bash
tar -xzf gnfp-cminer.tgz && cd gnfp-cminer-0.5
chmod +x gnfp-cminer
./gnfp-cminer --selftest
```

Must print:

```
selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
```

If it says `libssl.so.3: cannot open shared object file`, go back to step 2 and install `openssl`.

### 5. Set gnfp1 + worker, then run

`--user` is one string: your `gnfp1…` payout address, a dot, then a worker name for this machine.

| Part | What to put |
| --- | --- |
| Login | Your `gnfp1…` address. A real payout address is required. |
| Worker | A unique name per box, e.g. `vps1`. Letters, digits, `_`, `-` only; 1–24 characters. Do not reuse the same `.worker` on two machines. |
| Threads | This box’s logical CPUs. `$(nproc)` uses all of them. There is no `--threads` 256 clamp. |

Create `example.sh` next to the binary (or copy it from this repo):

```bash
nano example.sh
```

```sh
#!/bin/sh
cd "$(dirname "$0")"
exec ./gnfp-cminer --user gnfp1YOURADDRESS.vps1 --threads $(nproc)
```

Replace `gnfp1YOURADDRESS` with your real address. Save in nano: `Ctrl+O`, Enter, then `Ctrl+X`.

```bash
chmod +x example.sh
./example.sh
```

Or skip the script:

```bash
./gnfp-cminer --user gnfp1YOURADDRESS.vps1 --threads $(nproc)
```

TLS to `de.restoreprivacy.online:1474` is already the default. Do not pass `--notls` unless you are pointing at a local plaintext node.

### 6. Stay up (systemd)

As root. Paths are examples. Put the **same** `--user gnfp1….worker` you set above:

```ini
# /etc/systemd/system/gnfp-cminer.service
[Unit]
Description=gnfp-cminer (GNFPHash-v1)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/gnfp-cminer
ExecStart=/opt/gnfp-cminer/gnfp-cminer --user gnfp1YOURADDRESS.vps1 --threads 8
Restart=always
RestartSec=5
Nice=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo mkdir -p /opt/gnfp-cminer
sudo cp gnfp-cminer /opt/gnfp-cminer/ && sudo chmod +x /opt/gnfp-cminer/gnfp-cminer
sudo systemctl daemon-reload
sudo systemctl enable --now gnfp-cminer
sudo journalctl -u gnfp-cminer -f
```

Open outbound **TCP 1474** (TLS) to `de.restoreprivacy.online`.

---

## Windows (PC or Windows Server)

1. Download [gnfp-cminer-0.5-windows.zip](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-windows.zip).
2. Unzip so `gnfp-cminer.exe` and `example.bat` sit in the same folder. This PE is static (no extra OpenSSL DLLs).
3. If SmartScreen or Defender warns: **More info → Run anyway**, or file **Properties → Unblock**.
4. Edit `example.bat`: replace `gnfp1YOURADDRESS` with your payout address, change `.worker` to a unique name for this box, and set `--threads` to this box’s logical CPUs (`echo %NUMBER_OF_PROCESSORS%`).
5. Double-click `example.bat`, or from `cmd`:

```bat
cd /d C:\path\to\gnfp-cminer
gnfp-cminer.exe --selftest
gnfp-cminer.exe --user gnfp1YOURADDRESS.win1 --threads 8
```

Leave that window open. For a server that should survive logoff, use Task Scheduler: **Create Task → Run whether user is logged on or not → Action** start `gnfp-cminer.exe` with the same arguments, start in the unzip folder. Allow outbound **TCP 1474**.

---

## macOS

This is a command-line miner, not a `.app`. Finder will not “open” it like an application. Use **Terminal**.

**DMG** (Developer ID, notarized, stapled; static OpenSSL — no Homebrew dylib):

1. Download [gnfp-cminer-0.5-macos.dmg](https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-macos.dmg).
2. Open the image and copy `gnfp-cminer` off the disk (for example into `~/gnfp-cminer/`).
3. Eject the disk image.

**Or tar.gz:**

```bash
curl -L -o gnfp-cminer.tgz \
  https://github.com/rgsneddon/gnfp-cminer/releases/download/v0.5/gnfp-cminer-0.5-macos.tar.gz
tar -xzf gnfp-cminer.tgz && cd gnfp-cminer-0.5
```

Then:

```bash
chmod +x gnfp-cminer
xattr -d com.apple.quarantine gnfp-cminer 2>/dev/null || true
./gnfp-cminer --selftest
```

If Gatekeeper blocks it: **System Settings → Privacy & Security → Open Anyway**, or right-click → **Open**.

Edit `example.sh` the same way as Linux VPS step 5 (or run the binary directly). Replace `gnfp1YOURADDRESS` with your payout address, and `worker` with a unique name for this Mac (e.g. `mac1`):

```bash
./gnfp-cminer --user gnfp1YOURADDRESS.mac1 --threads $(sysctl -n hw.logicalcpu)
```

The public macOS pin is **Apple silicon (arm64)**. Haswell x86_64 leftover stays under `leftover/macos-x86_64-haswell-bigsur/` (not a second pin).

---

## Flags (`--help`)

```
GNFPHash C miner 0.5 (declared 5% fee, dual connection)
Credit: rebuild of https://github.com/rvp-design/gnfp_cminer
Not the official GNFPHash pin (rgsneddon/GNFPHash).

  --user gnfp1ADDR.worker   required
  --pool host:port          default de.restoreprivacy.online:1474
  --threads N               default physical cores minus 1; no 256 farm cap
  --notls                   plaintext (local node only)
  --bench [SECONDS]         local hashrate, no pool (default 3s)
  --selftest
  --help
```

| Flag | What it does |
| --- | --- |
| `--help` / `-h` | Print the list above and exit. |
| `--user` | Login. `gnfp1…` payout address, then `.worker`. Required to mine. |
| `--pool` | Stratum `host:port`. Default `de.restoreprivacy.online:1474`. |
| `--threads` | Worker threads. Use this machine’s logical CPUs. No 256 farm cap. Default = physical cores minus 1. |
| `--notls` | Plain TCP. Only for a local node. The public book needs TLS — omit this there. |
| `--bench [SECONDS]` | Hashrate bench, then exit. Optional duration (default 3s). |
| `--selftest` | Check the GNFPHash-v1 vector; must print `986437c4…`. |

---

## Honest architecture

`rvp-design/gnfp_cminer` shipped a closed ELF that logged in as official `GNFPHash 1.0.5`. This rebuild:

- keeps the friend’s published **5%** fee address
- is **open source**
- reports the farm it actually runs (`threads` = hash workers on the main login; fee login `threads=1`)

Deprecated Node miner: [rgsneddon/GNFPHash](https://github.com/rgsneddon/GNFPHash) (still earns at 1.0.4+; do not prefer it).

## Dev fee (5%)

This is a **miner** fee, not a pool tax. The live book still takes **1%** of each formed block for the operator. gnfp-cminer submits every 20th meeting nonce on a **second connection**, phased by a per-process offset so a farm does not burst onto the fee login together:

```
gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1.fee
```

That is the address published in the original ELF. The pool credits that login only when this software actually submits there. Other miners (including the deprecated Node GNFPHash tree) pay **0%** to that address. The fee socket reports **threads=1** so two logins from one box do not claim two full farms. Hash threads always mine the **main** job. If the fee socket is down, **all** shares stay on your login.

## Build from source

Needs a C compiler, pthreads, and OpenSSL (TLS). On macOS:

```
brew install openssl@3
make
./gnfp-cminer --selftest
```

On Debian/Ubuntu:

```
sudo apt-get install -y build-essential libssl-dev
make
./gnfp-cminer --selftest
```

`--selftest` must print:

```
selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
```

That vector is official `gnfpWorkHash("test-prework", "0000000000000001", "")`.

Local node (plaintext):

```
./gnfp-cminer --pool 127.0.0.1:1474 --notls --user gnfp1YOURADDRESS.worker --threads 4
```

`--threads` is real POSIX threads. There is **no `--threads` 256 clamp** and no other hardcoded farm-size lid. Cap = this machine’s logical CPUs. Default = physical cores minus 1. A 240-thread box may pass `--threads 240`.

A real `gnfp1` payout address is required. Worker tag is 1–24 letters/digits/`_`/`-` (default `worker`). TLS to `de.restoreprivacy.online:1474` is the default.

## Packs

GNFP client pack names, one tag `v0.5` (no sibling tags):

| File | What is inside |
|------|----------------|
| `gnfp-cminer-0.5-macos.tar.gz` | Darwin **arm64** binary (static OpenSSL; same hasher as the DMG) |
| `gnfp-cminer-0.5-linux.tar.gz` | Linux **x86_64 ELF** (`libssl.so.3`; rebuild with `sudo apt-get install -y build-essential libssl-dev && make`) |
| `gnfp-cminer-0.5-windows.zip` | `gnfp-cminer.exe` at zip root + `example.bat` (static PE; fee offset + lazy connect). |

Same naming as other GNFP clients (`gnfp-cminer-VERSION-platform`). Public pin: https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.5

Do **not** recut public **0.4**. Haswell x86_64 leftover stays under `leftover/macos-x86_64-haswell-bigsur/` (Air only, not a second pin). Do **not** ship leftover `1.0.6-max-autotune`.

The Windows PE on `v0.5` is `gnfp-cminer.exe` at the zip root plus `example.bat` (static, no extra DLLs). The macOS **DMG** is the notarized run path; the tar.gz is the same arm64 binary.

## Credit

- Original closed miner and 5% fee: [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer)
- Work hash / stratum: GNFPHash-v1 (same vector as the deprecated Node miner)
