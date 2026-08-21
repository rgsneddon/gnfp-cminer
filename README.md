# gnfp-cminer

Community **$GNFP** CPU miner. Rebuild of [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer) (that GitHub tree is a stripped Linux ELF, not source).

This is **not** the official miner. Official pin: [rgsneddon/GNFPHash](https://github.com/rgsneddon/GNFPHash).

- Coin: GNFP
- Algo: **GNFPHash** (same 8-round `GNFPHash-v1` work hash as official GNFPHash)
- Wire: `client=GNFPHash` `version=1.1.0` (live admit floor is **1.0.4+**)
- TLS by default to `de.restoreprivacy.online:1474` (`--notls` for local plaintext)
- Declared **5%** dual-login fee (see below)
- Repo is **private** (operator specified; not a public pin): https://github.com/rgsneddon/gnfp-cminer
- Official miner: [rgsneddon/GNFPHash](https://github.com/rgsneddon/GNFPHash)

## Honest architecture

`rvp-design/gnfp_cminer` shipped a closed ELF that logged in as official `GNFPHash 1.0.5`. This rebuild:

- keeps the friend’s published **5%** fee address
- is **open source**
- does **not** claim to be official **1.0.5** or **1.0.6**
- reports the farm it actually runs (`threads` = hash workers on the main login; fee login `threads=1`)

## Dev fee (5%)

Every 20th meeting nonce is submitted on a **second connection**:

```
gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1.fee
```

That is the address published in the original ELF. The fee socket reports **threads=1** so two logins from one box do not claim two full farms. If the fee socket is down, **all** shares stay on your login.

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

`--threads` is real POSIX threads, cap = this machine’s logical CPUs (max 256). Default = physical cores minus 1.

A real `gnfp1` payout address is required. Worker tag is 1–24 letters/digits/`_`/`-` (default `worker`).

## Packs

GNFP client pack names, one tag `v1.1.0` (no sibling tags):

| File | What is inside |
|------|----------------|
| `gnfp-cminer-1.1.0-macos.tar.gz` | Darwin binary + source (`brew` OpenSSL@3 dylib) |
| `gnfp-cminer-1.1.0-linux.tar.gz` | source + Makefile (`sudo apt-get install -y build-essential libssl-dev && make`) |
| `gnfp-cminer-1.1.0-windows.zip` | source + `pack/win/gnfp-cminer.cmd` (MinGW + OpenSSL + pthreads) |

This Mac cannot link Linux/Windows OpenSSL, so those two packs are source. Same naming as other GNFP clients (`gnfp-cminer-VERSION-platform`). Tag `v1.1.0` is on this **private** repo.

## Leftover — one Darwin x86_64 host (2013 Air)

`leftover/macos-x86_64-haswell-bigsur/` is a **Haswell / Big Sur 11.7.11** native binary + source from MacBookAir6,1 (no Homebrew OpenSSL; Apple TLS + AVX2). Wire `GNFPHash 1.0.5`. It is **not** a second pin and **not** a substitute for the 1.1.0 packs.

Other platforms still need a modern Mac or the Windows laptop (`pack/win`, Linux `make`). See that leftover README.

## Credit

- Original closed miner and 5% fee: [rvp-design/gnfp_cminer](https://github.com/rvp-design/gnfp_cminer)
- Work hash / stratum: official [rgsneddon/GNFPHash](https://github.com/rgsneddon/GNFPHash)
