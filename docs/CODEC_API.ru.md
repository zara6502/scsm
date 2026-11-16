# Ride Codec API — ABI v1 и необязательные расширения ABI v2

[English version](CODEC_API.md)

Этот документ описывает контракт DLL-кодеков, используемый Far Ride Viewer 2: неизменённый базовый ABI v1 и необязательные добавочные расширения, которые принято называть ABI v2.

## 1. Версии и совместимость

Интерфейс расширяется без нарушения обратной совместимости:

- Обязательная часть C ABI остаётся **версией 1**. Значение `RIDE_CODEC_API_VERSION` по-прежнему равно `1u`.
- Существующие структуры и обязательные сигнатуры v1 менять нельзя.
- Необязательные расширения обнаруживаются по именам экспортируемых функций. Кодек может реализовать только необходимые ему расширения.
- DLL с четырьмя обязательными функциями v1 и без дополнительных экспортов остаётся корректным **декодером**.
- Структура возможностей имеет собственное поле версии. `RIDE_CODEC_CAPABILITIES_VERSION` равно `2u`; это не меняет `RIDE_CODEC_API_VERSION`.

Версии остальных расширений также независимы:

| Константа | Значение | Для чего используется |
|---|---:|---|
| `RIDE_CODEC_API_VERSION` | 1 | Обязательный базовый API кодека |
| `RIDE_CODEC_CAPABILITIES_VERSION` | 2 | `RideCodecCapabilities` |
| `RIDE_CODEC_IMAGE_METADATA_VERSION` | 1 | `RideImageMetadata` |
| `RIDE_CODEC_ENCODE_OPTIONS_VERSION` | 1 | Схема и запрос универсального Encode |

Используйте общий заголовок `include/ride_viewer_codec_api.h`. Не копируйте определения структур в проект кодека и не меняйте их расположение или размеры локально.

## 2. Платформенный ABI и экспорты DLL

Публичный интерфейс использует C-совместимые структуры и функции:

- Соглашение о вызовах Windows: `__cdecl`, задаётся через `RIDE_CODEC_CALL`.
- C linkage: экспортируемые функции должны быть объявлены внутри `extern "C"`.
- При сборке DLL-кодека определяйте `RIDE_CODEC_BUILD`; заголовок применит `__declspec(dllexport)`.
- Для ABI-полей используйте целочисленные типы фиксированной ширины. Для размеров буферов и смещений применяется `size_t`, поэтому DLL кодека должна иметь ту же разрядность, что и процесс host.
- Не передавайте через границу DLL C++ классы, STL-контейнеры, исключения, ссылки и типы, зависящие от C++ ABI компилятора.
- Перехватывайте все C++ исключения внутри экспортируемых функций. Исключение не должно пересекать границу C ABI.

Host и кодек должны быть собраны для совместимой среды Windows и одинаковой архитектуры процесса.

## 3. Обязательный контракт ABI v1

Каждая DLL-кодек обязана экспортировать четыре функции:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetInfo(RideCodecInfo* info);
uint32_t RIDE_CODEC_CALL RideCodecProbe(const uint8_t* data, size_t size);
uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size,
    RideImage* image, RideCodecError* error);
