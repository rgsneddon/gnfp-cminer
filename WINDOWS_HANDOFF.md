# Windows pointer — gnfp-cminer

**Do not use this file as the pin list.** All leftover lives in:

**https://github.com/rgsneddon/handoff/blob/main/HANDOFF.md**

Current pin here: **1.1.6 public** (runtime SHA-NI then AVX2, `--backend scalar` hatch, fee worker `fTAIL_worker`). https://github.com/rgsneddon/gnfp-cminer/releases/tag/v1.1.6

No `--threads` 256 clamp. Do **not** recut **1.1.5**. Do **not** ship leftover `1.0.6-max-autotune`. Haswell leftover is Air-only. Do not overvolt or enable AVX in BIOS.

## PE / ELF on the same tag (attached 2026-08-25)

```
gnfp-cminer.exe --selftest
```

Must print:

```
selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
```

Then optional live TLS:

```
gnfp-cminer.exe --user gnfp1YOURADDRESS.worker --threads 1
```

Do **not** invent a sibling tag. Do **not** rebuild 1.1.0.
