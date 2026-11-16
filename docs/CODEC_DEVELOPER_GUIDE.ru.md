# Руководство разработчика кодеков

[English version](CODEC_DEVELOPER_GUIDE.md)

В этом руководстве описано создание DLL-кодека для Far Ride Viewer 2, добавление необязательных расширений ABI v2 и проверка модуля перед распространением.

Точные размеры структур и правила проверки полей приведены в [справочнике Codec API](CODEC_API.ru.md). Общий заголовок `include/ride_viewer_codec_api.h` является источником истины.

## 1. Выберите уровень интерфейса

| Уровень | Экспорты | Результат в Viewer 2 |
|---|---|---|
| ABI v1 | `GetInfo`, `Probe`, `Decode`, `FreeImage` | Кодек только для декодирования |
| Стабильная идентичность | Добавить `GetId` | Стабильный ID для настроек и профилей |
| Метаданные исходника | Добавить `DecodeEx` | Информация об исходном формате пикселей на экране F1 |
| Возможности | Добавить `GetCapabilities` | Явное объявление операций и форматов пикселей |
| Универсальное кодирование | Добавить `FreeEncodedData`, `GetEncodeOptions`, `EncodeEx`, объявить Encode | Save As и экспорт по профилям |

ABI v2 является добавочным. Сначала добейтесь работы декодера v1, а затем добавляйте необязательные экспорты. Значение `RIDE_CODEC_API_VERSION` остаётся равным 1; структура capabilities имеет собственную версию 2.

## 2. Создайте проект кодека

Кодек представляет собой DLL Windows, загружаемую из каталога `Codecs` рядом с `FarRideViewer.dll`.

```text
my-codec/
|-- CMakeLists.txt
|-- codec.cpp
`-- tests/
    |-- codec_test.cpp
    `-- samples/
```

Подключите общий заголовок:

```cpp
#include "ride_viewer_codec_api.h"
```

Не дублируйте структуры ABI в собственном заголовке. Локальная копия может разойтись с host и привести к повреждению памяти на границе DLL.

## 3. Соберите DLL через CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyCodec LANGUAGES CXX)