void RIDE_CODEC_CALL RideCodecFreeImage(RideImage* image);
```

### 3.1 `RideCodecInfo`

```cpp
typedef struct RideCodecInfo {
    uint32_t struct_size;
    uint32_t api_version;
    char name[64];
    char extensions[128];
} RideCodecInfo;
```

Вызывающая сторона инициализирует `struct_size` значением `sizeof(RideCodecInfo)`. Кодек должен проверить, что структура достаточно велика, установить `api_version = RIDE_CODEC_API_VERSION` и вернуть непустые строки UTF-8 с завершающим нулевым байтом.

- `name`: короткое отображаемое имя, например `QOI` или `PCX`.
- `extensions`: расширения через точку с запятой, без точек в начале, например `png;jpg;jpeg`.

Host проверяет строки и список расширений. Сравнение выполняется без учёта регистра. Совпадающее расширение влияет только на порядок вызова Probe; оно не доказывает, что файл действительно имеет этот формат.

### 3.2 `RideImage`

```cpp
typedef struct RideImage {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
    uint8_t* pixels;
    size_t data_size;
} RideImage;
```

Базовый контракт пикселей — **top-down RGBA8**:

- `pixel_format` должен быть равен `1` (бит возможности `RIDE_CODEC_PIXEL_RGBA8` обозначает этот формат).
- Каждый пиксель содержит четыре 8-битных канала в порядке R, G, B, A.
- Первая строка соответствует верхней части изображения.
- Альфа-канал прямой, не premultiplied.
- Для полностью непрозрачных пикселей alpha должен быть равен 255.
- `stride` — расстояние в байтах между началами строк; оно должно быть не меньше `width * 4`.
- Читаемая область буфера должна содержать как минимум `stride * (height - 1) + width * 4` байт.
- Ширина и высота должны быть ненулевыми.

Перед выделением памяти и обращением к буферу проверяйте переполнение при всех умножениях и сложениях. Установите разумные ограничения на размеры изображения и количество пикселей. До копирования изображения host проверяет структуру результата, размеры, формат пикселей, stride и минимальный размер буфера.

Перед вызовом Decode host устанавливает `struct_size`. Кодек должен сохранить переданное значение при очистке или заполнении выходной структуры.

### 3.3 `RideCodecError`

```cpp
typedef struct RideCodecError {
    uint32_t code;
    size_t byte_offset;
    char message[160];
} RideCodecError;
```

- `code` — ненулевой код ошибки, определяемый самим кодеком.
- `byte_offset` — смещение в исходном файле, если оно известно; иначе 0.
- `message` — короткое диагностическое сообщение UTF-8 с завершающим нулём.

Общего перечисления кодов ошибок нет. При необходимости документируйте коды конкретного кодека отдельно.

### 3.4 Поведение обязательных функций

**GetInfo**

- Возвращает ненулевое значение при успехе и ноль при ошибке.
- Отклоняет null-указатель и слишком маленькую структуру.
- Заполняет версию API и обе строки с завершающими нулями.
- Не возвращает пустое имя или список расширений.

**Probe**

- Возвращает ненулевое значение, если переданные байты распознаны, иначе ноль.
- Не читает за пределами диапазона из `size` байт.
- Безопасно обрабатывает null/пустой ввод, короткие и усечённые префиксы.
- Не доверяет расширению файла; Probe получает только байты.
- Должна быть быстрой. Она проверяет достаточную структуру для предотвращения типичных ложных совпадений, но не декодирует всё изображение.

**Decode**

- Получает полный буфер файла.
- При успехе возвращает корректный `RideImage` top-down RGBA8.
- При ошибке возвращает ноль, заполняет `RideCodecError`, если он передан, и оставляет изображение пустым и безопасным для освобождения.
- Сохраняет переданное вызывающей стороной значение `struct_size`.
- Проверяет размеры, объёмы буферов, смещения и арифметику до выделения памяти или чтения.

**FreeImage**

- Освобождает только память, выделенную этой DLL-кодеком.
- Безопасно принимает null, пустую и частично инициализированную структуру.
- Вызывается host после Decode, в том числе после неудачного Decode.
- После освобождения очищает указатель и поля размера.

Host удерживает модуль кодека загруженным, пока использует декодированное изображение, после чего вызывает его собственную функцию `RideCodecFreeImage`. Нельзя выделять память в одной DLL, а освобождать в другой.

## 4. Необязательный стабильный ID кодека

Необязательный экспорт:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetId(char* output, uint32_t capacity);
```

Идентификатор используется в настройках и профилях экспорта. Он должен оставаться неизменным между версиями кодека и быть уникальным среди загруженных модулей.

Правила:

- Только строчные ASCII-символы.
- Первый символ: `a`–`z`.
- Остальные символы: `a`–`z`, `0`–`9`, подчёркивание или дефис.
- Максимальный размер поля: `RIDE_CODEC_ID_CAPACITY` (64 байта, включая завершающий нулевой байт).
- При успехе возвращает ненулевое значение и всегда завершает строку нулём.

Если экспорт отсутствует, host строит резервный ID из имени DLL без расширения. Он подходит для загрузки, но менее стабилен, чем явно объявленный ID. При совпадении ID позднее загружаемый кодек отклоняется.

