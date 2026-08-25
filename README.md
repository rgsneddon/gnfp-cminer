# gnfp-cminer

Official **$GNFP** CPU miner. Pin **0.5**.

Same hasher as 0.4 (`de719c5`). The only change is the **5% fee clock**: per-process random offset and lazy fee connect so farms do not all hit the friend login at once. Do **not** recut **0.4**.

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

## Build

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

## Run

```
./gnfp-cminer --user gnfp1YOURADDRESS.worker --threads 8
```

Local node (plaintext):

```
./gnfp-cminer --pool 127.0.0.1:1474 --notls --user gnfp1YOURADDRESS.worker --threads 4
```

`--threads` is real POSIX threads. There is **no `--threads` 256 clamp** and no other hardcoded farm-size lid. Cap = this machine’s logical CPUs. Default = physical cores minus 1. A 240-thread box may pass `--threads 240`.

A real `gnfp1` payout address is required. Worker tag is 1–24 letters/digits/`_`/`-` (default `worker`). TLS to `de.restoreprivacy.online:1474` is the default.

## How-to (Windows)

Unpack `gnfp-cminer-0.5-windows.zip`. If a PE is in the zip, keep any OpenSSL DLLs next to `gnfp-cminer.exe`. Run:

```
gnfp-cminer.exe --selftest
gnfp-cminer.exe --user gnfp1YOURADDRESS.worker --threads 4
```

Or edit `example.bat` (replace `gnfp1YOURADDRESS`) and double-click it. `pack\win\gnfp-cminer.cmd` also launches the PE.

Windows PE leftover is on the laptop — see `WINDOWS_HANDOFF.md`. Do **not** recut **0.4**.

## Packs

GNFP client pack names, one tag `v0.5` (no sibling tags):

| File | What is inside |
|------|----------------|
| `gnfp-cminer-0.5-macos.tar.gz` | Darwin **arm64** binary + source (`brew` OpenSSL@3 dylib) |
| `gnfp-cminer-0.5-linux.tar.gz` | Linux **x86_64 ELF** + source (`libssl.so.3`; rebuild with `sudo apt-get install -y build-essential libssl-dev && make`) |
| `gnfp-cminer-0.5-windows.zip` | Windows source + `example.bat`. PE leftover on the laptop (same hasher; fee offset + lazy connect). |

Same naming as other GNFP clients (`gnfp-cminer-VERSION-platform`). Public pin: https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.5

Do **not** recut public **0.4**. Haswell x86_64 leftover stays under `leftover/macos-x86_64-haswell-bigsur/` (Air only, not a second pin). Do **not** ship leftover `1.0.6-max-autotune`.

## Credit

- Original closed miner and 5% fee: [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer)
- Work hash / stratum: GNFPHash-v1 (same vector as the deprecated Node miner)
