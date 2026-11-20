# PdnRide — FileType-плагин RIDE для Paint.NET

Открытие и сохранение изображений формата **RIDE** (Run · Index · Delta ·
Escape, lossless QOI-стиль с независимыми блоками) в Paint.NET 5.2+
(net11.0, проверено на 5.20).

## Архитектура

```
Paint.NET ── PdnRide.dll (обвязка: IFileTypeFactory, PropertyBasedFileType,
│                        P/Invoke, IndirectUI-настройки)
│
└──► RideCodec.dll (нативный кодек, C ABI над rideent)
        └── rideent (reference-реализация RIDE, C:/work/rideent)
```

- **RideCodec.dll** — нативный кодек с задокументированным C ABI
  (`ridecodec/include/ride_codec/ride_codec.h` — правила памяти,
  потокобезопасность, версионирование опций, коды ошибок). Собирается
  статически с reference-реализацией rideent. Все варианты кодирования
  rideent: профили **RGB / INDEXED / QOI**, выбор профиля Auto,
  rANS-слой энтропии (Off/Auto/On), размеры блоков, оба COPY-парса.
  Формат RIDE v1 **не хранит альфу** — альфа при сохранении отбрасывается,
  при загрузке A = 255.
- **PdnRide.dll** — только связующая обвязка для Paint.NET: Load,
  Save As с окном настроек.

## Окно настроек сохранения (Save As → RIDE)

| Опция | Значения |
|---|---|
| Profile | Auto (по умолчанию) / RGB / Indexed / QOI |
| rANS entropy layer | Off / Auto (≥2% выигрыш) / On |
| Block size | Default (262144 px) / Small / Large / Single block |
| Linear colors | флаг kFlagLinear в заголовке |

## Сборка RideCodec.dll (MSYS2 UCRT64)

Требуется клон rideent (по умолчанию ищется в `C:/work/rideent`,
переопределяется `-DRIDEENT_ROOT=...`):

```bash
cd pdn-ride/ridecodec
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                 # RideCodec.dll + codec_smoke.exe
./build/codec_smoke.exe             # smoke: encode/decode/ошибки
```

## Сборка плагина и тестов (.NET 11 SDK)

```bash
cd pdn-ride/Plugin
dotnet build Plugin.csproj -c Release   # → bin/Release/PdnRide.dll
cd ../TestApp
dotnet build TestApp.csproj -c Release  # → bin/Release/TestApp.exe + RideCodec.dll
```

## Установка

1. `Plugin/bin/Release/PdnRide.dll` → в одно из мест:
   - `C:\Program Files\paint.net\FileTypes\` (права администратора)
   - `Documents\paint.net App Files\FileTypes\`
2. `ridecodec/build/RideCodec.dll` → **в ту же папку `paint.net App Files\FileTypes\`**
   (рядом с PdnRide.dll; для варианта в Program Files — рядом с
   `PaintDotNet.exe`, т.е. в корень установки)
3. Полностью перезапустить Paint.NET.

В диалогах Open/Save появится формат **RIDE (*.ride)**; Save As показывает
окно настроек кодирования.

## Тесты

```bash
# C++ smoke нативного кодека (см. выше)
# C# тест: roundtrip всех профилей + сверка reference-файлов
cd pdn-ride/TestApp
dotnet run -c Release -- <папка с .ride/.ppm>
```

`tests/images/*.ride` созданы официальным `ride_convert` из
`tests/images/*.png`; `*.ppm` — их пиксельная копия для сверки
(декодирование через RideCodec.dll сравнивается с PPM побайтово).

## Расширение формата

При добавлении новых профилей/опций в rideent:
1. Обновите `ride::EncodeOptions`-маппинг в `ridecodec/src/ride_codec.cpp`
   и структуру `RideEncodeOptions` в `ride_codec.h` (новые поля — в конец,
   struct_size обеспечивает совместимость);
2. Пересоберите RideCodec.dll;
3. Добавьте опцию в `RideFileType.OnCreateSavePropertyCollection` /
   `OnCreateSaveConfigUI` и в `OnSaveT` плагина;
4. Обновите тесты (`codec_smoke.cpp`, `TestApp`).

Контракт DLL не меняется при добавлении опций — старые хосты продолжают
работать с новой DLL.