## 5. Необязательные метаданные конкретного изображения

Необязательный экспорт:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecDecodeEx(
    const uint8_t* data, size_t size,
    RideImage* image,
    RideImageMetadata* metadata,
    RideCodecError* error);
```

Если экспорт присутствует, в данном вызове он заменяет Decode. Он возвращает тот же нормализованный `RideImage` top-down RGBA8; метаданные описывают исходный файл, а не другой выходной пиксельный буфер.

```cpp
#define RIDE_CODEC_IMAGE_METADATA_VERSION 1u

typedef struct RideImageMetadata {
    uint32_t struct_size;
    uint32_t version;
    char format_name[64];
    char pixel_format[64];
    uint32_t bits_per_pixel;
    uint32_t palette_entries;
    uint32_t flags;
    uint32_t reserved[4];
} RideImageMetadata;
```

Перед вызовом инициализируйте `struct_size` и `version`. При успехе установите версию и заполните известные поля:

- `format_name`: имя исходного формата, например `PCX`.
- `pixel_format`: исходное представление, например `Indexed`, `RGB` или `RGBA`.
- `bits_per_pixel`: разрядность исходного изображения, если известна.
- `palette_entries`: количество элементов палитры, если применимо.
- `flags`: ноль или комбинация следующих битов.

| Флаг | Значение | Назначение |
|---|---:|---|
| `RIDE_CODEC_IMAGE_META_INDEXED` | `1 << 0` | Исходник использует индексы палитры |
| `RIDE_CODEC_IMAGE_META_GRAYSCALE` | `1 << 1` | Исходник является grayscale |
| `RIDE_CODEC_IMAGE_META_ALPHA` | `1 << 2` | Исходник содержит alpha |
| `RIDE_CODEC_IMAGE_META_PLANAR` | `1 << 3` | Исходные данные организованы по плоскостям |

Все строки должны быть UTF-8 и завершаться нулём. Зарезервированные поля обнуляйте. Метаданные носят справочный характер: текущий host игнорирует некорректные или неподдерживаемые необязательные метаданные, не отклоняя корректно декодированное изображение. Кодек без `DecodeEx` продолжает работать через `Decode`.

## 6. Получение возможностей кодека

Необязательный экспорт:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities* capabilities);
```

Вызывающая сторона инициализирует `struct_size`. Кодек заполняет структуру:

```cpp
#define RIDE_CODEC_CAPABILITIES_VERSION 2u

typedef struct RideCodecCapabilities {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t input_pixel_formats;
    uint32_t output_pixel_formats;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t reserved[8];
} RideCodecCapabilities;
```

Установите `version = RIDE_CODEC_CAPABILITIES_VERSION`. Поля означают:

- `flags`: поддерживаемые операции и возможности форматов.
- `input_pixel_formats`: форматы пикселей, принимаемые Encode.
- `output_pixel_formats`: форматы пикселей, выдаваемые Decode.
- `max_width`, `max_height`: объявленные ограничения размеров; ноль означает, что это поле не задаёт дополнительного ограничения.
- `reserved`: резерв для будущего расширения; инициализируйте нулём.

Известные биты flags:

| Флаг | Значение | Назначение |
|---|---:|---|
| `RIDE_CODEC_CAP_DECODE` | `1 << 0` | Поддерживается Decode |
| `RIDE_CODEC_CAP_ENCODE` | `1 << 1` | Поддерживается Encode |
| `RIDE_CODEC_CAP_RGB` | `1 << 2` | Возможность RGB |
| `RIDE_CODEC_CAP_RGBA` | `1 << 3` | Возможность RGBA |
| `RIDE_CODEC_CAP_ALPHA` | `1 << 4` | Поддерживается alpha |

Известный бит формата пикселей:

| Формат | Значение | Назначение |
|---|---:|---|
| `RIDE_CODEC_PIXEL_RGBA8` | `1 << 0` | Буфер RGBA8 со строками сверху вниз |

Для текущего host действуют правила:

