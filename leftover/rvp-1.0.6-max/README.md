# leftover — rvp-design 1.0.6-max-autotune (2026-08-24)

GitHub UI uploads by **rvp-design** onto `rgsneddon/gnfp-cminer` `main`:

| File | What it is |
|------|------------|
| `gnfp_cminer_win.c` | Unified Windows+Linux source (`VERSION 1.0.6-max-autotune`) |
| `gnfp_cminer_lin` | Stripped Linux x86_64 ELF of that source (Ubuntu 11.4, libssl.so.3) |
| `gnfp_win.exe` | Windows PE of that source |

**Not the public pin.** Pin is official scalar `src/` **1.1.4**.

## Safety (do not ship these binaries)

1. **Fee shares are not credited to `FEE_ADDR` on the live book.** Identity JSON has no `"login"` field. `"user"` is always the miner's gnfp1. Submit then overwrites `"id"` with the jobId. Pool does `who = msg.login || msg.user`, so fee submits land on the miner address. Official miner must keep `"login":"<FEE_ADDR>.<fee_worker>"`.
2. **SHA-NI `sha256_fast_ni` is a single 64-byte block.** Round hashes are `60+pre_len` bytes. `pre_len < 40` still overflows the stack (`memcpy` 72+ into 64). Live 64-char hex `preWork` skips this path (OpenSSL fallback, hashes OK). `--selftest` / `--bench` / short pre take the broken path on SHA-NI CPUs.
3. Reintroduces `--threads` **256 clamp**. Official pin has none.
4. `SSL_VERIFY_NONE`. Same as earlier C miner; not a new host.
5. Writes `gnfp_cminer_best.cfg` in the cwd. No extra hosts, no `system`/`popen`/`WinExec`. Fee address matches the published gnfp1. Selftest vector matches official `GNFPHash-v1`.

Fee-worker idea kept: last 6 of miner address + worker, as `fTAIL_worker` on `FEE_ADDR`, so the book shows who is paying.
