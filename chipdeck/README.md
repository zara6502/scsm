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
  - [engine_dr](plugins/engine_dr) — **рабочий движок** на dr_libs
    (single-header, public domain): MP3 (.mp3), FLAC (.flac), WAV (.wav).
    Линейный ресемплинг файловой частоты под частоту host'а, mono →
    stereo, Seek. Самодостаточная DLL ~550 КБ (KERNEL32 + UCRT).
  - [engine_stub](plugins/engine_stub) — заглушка для проверки ABI.
- **ChipLibrary** (`plugins/`) — провайдеры онлайн-библиотек.
  - [lib_botb](plugins/lib_botb) — **рабочий плагин** Battle of the Bits
    (API v1, публичный): Top rated / Search / Random, рейтинг 0–1000,
    скачивание оригиналов. Скачивание на BotB требует логин —
    email/password задаются в `Libraries/botb.ini` (см. схему настроек
    плагина); без них загрузка вернёт CHP_ERR_AUTH.
  - Host: окно «Library» (меню лотка → Library): Top rated/Search/Random,
    двойной клик по записи — скачивание в кеш (`cache/<id>/`) и
    воспроизведение, если для формата есть движок.

- ABI: [include/chipdeck_api.h](include/chipdeck_api.h) (v1)
- Дизайн: [docs/DESIGN.ru.md](docs/DESIGN.ru.md) (English: [DESIGN.md](docs/DESIGN.md))

## Проверка движков (MSYS2 UCRT64)

```bash
# Заглушка ABI
cd chipdeck/plugins/engine_stub
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/stub_test build/Engines/engine_stub.dll

# Рабочий движок GME (AY + POKEY + NSF)
cd ../engine_gme
bash build-deps.sh   # статическая libgme 0.6.5 → deps/inst
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/gme_test.exe build/Engines/engine_gme.dll <файл.ay|файл.sap> out.wav

# Рабочий движок dr_libs (MP3/FLAC/WAV) — зависимости не нужны,
# single-header библиотеки уже в plugins/engine_dr/dr/
cd ../engine_dr
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/dr_test.exe build/Engines/engine_dr.dll <файл.mp3|файл.flac|файл.wav> out.wav

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

# Библиотека BotB (нужен интернет)
cd ../lib_botb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/botb_test.exe build/Libraries/lib_botb.dll

# Проверка интеграции библиотек в host
CHIPDECK_LIBS=../plugins/lib_botb/build/Libraries ../../host/build/chipdeck_host.exe --top
```

## Дистрибутив

```bash
cd chipdeck
bash make-dist.sh     # → dist/ChipDeck/ (exe + Engines + Libraries + README)
```

Готовая папка ~20 МБ, архив `dist/ChipDeck-win64.zip` ~5.7 МБ.
Скопировать на любую Windows x64 машину и запустить `chipdeck_host.exe`
— всё остальное (Engines, Libraries, кеш) подхватывается автоматически.

Тест GME проверяет: Probe, Open (включая отказ на мусоре), подтюны,
метаданные, Select, рендер 5 секунд с проверкой не-тишины, Seek, WAV-дамп,
циклы Open/Close. Код возврата 0 — контракт выполнен.
