# gnfp-cminer

Official **$GNFP** CPU miner. Pin **1.1.4** (scalar-only).

The Node **GNFPHash** / `gnfp-mine` tree is **deprecated** as the miner everyone should pull. Use this C miner instead: https://github.com/rgsneddon/gnfp-cminer

- Coin: GNFP
- Algo: **GNFPHash** (same 8-round `GNFPHash-v1` work hash)
- Wire: `client=GNFPHash` `version=1.1.4` (live admit floor is **1.0.4+**)
- TLS by default to `de.restoreprivacy.online:1474` (`--notls` for local plaintext)
- Declared **5%** dual-login fee (see below)
- **No `--threads` 256 clamp.** `--threads N` is this machine’s logical CPUs only (no hardcoded farm-size lid).
- Repo: https://github.com/rgsneddon/gnfp-cminer
- Releases: https://github.com/rgsneddon/gnfp-cminer/releases/tag/v1.1.4
- **Scalar-only.** Default `make` does **not** pass `-mavx2` / `-msha`. Hash backend is `scalar-x8`. Do not enable AVX/AVX2/SHA in BIOS or overvolt the CPU to run this miner. Public **1.1.2** Linux/Windows ELFs still contain AVX2 ymm — use **1.1.4**. Do **not** run leftover `1.0.6-max-autotune` uploads (SHA-NI single-block + fee login does not credit `FEE_ADDR`).
- 8-way scalar batch, `--bench`, in-flight share window

Rebuild of [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer) (that GitHub tree is a stripped Linux ELF, not source).

## Honest architecture

`rvp-design/gnfp_cminer` shipped a closed ELF that logged in as official `GNFPHash 1.0.5`. This rebuild:

- keeps the friend’s published **5%** fee address
- is **open source**
- reports the farm it actually runs (`threads` = hash workers on the main login; fee login `threads=1`)

Deprecated Node miner: [rgsneddon/GNFPHash](https://github.com/rgsneddon/GNFPHash) (still earns at 1.0.4+; do not prefer it).

## Dev fee (5%)

This is a **miner** fee, not a pool tax. The live book still takes **1%** of each formed block for the operator. gnfp-cminer submits every 20th meeting nonce on a **second connection**. The fee **address** is the published gnfp1; the fee **worker** names who paid:

```
gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1.fTAIL_worker
```

`TAIL` is the last 6 characters of the miner gnfp1. `worker` is the first 8 of `--user`'s worker. Example: `--user gnfp18ff7e8b2f0ef3e96f598231638aafd5a5abc490c.testc` logs the fee socket as `…c6ff1.fbc490c_testc`. Paying farms show up as distinct workers on that address; miners still on `.fee` (or with no fee login at all) are not this pin. The pool credits that login only when this software actually submits `"login"` there (not `"user"`). Other miners (including the deprecated Node GNFPHash tree) pay **0%** to that address. The fee socket reports **threads=1** so two logins from one box do not claim two full farms. If the fee socket is down, **all** shares stay on your login.

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

Unpack `gnfp-cminer-1.1.4-windows.zip`. Keep the OpenSSL DLLs next to `gnfp-cminer.exe`. Run:

```
gnfp-cminer.exe --selftest
gnfp-cminer.exe --user gnfp1YOURADDRESS.worker --threads 4
```

Or edit `example.bat` (replace `gnfp1YOURADDRESS`) and double-click it. `pack\win\gnfp-cminer.cmd` also launches the PE.

## Packs

GNFP client pack names, one tag `v1.1.4` (no sibling tags):

| File | What is inside |
|------|----------------|
| `gnfp-cminer-1.1.4-macos.tar.gz` | Darwin **arm64** scalar binary + source (`brew` OpenSSL@3 dylib) |
| `gnfp-cminer-1.1.4-linux.tar.gz` | Linux **x86_64** source (`libssl.so.3`; rebuild with `sudo apt-get install -y build-essential libssl-dev && make`). ELF leftover is attached when Germany builds it. |
| `gnfp-cminer-1.1.4-windows.zip` | Windows source + `example.bat`. PE leftover on the laptop (scalar; no `-mavx2`). |

Same naming as other GNFP clients (`gnfp-cminer-VERSION-platform`). Public pin: https://github.com/rgsneddon/gnfp-cminer/releases/tag/v1.1.4

Do **not** recut public **1.1.3** / **1.1.2**. **1.1.2** Linux ELF still has AVX2 ymm. Haswell leftover stays under `leftover/macos-x86_64-haswell-bigsur/` (Air only, not a second pin). rvp-design `1.0.6-max-autotune` uploads stay under `leftover/rvp-1.0.6-max/` (not a pin).

## Credit

- Original closed miner and 5% fee: [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer)
- Work hash / stratum: GNFPHash-v1 (same vector as the deprecated Node miner)