- Если кодек экспортирует `GetCapabilities`, он должен вернуть версию структуры и размер, покрывающий все определённые поля.
- Поддержка Decode и выход RGBA8 обязательны.
- Сочетание `ALPHA` без `RGBA` противоречиво и отклоняется.
- Если объявлен `ENCODE`, требуются вход RGBA8, полный интерфейс освобождения закодированных данных и хотя бы одна функция кодирования.
- Общая схема Encode и `EncodeEx` должны присутствовать одновременно.
- Неизвестные биты возможностей текущий загрузчик маскирует; не следует рассчитывать на их сохранение.

Если `GetCapabilities` отсутствует, host регистрирует кодек как legacy-модуль только для декодирования с базовым контрактом выходного RGBA8. Необязательные символы `GetId` и `DecodeEx` при этом разрешаются независимо.

## 7. Необязательные интерфейсы кодирования

Оба интерфейса принимают `RideImage` со входными пикселями top-down RGBA8 и возвращают буфер `RideCodecEncodedData`, принадлежащий модулю кодека.

```cpp
typedef struct RideCodecEncodedData {
    uint32_t struct_size;
    uint8_t* data;
    size_t data_size;
} RideCodecEncodedData;
```

Перед вызовом инициализируйте `struct_size`. При успехе `data` указывает на байты закодированного файла, а `data_size` содержит точную длину. При ошибке верните ноль, по возможности заполните ошибку и оставьте выходную структуру пустой.

Необязательная функция освобождения:

```cpp
void RIDE_CODEC_CALL RideCodecFreeEncodedData(
    RideCodecEncodedData* encoded);
```

Host вызывает эту функцию из той же DLL, которая выделила закодированный буфер. Функция должна безопасно принимать null и пустые структуры, а после освобождения очистить указатель и размер. Не используйте аллокатор host или другой DLL для освобождения памяти кодека.

### 7.1 Legacy Encode

Необязательный экспорт:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecEncode(
    const RideImage* image,
    const RideCodecEncodeOptions* options,
    RideCodecEncodedData* output,
    RideCodecError* error);
```

```cpp
typedef struct RideCodecEncodeOptions {
    uint32_t struct_size;
    uint32_t channels;
    uint32_t colorspace;
    uint32_t reserved[5];
} RideCodecEncodeOptions;
```

Общие поля:

- `channels`: выбор каналов, конкретные допустимые значения определяются кодеком. QOI поддерживает 0 (Auto), 3 (RGB) и 4 (RGBA).
- `colorspace`: 0 (sRGB) или 1 (linear), если поддерживается кодеком.
- `reserved`: заполняются нулями.

У этого интерфейса фиксированный набор общих параметров, без отдельной описываемой схемы. Он сохраняется для совместимости и специализированных вызовов.

### 7.2 Универсальный EncodeEx и схема параметров

Для кодирования с настройками кодек предоставляет оба экспорта:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetEncodeOptions(
    RideCodecEncodeOptionsSchema* schema);

uint32_t RIDE_CODEC_CALL RideCodecEncodeEx(
    const RideImage* image,
    const RideCodecEncodeRequest* request,
    RideCodecEncodedData* output,
    RideCodecError* error);
```

Эти два экспорта должны присутствовать одновременно.

Схема описывает элементы управления, которые показывает host. Запрос передаёт выбранные значения обратно кодеку. Labels предназначены для отображения; стабильный контракт строится на ID и числовых значениях.

