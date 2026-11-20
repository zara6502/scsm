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

- **RideCodec.dll** — нативный кодек с задокументированным C ABI.
  Источник истины по контракту: `ridecodec/include/ride_codec/ride_codec.h`,
  человекочитаемое описание — `ridecodec/CONTRACT.md`. Собирается статически
  с reference-реализацией rideent. Все варианты кодирования rideent:
  профили **RGB / INDEXED / QOI**, rANS-слой энтропии (Off/Auto/On),
  размеры блоков, оба COPY-парса, квантование с дизерингом (см. ниже).
  Формат RIDE v1 **не хранит альфу** — альфа при сохранении отбрасывается,
  при загрузке A = 255.
- **PdnRide.dll** — только связующая обвязка для Paint.NET: Load, Save As
  с окном настроек. Собственного кодека в ней нет — кодек вызывается
  через P/Invoke из RideCodec.dll.

## Окно настроек сохранения (Save As → RIDE)

| Опция | Значения |
|---|---|
| Profile | Auto (по умолчанию) / RGB / Indexed / QOI |
| Indexed colors | выпадающий список 2–256: сначала степени двойки (256, 128, 64, 32, 16, 8, 4, 2), затем 255, 254, … 3 |
| Floyd-Steinberg dithering | Off (по умолчанию) / On — при любом числе цветов квантования |
| Black & white palette | On = жёсткие чёрный/белый вместо кластеров изображения — только при Indexed colors = 2 |
| rANS entropy layer | Off / Auto (≥2% выигрыш) / On |
| Block size | Default (262144 px) / Small / Large / Single block |
| Linear colors | флаг kFlagLinear в заголовке |

### Профили

- **Auto** — всегда RGB, без индексирования и без квантования
  (решение владельца; эталонный Auto rideent с INDEXED для ≤256 цветов
  не используется).
- **RGB** — 24 бита, без квантования.
- **Indexed** — палитра: точная, когда уникальных цветов ≤ «Indexed
  colors»; иначе квантование (см. ниже). Единственный профиль,
  в котором работают «Indexed colors», Floyd–Steinberg и B/W-палитра.
- **QOI** — канонические QOI-операции в RIDE-контейнере, без квантования.

### Квантование и дизеринг

Профиль INDEXED для фото (>256 цветов) требует **квантования палитры**:

- алгоритм: **Median Cut в Oklab** (перцептивное пространство) +
  **3 итерации k-means с ΔE в Oklab** — «квантование через дельту E»;
- опция **Indexed colors** (2–256): квантование выполняется, когда
  уникальных цветов больше заданного N; иначе палитра точная и
  дизеринг не нужен;
- **Floyd–Steinberg** (по умолчанию off): дизеринг при фактическом
  квантовании; шум дизеринга увеличивает размер файла;
- **B/W-палитра** — при Indexed colors = 2: жёсткие чёрный/белый
  вместо кластеров изображения (классическое ч/б дизеринговое фото);
- квантование реализовано в RideCodec.dll. История контракта:
  **v1** — базовые поля; **v2** — `quantize_colors`, `dither`;
  **v3** — `bw_palette`. Новые поля добавляются в конец
  `RideEncodeOptions`, старые хосты совместимы через `struct_size`.
  rideent не изменяется; при появлении RGBA-профиля квантизатор
  расширяется по той же схеме (Oklab уже выбран как пространство ΔE).

## Сборка RideCodec.dll (MSYS2 UCRT64)

Требуется клон rideent (по умолчанию ищется в `C:/work/rideent`,
переопределяется `-DRIDEENT_ROOT=...`):

```bash
cd pdn-ride/ridecodec
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                 # RideCodec.dll + codec_smoke.exe
./build/codec_smoke.exe             # smoke: encode/decode/квантование/ошибки
```

## Сборка плагина и тестов (.NET 11 SDK)

