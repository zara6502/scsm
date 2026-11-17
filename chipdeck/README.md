# ChipDeck — плагинный чиптюн-плеер (проект)

Дизайн и ABI плеера в системном лотке с плагинами двух типов:

- **ChipEngine** (`plugins/`) — движки-декодеры чипов (AY, SID, POKEY...):
  файл → поток PCM.
  - [engine_gme](plugins/engine_gme) — **рабочий движок** на libgme 0.6.5:
    ZX Spectrum AY (.ay, .emul) и Atari POKEY (.sap), статическая линковка,
    самодостаточная DLL ~700 КБ (KERNEL32 + UCRT).
  - [engine_sidplayfp](plugins/engine_sidplayfp) — **рабочий движок** на
    libsidplayfp 3.1.2 + libresidfp 1.3.0: C64 SID (.sid, PSID/RSID),
    модель чипа из заголовка тюна, mono → stereo.
  - [engine_openmpt](plugins/engine_openmpt) — **рабочий движок** на
    libopenmpt 0.8.10: трекерные форматы MOD/S3M/XM/IT/MTM/MO3/...
    (полный список в extensions движка), sub-songs, честная длительность,
    Seek.
  - [engine_stub](plugins/engine_stub) — заглушка для проверки ABI.
- **ChipLibrary** — провайдеры онлайн-библиотек с рейтингами
  (ModArchive, BotB). В v1 — только дизайн.

- ABI: [include/chipdeck_api.h](include/chipdeck_api.h) (v1)
- Дизайн: [docs/DESIGN.ru.md](docs/DESIGN.ru.md) (English: [DESIGN.md](docs/DESIGN.md))

## Проверка движков (MSYS2 UCRT64)

```bash
# Заглушка ABI
cd chipdeck/plugins/engine_stub
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/stub_test build/Engines/engine_stub.dll

# Рабочий движок GME (AY + POKEY)
cd ../engine_gme
bash build-deps.sh   # статическая libgme 0.6.5 → deps/inst
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/gme_test.exe build/Engines/engine_gme.dll <файл.ay|файл.sap> out.wav

# Рабочий движок SID (C64) — нужны autotools в MSYS2 (pacman -S autoconf
# automake libtool pkgconf) и xa65 в PATH (см. build-deps.sh)
cd ../engine_sidplayfp
bash build-deps.sh   # статические libresidfp + libsidplayfp → deps/inst
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/sid_test.exe build/Engines/engine_sidplayfp.dll <файл.sid> out.wav

# Рабочий движок OpenMPT (MOD/S3M/XM/IT) — нужны autotools в MSYS2
cd ../engine_openmpt
bash build-deps.sh   # статическая libopenmpt 0.8.10 → deps/inst
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/ompt_test.exe build/Engines/engine_openmpt.dll <файл.mod|s3m|xm|it> out.wav
```

Тест GME проверяет: Probe, Open (включая отказ на мусоре), подтюны,
метаданные, Select, рендер 5 секунд с проверкой не-тишины, Seek, WAV-дамп,
циклы Open/Close. Код возврата 0 — контракт выполнен.
