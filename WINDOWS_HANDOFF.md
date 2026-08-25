# Windows pointer — gnfp-cminer

**Do not use this file as the pin list.** All leftover lives in:

**https://github.com/rgsneddon/handoff/blob/main/HANDOFF.md**

Current pin here: **0.5 public**. https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.5

Same hasher as **0.4**. Wire `client=GNFPHash` `version=0.5`. Do **not** recut **0.4**. Do **not** ship leftover `1.0.6-max-autotune`.

Darwin **arm64** DMG + Linux **x86_64 ELF** + Windows **PE** (`gnfp-cminer.exe` + `example.bat` at zip root) are on **the same** `v0.5`.

## PE leftover — **done** (attached to `v0.5`)

`gnfp-cminer-0.5-windows.zip` has `gnfp-cminer.exe` at the zip root plus `example.bat`. Static PE (no extra DLLs). `--clobber` onto **the same** `v0.5`. **No sibling tag.**

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