```cpp
#define RIDE_CODEC_ENCODE_OPTIONS_VERSION 1u
#define RIDE_CODEC_ENCODE_MAX_OPTIONS 16u
#define RIDE_CODEC_ENCODE_MAX_VALUES 16u
#define RIDE_CODEC_ENCODE_ID_CAPACITY 32u
#define RIDE_CODEC_ENCODE_LABEL_CAPACITY 64u

enum RideCodecEncodeOptionTypes {
    RIDE_CODEC_ENCODE_OPTION_CHOICE  = 1u,
    RIDE_CODEC_ENCODE_OPTION_INTEGER = 2u,
    RIDE_CODEC_ENCODE_OPTION_BOOLEAN = 3u
};

typedef struct RideCodecEncodeOptionValue {
    int64_t value;
    char id[RIDE_CODEC_ENCODE_ID_CAPACITY];
    char label[RIDE_CODEC_ENCODE_LABEL_CAPACITY];
} RideCodecEncodeOptionValue;

typedef struct RideCodecEncodeOption {
    char id[RIDE_CODEC_ENCODE_ID_CAPACITY];
    char label[RIDE_CODEC_ENCODE_LABEL_CAPACITY];
    uint32_t type;
    uint32_t flags;
    int64_t default_value;
    int64_t min_value;
    int64_t max_value;
    int64_t step;
    uint32_t value_count;
    uint32_t reserved;
    RideCodecEncodeOptionValue values[RIDE_CODEC_ENCODE_MAX_VALUES];
} RideCodecEncodeOption;

typedef struct RideCodecEncodeOptionsSchema {
    uint32_t struct_size;
    uint32_t version;
    uint32_t option_count;
    uint32_t reserved[5];
    RideCodecEncodeOption options[RIDE_CODEC_ENCODE_MAX_OPTIONS];
} RideCodecEncodeOptionsSchema;

typedef struct RideCodecEncodeParameter {
    char id[RIDE_CODEC_ENCODE_ID_CAPACITY];
    int64_t value;
} RideCodecEncodeParameter;

typedef struct RideCodecEncodeRequest {
    uint32_t struct_size;
    uint32_t version;
    uint32_t parameter_count;
    uint32_t reserved[5];
    RideCodecEncodeParameter parameters[RIDE_CODEC_ENCODE_MAX_OPTIONS];
} RideCodecEncodeRequest;
```

Общие правила схемы, которые проверяет текущий загрузчик:

- `struct_size` должен покрывать всю структуру схемы.
- `version` должен быть равен `RIDE_CODEC_ENCODE_OPTIONS_VERSION`.
- `option_count` не может превышать 16.
- Все резервные поля и флаги уровня схемы и параметров должны быть равны нулю.
- ID параметров должны быть уникальными стабильными идентификаторами с завершающим нулём. Первый символ — строчная ASCII-буква `a`–`z`; последующие могут быть строчными буквами, цифрами, подчёркиванием или дефисом.
- Labels параметров и вариантов должны быть непустыми UTF-8 строками с завершающим нулём.
- Все ID должны помещаться в поля фиксированного размера.

#### CHOICE

- `value_count` должен быть от 1 до 16.
- Каждый элемент содержит стабильное числовое `value`, стабильный символьный `id` и отображаемый `label`.
- Числовые значения и символьные ID должны быть уникальны внутри параметра.
- `default_value` должен совпадать с одним из объявленных числовых значений.
- В запрос записывается выбранное числовое значение. Символьный ID остаётся частью контракта схемы.

#### INTEGER

- `value_count` должен быть равен нулю.
- `min_value <= max_value`.
- `step > 0`.
- Значение по умолчанию должно находиться в диапазоне и лежать на последовательности, начинающейся с `min_value`: `(default_value - min_value) % step == 0`.
- Host проверяет введённое значение по тому же диапазону и шагу.

#### BOOLEAN

- `value_count` должен быть равен нулю.
- `default_value` должен быть 0 или 1.
- Значение в запросе: 0 (false) или 1 (true).

Host отклоняет некорректную схему и не регистрирует такой универсальный кодек. Сохраняйте ID параметров и вариантов неизменными между версиями кодека. Отображаемые подписи можно переводить и менять без изменения сохранённых настроек.

### 7.3 Поведение Encode-запроса

Host инициализирует `RideCodecEncodeRequest` размером структуры и версией схемы. Каждый параметр содержит ID и числовое значение.

Кодеку рекомендуется:

- Проверять заголовок запроса, версию, количество параметров, резервные поля и завершающие нули ID.
- Отклонять неизвестные и повторяющиеся ID.
- Проверять каждое значение по определению соответствующего параметра.
- Использовать значение по умолчанию из схемы для параметра, который отсутствует в запросе, как требует контракт универсального интерфейса.
- До начала работы очистить выходную структуру, проверить компоновку входного изображения и лимиты, исключить переполнение целых чисел.
- Возвращать ненулевое значение только при наличии полного закодированного буфера.

