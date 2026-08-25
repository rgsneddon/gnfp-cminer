# Windows pointer — gnfp-cminer

**Do not use this file as the pin list.** All leftover lives in:

**https://github.com/rgsneddon/handoff/blob/main/HANDOFF.md**

Current pin here: **0.5 public**. https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.5

Same hasher as **0.4**. Only the 5% fee clock changed: per-process random offset `0..19` and lazy fee connect (second login on the first fee share). Wire still `client=GNFPHash` `version=1.1.2`. Do **not** recut **0.4**. Do **not** ship leftover `1.0.6-max-autotune`.

Darwin **arm64** + Linux **x86_64 ELF** are on **the same** `v0.5`. **Leftover (do this now):** Windows PE attach to **the same** `v0.5`.

## PE leftover — do this now (not yet attached)

Build from `rgsneddon/gnfp-cminer` tag **`v0.5`** (not 0.4, not 1.1.6). Same OpenSSL as other PE leftovers. Put `gnfp-cminer.exe` at the zip root (DLLs next to it if the PE is not static). `example.bat` is already in the source zip on the tag — `--clobber` the PE zip onto **the same** `v0.5`. **No sibling tag.**

```
gnfp-cminer.exe --selftest
```

Must print:

```
selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
```

`--help` / startup must mention fee offset (`offset=N/20`) and must **not** open the fee socket until the first fee share. Then optional live TLS:

```
gnfp-cminer.exe --user gnfp1YOURADDRESS.worker --threads 1
```

Do **not** invent a sibling tag. Do **not** rebuild 0.4. Do **not** rebuild 1.1.6.
