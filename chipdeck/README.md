# ChipDeck — плагинный чиптюн-плеер (проект)

Дизайн и ABI плеера в системном лотке с плагинами двух типов:

- **ChipEngine** (`plugins/`) — движки-декодеры чипов (AY, SID, POKEY...):
  файл → поток PCM.
  - [engine_gme](plugins/engine_gme) — **рабочий движок** на libgme 0.6.5:
    ZX Spectrum AY (.ay) и Atari POKEY (.sap), статическая линковка,
    самодостаточная DLL ~700 КБ (KERNEL32 + UCRT).
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
```

Тест GME проверяет: Probe, Open (включая отказ на мусоре), подтюны,
метаданные, Select, рендер 5 секунд с проверкой не-тишины, Seek, WAV-дамп,
циклы Open/Close. Код возврата 0 — контракт выполнен.