```bash
cd pdn-ride/Plugin
dotnet build Plugin.csproj -c Release   # → bin/Release/PdnRide.dll
cd ../TestApp
dotnet build TestApp.csproj -c Release  # → bin/r2/TestApp.exe + RideCodec.dll
```

Внимание: в csproj-файлах `EnableDefaultCompileItems=false` — каждый
новый .cs файл нужно явно добавлять в `<Compile Include=...>` (дважды
забытая фабрика была причиной «молчаливого» пропуска плагина).

## Установка

1. `Plugin/bin/Release/PdnRide.dll` → в одно из мест:
   - `C:\Program Files\paint.net\FileTypes\` (права администратора)
   - `Documents\paint.net App Files\FileTypes\`
2. `ridecodec/build/RideCodec.dll` → **в ту же папку рядом с PdnRide.dll**
   (`paint.net App Files\FileTypes\` или корень установки рядом с
   `PaintDotNet.exe` — PdnRide грузит кодек из своего каталога явно,
   а при отсутствии ищет в корне Paint.NET).
3. **Обновляйте оба файла вместе**: устаревший RideCodec.dll при свежем
   PdnRide.dll — причина «опции не влияют на результат» (кодек без
   квантования просто игнорирует новые поля опций).
4. Полностью перезапустить Paint.NET (завершить `PaintDotNet.exe` —
   DLL держатся процессом и не перечитываются на лету).

В диалогах Open/Save появится формат **RIDE (*.ride)**; Save As показывает
окно настроек кодирования. После сохранения **холст Paint.NET остаётся
оригинальным** — это стандартное неразрушающее поведение PDN (как JPEG
с качеством <100); сохранённый файл при этом содержит именно то, что
задано опциями (проверка: откройте сохранённый .ride или выполните
`ride_info --verify файл.ride` из rideent).

## Тесты

```bash
# C++ smoke нативного кодека (см. выше)
# C# тест: roundtrip всех профилей + сверка reference-файлов
cd pdn-ride/TestApp
dotnet run -c Release -- <папка с .ride/.ppm>

# Эмуляция диалога PDN без GUI: дефолтный токен -> значения -> Save,
# печатает размеры файлов для Auto/Indexed+квантование/RGB
dotnet bin/r2/TestApp.dll --dialog
```

`tests/images/*.ride` созданы официальным `ride_convert` из
`tests/images/*.png`; `*.ppm` — их пиксельная копия для сверки
(декодирование через RideCodec.dll сравнивается с PPM побайтово).

## Диагностика

- **Формат не появился в диалогах** → проверьте, что обе DLL в одной
  папке и что PdnRide.dll содержит `IFileTypeFactory` (без фабрики PDN
  5.2 молча пропускает сборку, вкладка «Plugin Errors» при этом пуста).
- **Опции сохранения не влияют на результат** → почти всегда устаревший
  RideCodec.dll: кодек и плагин обновляются парой.
- **FS «не работает»** → он применяется только при фактическом
  квантовании (уникальных цветов больше «Indexed colors»); для точной
  палитры дизеринг бессмыслен.
- **Settings → Plugin Errors** — штатное место ошибок плагинов, но
  «молчаливые» пропуски (нет фабрики) в него не попадают.

## Расширение формата

При добавлении новых профилей/опций в rideent:
1. Обновите `ride::EncodeOptions`-маппинг в `ridecodec/src/ride_codec.cpp`
   и структуру `RideEncodeOptions` в `ride_codec.h` (новые поля — в конец,
   struct_size обеспечивает совместимость);
2. Пересоберите RideCodec.dll;
3. Добавьте опцию в `RideFileType.OnCreateSavePropertyCollection` /
   `OnCreateSaveConfigUI` и в `OnSaveT` плагина; не забудьте новый .cs
   файл добавить в `Plugin.csproj` (`<Compile Include=...>`);
4. Обновите тесты (`codec_smoke.cpp`, `TestApp`) и этот README.

Контракт DLL не меняется при добавлении опций — старые хосты продолжают
работать с новой DLL.
