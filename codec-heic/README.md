# ride_codec_heic — DLL-кодек HEIC/HEIF для Far Ride Viewer 2

Декодирует изображения HEIC/HEIF (HEVC внутри HEIF-контейнера) в
нормализованный top-down RGBA8 согласно [Ride Codec API](../docs/CODEC_API.ru.md).

Строится как **самодостаточная DLL**: libheif и libde265 линкуются статически,
посторонних DLL не требуется — только системная UCRT (Windows 10+).

## Экспорты

| Функция | Уровень |
|---|---|
| `RideCodecGetInfo`, `RideCodecProbe`, `RideCodecDecode`, `RideCodecFreeImage` | обязательный ABI v1 |
| `RideCodecGetId` (`heic`), `RideCodecGetCapabilities`, `RideCodecDecodeEx` | необязательные ABI v2 |

Кодирование не реализовано (в capabilities не объявляется).

- **Имя**: `HEIC (HEIF/HEVC)`; **расширения**: `heic;heif;hif`
- **Probe**: распознаёт по брендам `ftyp` (`heic/heix/heim/heis/hevc/...`,
  контейнеры `mif1/mif2/msf1/miaf`); AVIF-бренды (`avif/avis/av01`) отвергаются
- **Decode**: 8/10-бит HEVC (HDR сводится к 8 бит), альфа-канал, повороты
  irot/imir применяются libheif автоматически; лимит 128 Мпикс и 1 ГиБ
  внутри декодера
- **DecodeEx**: метаданные — исходный формат (`YCbCr N-bit` / `Grayscale N-bit`),
  флаг `RIDE_CODEC_IMAGE_META_ALPHA`

## Сборка и тесты

Подробные инструкции — в [BUILDING.ru.md](BUILDING.ru.md)
(English: [BUILDING.md](BUILDING.md)). Кратко, в шелле MSYS2 UCRT64:

```bash
bash build-deps.sh     # статические libde265 v1.1.3 + libheif v1.23.7 → deps/inst
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build    # → build/Codecs/ride_codec_heic.dll + build/codec_test.exe
./build/codec_test.exe build/Codecs/ride_codec_heic.dll ../docs/sample-heic-*.heic out.bmp
```

## Тесты

```bash
./build/codec_test.exe build/Codecs/ride_codec_heic.dll <файл.heic> [out.bmp]
```

Стенд грузит DLL как host, проверяет таблицу экспортов, GetInfo/GetId/
GetCapabilities, Probe (позитивные/негативные/усечённые/AVIF-случаи),
Decode с проверкой RGBA8/stride/data_size, DecodeEx с метаданными, циклы
Decode/Free (включая повторный free), пути с мусорным и фальшивым вводом.
`0 неудачных проверок` — контракт выполнен. С ключом `[out.bmp]` пишет
декодированное изображение для визуальной проверки.

Установка: скопировать DLL в каталог `Codecs` рядом с `FarRideViewer.dll`
(разрядность должна совпадать с Far Manager), перезапустить Far Manager.

## Лицензии зависимостей

- [libheif](https://github.com/strukturag/libheif) — LGPL-3.0 / MPL-2.0 (по выбору)
- [libde265](https://github.com/strukturag/libde265) — LGPL-3.0

При распространении DLL с статической линковкой могут потребоваться
объектные файлы для перелинковки пользователем (см. COPYING проектов).