Профили и Save As в Viewer 2 передают полный набор параметров в порядке схемы и повторно проверяют сохранённые значения по текущей схеме перед кодированием.

## 8. Загрузка кодеков и правила работы

Host просматривает каталог `Codecs` рядом с DLL плагина и пытается загрузить каждую DLL.

1. Должны присутствовать четыре обязательных экспорта v1.
2. `GetInfo` должен вернуть корректные метаданные и базовую версию API.
3. Расширения разбираются и нормализуются. Совпадающее расширение влияет только на порядок вызова Probe.
4. При наличии используется `GetId`; иначе ID строится из имени DLL. Повторяющиеся ID отклоняются.
5. При наличии вызывается и проверяется `GetCapabilities`.
6. Необязательные интерфейсы Encode принимаются только тогда, когда они полны и согласуются с объявленными возможностями.
7. После чтения полного файла host вызывает Probe с полным буфером. Декодирование начинается только после успешного распознавания.
8. Возвращённое изображение проверяется и копируется в память host; после этого вызывается `FreeImage` кодека.

DLL кодека остаётся загруженной, пока host хранит ссылку на кодек или изображение, выделенное этим модулем. После добавления, удаления или замены DLL перезапустите Far Manager: реестр кодеков кэшируется на время сеанса.

## 9. Минимальный пример объявлений DLL

Кодек должен подключать общий заголовок и экспортировать функции с C linkage. Ниже приведён только шаблон объявлений; декодер, проверки, выделение памяти и обработка ошибок реализуются отдельно.

```cpp
#include "ride_viewer_codec_api.h"

extern "C" {

uint32_t RIDE_CODEC_CALL RideCodecGetInfo(RideCodecInfo* info);
uint32_t RIDE_CODEC_CALL RideCodecProbe(const uint8_t* data, size_t size);
uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size,
    RideImage* image, RideCodecError* error);
void RIDE_CODEC_CALL RideCodecFreeImage(RideImage* image);

/* Необязательные экспорты ABI v2. */
uint32_t RIDE_CODEC_CALL RideCodecGetId(char* output, uint32_t capacity);
uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities* capabilities);
uint32_t RIDE_CODEC_CALL RideCodecDecodeEx(
    const uint8_t* data, size_t size,
    RideImage* image, RideImageMetadata* metadata,
    RideCodecError* error);
void RIDE_CODEC_CALL RideCodecFreeEncodedData(
    RideCodecEncodedData* encoded);
uint32_t RIDE_CODEC_CALL RideCodecGetEncodeOptions(
    RideCodecEncodeOptionsSchema* schema);
uint32_t RIDE_CODEC_CALL RideCodecEncodeEx(
    const RideImage* image, const RideCodecEncodeRequest* request,
    RideCodecEncodedData* output, RideCodecError* error);

}
```

Полные сигнатуры берите из общего заголовка. Необязательную функцию не следует экспортировать как заглушку, если кодек не может реально выполнить объявленную возможность.

## 10. Совместимость и миграция

При переносе кодека с v1:

1. Оставьте `RideCodecGetInfo`, `RideCodecProbe`, `RideCodecDecode` и `RideCodecFreeImage` без изменений.
2. Не меняйте существующие структуры v1, порядок и типы их полей.
3. Сначала убедитесь, что DLL загружается как декодер.
4. Добавьте `RideCodecGetId`, если нужна стабильная идентичность для настроек и профилей.
5. Добавьте `RideCodecDecodeEx`, если нужны метаданные исходного формата.
6. Добавьте `RideCodecGetCapabilities`, чтобы объявить реально реализованные возможности.
7. Для использования универсального экспорта Viewer 2 добавьте `RideCodecFreeEncodedData`, `RideCodecGetEncodeOptions` и `RideCodecEncodeEx`; объявите `RIDE_CODEC_CAP_ENCODE` и вход RGBA8.
8. Проверьте успешный и ошибочный ввод, повреждённые файлы и многократные циклы выделения/освобождения памяти.

Не меняйте `RIDE_CODEC_API_VERSION` на 2 для этих расширений. Для будущего несовместимого базового ABI потребуется отдельный проект версии и явная поддержка со стороны host.