add_library(my_codec SHARED codec.cpp)
target_include_directories(my_codec PRIVATE
  "${FAR_RIDE_VIEWER_ROOT}/include"
)
target_compile_features(my_codec PRIVATE cxx_std_17)
target_compile_definitions(my_codec PRIVATE
  RIDE_CODEC_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
set_target_properties(my_codec PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "my_codec"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Codecs"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Codecs"
)
```

Сборка в MSYS2 UCRT64:

```bash
cmake -S . -B build -G Ninja \\
  -DCMAKE_BUILD_TYPE=Release \\
  -DFAR_RIDE_VIEWER_ROOT="/path/to/far-ride-viewer-2"
cmake --build build
```

Результат должен находиться в `build/Codecs/my_codec.dll`. Разрядность DLL должна совпадать с разрядностью Far Manager. Для распространяемых сборок MinGW статическая линковка GCC и C++ runtime позволяет избежать дополнительной зависимости от локальных DLL MSYS2:

```cmake
if(MINGW)
  target_link_options(my_codec PRIVATE
    -static -static-libgcc -static-libstdc++
  )
endif()
```

Если кодеку требуются дополнительные DLL, поставьте их в каталог, из которого Windows сможет разрешить их загрузку.

## 4. Реализуйте обязательные функции v1

### GetInfo

Верните короткое имя кодека и список расширений через точку с запятой. Строки должны быть UTF-8, завершаться нулём и помещаться в поля фиксированного размера. Расширения указываются без начальных точек, например `foo;bar`.

### Probe

Probe получает указатель и точное количество байт, а не имя файла. Функция должна проверять `data` и `size` перед чтением, не выходить за пределы переданного диапазона, безопасно отклонять усечённые заголовки и не декодировать всё изображение. Она должна проверять достаточно структуры для предотвращения типичных ложных совпадений.

Расширение влияет только на порядок вызова Probe. До Decode требуется успешное распознавание полного файла.

### Decode

Decode получает полный буфер файла и возвращает RGBA8 со строками сверху вниз. До выделения памяти проверьте заголовок, вариант формата, размеры, количество пикселей, требуемый объём входных данных и все вычисления смещений и размера буфера.

Используйте прямую альфу, не premultiplied alpha. Для непрозрачного исходника установите alpha = 255.

### FreeImage

Выделяйте и освобождайте буфер изображения внутри одной DLL. Host вызывает FreeImage и после неудачного Decode, поэтому функция должна безопасно принимать null, пустые и частично инициализированные структуры.

Не позволяйте исключениям пересекать границу C ABI. Перехватывайте исключения выделения памяти и декодирования внутри экспорта и возвращайте код ошибки вместе с `RideCodecError`.

## 5. Добавьте стабильный ID и capabilities

### Стабильный ID

Экспортируйте `RideCodecGetId` и возвращайте стабильный идентификатор ASCII в нижнем регистре, например `my_codec`. Не используйте строку версии или отображаемое имя. Host использует этот ID для настроек и сохранённых профилей экспорта.

### Capabilities

Кодек с поддержкой v2 должен экспортировать `RideCodecGetCapabilities`:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities* caps)
{
    if (!caps || caps->struct_size < sizeof(*caps))
        return 0;

    const uint32_t caller_size = caps->struct_size;
    std::memset(caps, 0, sizeof(*caps));
    caps->struct_size = caller_size;
    caps->version = RIDE_CODEC_CAPABILITIES_VERSION;
    caps->flags = RIDE_CODEC_CAP_DECODE;
    caps->output_pixel_formats = RIDE_CODEC_PIXEL_RGBA8;
    caps->max_width = 32768;
    caps->max_height = 32768;
    return 1;
}
```

Устанавливайте только те флаги и биты форматов пикселей, которые модуль действительно поддерживает. Декодер обязан объявить Decode и выход RGBA8. Не объявляйте Encode, пока не реализован полный интерфейс кодирования и освобождения памяти.

## 6. Передавайте метаданные исходного формата через DecodeEx

Используйте `RideCodecDecodeEx`, когда нормализованный RGBA8 недостаточно описывает исходный файл. Например, исходный PCX с 4-битной индексацией остаётся индексированным изображением с палитрой, хотя просмотрщик получает RGBA8.

Перед вызовом инициализируйте `RideImageMetadata::struct_size` и `version`. Заполняйте только известные поля. Все строки должны быть UTF-8 и завершаться нулём. Метаданные носят справочный характер; декодирование должно оставаться корректным и без них.

## 7. Добавьте универсальное кодирование

Диалог Save As и сочетания профилей `s1`–`s9` в текущем Viewer 2 используют универсальный интерфейс схемы. Одного экспорта legacy `RideCodecEncode` недостаточно, чтобы кодек появился в этих элементах интерфейса.

Для участия в Save As и профилях экспорта предоставьте:

- `RideCodecGetCapabilities` с флагом `RIDE_CODEC_CAP_ENCODE`.
- `RIDE_CODEC_PIXEL_RGBA8` в `input_pixel_formats`.
- `RideCodecFreeEncodedData`.
- `RideCodecGetEncodeOptions`.
- `RideCodecEncodeEx`.

Два универсальных экспорта должны присутствовать одновременно.

### Пример схемы

Пример объявляет выбор глубины цвета и логический параметр RLE:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetEncodeOptions(
    RideCodecEncodeOptionsSchema* schema)
{
    if (!schema || schema->struct_size < sizeof(*schema))
        return 0;

    const uint32_t caller_size = schema->struct_size;
    std::memset(schema, 0, sizeof(*schema));
    schema->struct_size = caller_size;
    schema->version = RIDE_CODEC_ENCODE_OPTIONS_VERSION;
    schema->option_count = 2;

    auto& depth = schema->options[0];
    std::snprintf(depth.id, sizeof(depth.id), "%s", "bit_depth");
    std::snprintf(depth.label, sizeof(depth.label), "%s", "Bit depth");
    depth.type = RIDE_CODEC_ENCODE_OPTION_CHOICE;
    depth.default_value = 8;
    depth.value_count = 3;

    const int64_t values[] = {1, 4, 8};
    const char* ids[] = {"1bit", "4bit", "8bit"};
    const char* labels[] = {"1-bit", "4-bit", "8-bit"};
    for (uint32_t i = 0; i < depth.value_count; ++i) {
        depth.values[i].value = values[i];
        std::snprintf(depth.values[i].id,
                      sizeof(depth.values[i].id), "%s", ids[i]);
        std::snprintf(depth.values[i].label,
                      sizeof(depth.values[i].label), "%s", labels[i]);
    }

    auto& rle = schema->options[1];
    std::snprintf(rle.id, sizeof(rle.id), "%s", "rle");
    std::snprintf(rle.label, sizeof(rle.label), "%s", "RLE");
    rle.type = RIDE_CODEC_ENCODE_OPTION_BOOLEAN;
    rle.default_value = 1;
    return 1;
}
```

Host проверяет размер схемы, версию, идентификаторы, labels, повторяющиеся ID и значения, defaults, диапазоны, шаги и резервные поля. Некорректная схема не позволяет зарегистрировать универсальный кодек.

### Надёжная реализация EncodeEx

Кодек получает входные пиксели RGBA8. Учитывайте `RideImage::stride`; нельзя предполагать, что строки расположены вплотную. Проверяйте размер структур, версию, количество параметров, ID, резервные поля, значения, размеры изображения и объём входного буфера. Для отсутствующего параметра используйте значение по умолчанию из схемы. Отклоняйте повторяющиеся и неизвестные ID.

При успехе выделите полный буфер закодированных байтов внутри DLL кодека, заполните `RideCodecEncodedData::data` и `data_size`, верните ненулевое значение. При ошибке оставьте указатель данных равным null и размер равным нулю, заполните `RideCodecError`, если он передан, освободите временные выделения и верните ноль.

`RideCodecFreeEncodedData` должна освобождать возвращённый буфер тем же аллокатором и очищать структуру.

## 8. Проверьте модуль

Минимальный набор проверок:

1. Таблица экспортов: все обязательные и заявленные необязательные символы присутствуют.
2. GetInfo: базовая версия API, непустые строки UTF-8, завершающие нули.
3. Probe: положительные и отрицательные примеры, пустой ввод, короткие заголовки, усечение, случайные данные.
4. Decode: размеры, точные значения пикселей, порядок строк, альфа-канал, stride и data_size.
5. Повреждённый ввод: неверные сигнатуры, неподдерживаемые варианты, неверные размеры, переполнение, усечение и чрезмерное число пикселей.
6. Память: освобождение успешного результата, освобождение после неудачного Decode, повторное освобождение и циклы Decode/Free.
7. Capabilities: флаги соответствуют реально реализованным функциям и форматам пикселей.
8. Encode: каждый тип параметров, defaults, проверка диапазонов/шага, неверные ID, корректность результата и FreeEncodedData.
9. Интеграция: загрузка DLL через реестр Viewer 2 и открытие настоящего файла в Far Manager.

Запустите тесты репозитория после конфигурации с FAR SDK:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Набор проверяет загрузку ABI, старые модули, QOI, PCX, декодирование RIDE, реальные файлы изображений и ресемплинг миниатюр.

## 9. Контрольный список перед публикацией

- [ ] Разрядность DLL совпадает с Far Manager.
- [ ] Присутствуют четыре обязательных экспорта v1.
- [ ] Каждый экспорт использует C linkage и `RIDE_CODEC_CALL`.
- [ ] Исключения C++ не пересекают границу ABI.
- [ ] Строки GetInfo — корректные UTF-8 и завершены нулём.
- [ ] Probe ограничена переданным буфером и безопасно отклоняет повреждённые префиксы.
- [ ] Decode возвращает top-down RGBA8 с прямой альфой.
- [ ] Все размеры и смещения проверяются на переполнение.
- [ ] FreeImage безопасна после успешного и неудачного Decode.
- [ ] Capabilities соответствуют реально поддерживаемым функциям и форматам пикселей.
- [ ] Стабильные ID кодека и параметров не меняются случайно между релизами.
- [ ] Закодированные данные освобождаются только через ту же DLL кодека.
- [ ] Тесты охватывают корректные, некорректные, усечённые и реальные файлы.
- [ ] Все внешние зависимости и лицензионные уведомления включены.
