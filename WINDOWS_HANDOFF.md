# Windows pointer — gnfp-cminer

**Do not use this file as the pin list.** All leftover lives in:

**https://github.com/rgsneddon/handoff/blob/main/HANDOFF.md**

Current pin here: **0.4 public**. https://github.com/rgsneddon/gnfp-cminer/releases/tag/v0.4

Desktop `gnfp_cminer_v0.4` as shipped — **no miner changes**. Do **not** recut **0.4** / **1.1.6**. Do **not** ship leftover `1.0.6-max-autotune`. Haswell leftover is Air-only.

## PE / ELF on the same tag (attached 2026-08-25)

Windows pack is the Desktop `gnfp` folder unchanged (`gnfp_cminer_v0.4.exe` + DLLs). Keep the DLLs next to the exe.

```
gnfp_cminer_v0.4.exe --selftest
```

Must print:

```
selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
```

Then optional live TLS:

```
gnfp_cminer_v0.4.exe --user gnfp1YOURADDRESS.worker --threads 1
```

Do **not** invent a sibling tag. Do **not** rebuild the Windows PE. Do **not** recut 1.1.6.
