# Windows pointer — gnfp-cminer

**Do not use this file as the pin list.** All leftover lives in:

**https://github.com/rgsneddon/handoff/blob/main/HANDOFF.md**

Current pin here: **1.1.0 public**. https://github.com/rgsneddon/gnfp-cminer/releases/tag/v1.1.0

Windows zip on **the same** `v1.1.0` already includes `gnfp-cminer.exe` (MinGW PE, static OpenSSL, no extra DLL) plus `pack\win\gnfp-cminer.cmd`. Darwin **arm64** and Linux **x86_64 ELF** are also on that tag. **Do not rebuild the PE.** Haswell leftover is Air-only.

## Laptop leftover (verify, do not rebuild)

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

Do **not** invent a sibling tag. Do **not** rebuild 1.1.0. PE is already on the public tag.
