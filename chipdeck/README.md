# ChipDeck — плагинный чиптюн-плеер (проект)

Дизайн и ABI плеера в системном лотке с плагинами двух типов:

- **ChipEngine** (`plugins/`) — движки-декодеры чипов (AY, SID, POKEY...):
  файл → поток PCM. Заглушка: [plugins/engine_stub](plugins/engine_stub).
- **ChipLibrary** — провайдеры онлайн-библиотек с рейтингами
  (ModArchive, BotB). В v1 — только дизайн.

- ABI: [include/chipdeck_api.h](include/chipdeck_api.h) (v1)
- Дизайн: [docs/DESIGN.ru.md](docs/DESIGN.ru.md) (English: [DESIGN.md](docs/DESIGN.md))

## Проверка заглушки (MSYS2 UCRT64)

```bash
cd chipdeck/plugins/engine_stub
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/stub_test build/Engines/engine_stub.dll
```

Код возврата 0 — контракт GetInfo/Probe выполнен.
