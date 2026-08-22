# Leftover — macOS x86_64 Haswell / Big Sur 11.7.11 only

One-platform native build from the **2013 MacBook Air 6,1** (Intel i5-4250U, AVX2, no SHA-NI, no Homebrew OpenSSL).

This is **not** a multi-platform pin. 1.1.0 packs on private `v1.1.0` now include Darwin **arm64**, Linux **x86_64 ELF**, and Windows **PE**. Keep this leftover as the Air-only Haswell binary.

| | |
|--|--|
| Host | MacBookAir6,1 · Big Sur 11.7.11 |
| Binary | `gnfp_cminer` Mach-O x86_64 |
| TLS | Apple SecureTransport (no `brew openssl`) |
| Hash | AVX2 8-way SHA-256 (`backend=avx2-x8`) |
| Wire | `client=GNFPHash` `version=1.0.5` (admit floor 1.0.4+) |
| Fee | 5% dual-login, same friend address as 1.1.0 |
| Stats | pool `hashrate` field is **proven** (accepted × 2^diff / s); STATS line also prints `call=` |

Selftest:

```
./gnfp_cminer --selftest
# selftest ok 986437c4…d4eb backend=avx2-x8 avx2=yes
```

Run on **this** Air (keep `--threads 1` — two threads cook the chassis):

```
./gnfp_cminer --user gnfp1YOURADDRESS.worker --threads 1
```

Will **not** run on Apple silicon (arm64) or Linux. Other platforms are on private `v1.1.0` (no sibling tag):

- **macOS arm64** — `gnfp-cminer-1.1.0-macos.tar.gz` (OpenSSL@3).
- **Linux x86_64** — `gnfp-cminer-1.1.0-linux.tar.gz` ELF (`libssl.so.3`).
- **Windows** — `gnfp-cminer-1.1.0-windows.zip` includes `gnfp-cminer.exe` (static OpenSSL). Laptop leftover is `--selftest` on real Windows.

Live book `de.restoreprivacy.online:1474` is still **fixed diff 14**. Next pool work is **vardiff** (~1 share / 5–10s per TCP session). See `rgsneddon/handoff` `HANDOFF.md`.
