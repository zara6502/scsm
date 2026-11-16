# Building the HEIC codec DLL

[Русская версия](BUILDING.ru.md)

This document describes how to build `ride_codec_heic.dll`, a decoder codec
for Far Ride Viewer 2, from a clean checkout. The codec contract itself is
specified in the [Codec API reference](../docs/CODEC_API.md).

## 1. Prerequisites

- **MSYS2** with the **UCRT64** environment (<https://www.msys2.org>).
- Packages inside the UCRT64 shell:

  ```bash
  pacman -S mingw-w64-ucrt-x86_64-{cmake,ninja,gcc}
  ```

- `curl` and `tar` (included in MSYS2 by default) — used once by the
  dependency script to download pinned library sources.
- Internet access during the first dependency build only.

The produced DLL is 64-bit and matches a 64-bit Far Manager. To build for a
different architecture, use the matching MSYS2 environment (e.g. CLANGARM64
for Windows on ARM) and rebuild the dependencies there as well.

## 2. Layout

```text
codec-heic/
|-- CMakeLists.txt       codec DLL + test harness
|-- build-deps.sh        downloads and builds static libde265 + libheif
|-- src/
|   |-- codec.cpp        Ride Codec ABI exports (v1 + optional v2)
|   `-- heic_decoder.cpp  Probe (ftyp brands) and Decode (libheif)
|-- tests/harness.cpp    loads the built DLL and checks the ABI contract
`-- deps/                created by build-deps.sh (not committed)
    |-- src/             pinned dependency sources
    |-- build-*/         dependency build trees
    `-- inst/            installed headers and static .a libraries
```

Dependency sources are **not committed**. `build-deps.sh` downloads exact
pinned releases, so builds are reproducible:

- libde265 v1.1.3 (<https://github.com/strukturag/libde265>)
- libheif v1.23.7 (<https://github.com/strukturag/libheif>)

Both are linked statically; the resulting DLL depends only on `KERNEL32` and
the system UCRT (present on Windows 10+), and exports only the
`RideCodec*` functions.

## 3. Build

Run all commands from the **UCRT64** shell (or ensure
`C:\msys64\ucrt64\bin` is on `PATH`):

```bash
cd codec-heic

# 1) Static libde265 + libheif into deps/inst (a few minutes)
bash build-deps.sh

# 2) Codec DLL + test harness
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Result:

- `build/Codecs/ride_codec_heic.dll` — the codec (place into the `Codecs`
  directory next to `FarRideViewer.dll`)
- `build/codec_test.exe` — the test harness

If the dependency prefix was moved, pass it explicitly:
`cmake -S . -B build -G Ninja -DHEIC_DEPS_PREFIX=/path/to/deps/inst`.

## 4. Tests

```bash
./build/codec_test.exe build/Codecs/ride_codec_heic.dll <file.heic> [out.bmp]
```

The harness loads the DLL exactly like the host does and checks:

1. export table (7 decoder exports present, no encode exports);
2. `GetInfo` / `GetId` / `GetCapabilities` field and version rules;
3. `Probe` — real files, garbage, truncated headers, AVIF brands rejected;
4. `Decode` — pixel format, stride, buffer size, sample pixels;
5. `DecodeEx` — metadata structure;
6. repeated Decode/FreeImage cycles and double free;
7. error paths — garbage input, null input, fake HEIC container (no crash).

The exit code is the number of failed checks; `0` means the contract holds.
With `[out.bmp]` the decoded image is written as a 24-bit BMP for visual
inspection. A sample HEIC lives in `../docs/`.

## 5. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `Не найден .../libheif.a` at CMake configure | Dependencies not built yet — run `bash build-deps.sh` |
| `undefined reference to __imp_heif_*` | Built against a shared libheif or forgot `LIBHEIF_STATIC_BUILD` — rebuild deps with `build-deps.sh` and the codec via this CMakeLists |
| `undefined reference to __imp_de265_*` | libheif compiled without `LIBDE265_STATIC_BUILD` — `build-deps.sh` passes it via `CMAKE_CXX_FLAGS`; use the script |
| `LoadLibrary` fails in the harness or Far | Architecture mismatch (32/64-bit) or missing UCRT on very old Windows |
| Codec not listed in Viewer 2 after replace | Codec registry is cached per Far session — restart Far Manager |

## 6. Notes for distributors

- libheif is LGPL-3.0 / MPL-2.0 (dual), libde265 is LGPL-3.0. When shipping
  a statically linked DLL you may need to provide object files or source
  offer per license terms; see the `COPYING` files of both projects.
- Pinned versions can be updated by editing the tag URLs in
  `build-deps.sh`; verify the test harness after upgrading.
