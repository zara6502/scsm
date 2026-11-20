// RideCodec — C ABI над reference-реализацией формата RIDE
// (Run · Index · Delta · Escape, https://github.com/zara6502, проект rideent).
//
// Контракт для хостов (плагины Paint.NET и прочие):
//
//   1. ПАМЯТЬ. Все выходные буферы, возвращаемые этой DLL, выделены
//      malloc'ом ВНУТРИ RideCodec.dll. Хост обязан освобождать их только
//      через RideCodecFree(). Освобождение напрямую через free() хоста
//      не допускается (разные CRT).
//
//   2. ФОРМАТ ПИКСЕЛЕЙ. Вход и выход кодека — BGRA-буферы: 4 байта на
//      пиксель в порядке B, G, R, A, строки сверху вниз, без выравнивания
//      строк. Альфа-канал: при кодировании игнорируется (формат RIDE
//      v1 не хранит alpha); при декодировании A всегда 255.
//
//   3. ПОТОКОБЕЗОПАСНОСТЬ. Все функции независимы и могут вызываться
//      из разных потоков одновременно. Внутренние потоки кодека
//      управляются опцией `threads` (0 = hardware concurrency).
//
//   4. ВЕРСИОНИРОВАНИЕ. Структуры опций начинаются с поля struct_size
//      (sizeof структуры той версии, которую собрал хост). Кодек читает
//      только поля, покрываемые struct_size, — добавление полей в конец
//      структуры обратно совместимо.
//
//   5. ОШИБКИ. Любая функция возвращает 0 при успехе и заполняет
//      RideError (код + UTF-8 сообщение) при неудаче. Коды ошибок:
//      0 ok, 1 invalid argument, 2 malformed input (не RIDE / битые данные),
//      3 image too large, 4 out of memory, 5 internal error.
//
//   6. ВЕРСИЯ КОДЕКА. RideCodecVersion() возвращает (major<<16 | minor).
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(RIDE_CODEC_BUILD)
#    define RIDE_CODEC_EXPORT __declspec(dllexport)
#  else
#    define RIDE_CODEC_EXPORT
#  endif
#  define RIDE_CODEC_CALL __cdecl
#else
#  define RIDE_CODEC_EXPORT
#  define RIDE_CODEC_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Версия кодека: (major << 16) | minor.
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecVersion(void);

// Выбор профиля кодирования (значения совпадают с ride::ProfileChoice).
typedef enum RideProfileChoice {
    RIDE_PROFILE_AUTO = 0,     // INDEXED для <=256 цветов, иначе меньший из RGB/INDEXED
    RIDE_PROFILE_RGB = 1,      // форсировать RGB (24 бита)
    RIDE_PROFILE_INDEXED = 2,  // форсировать INDEXED (палитра <=256 цветов)
    RIDE_PROFILE_QOI = 5       // канонические QOI-операции внутри RIDE-контейнера
} RideProfileChoice;

// Опциональный rANS-слой поверх потока операций.
typedef enum RideEntropyMode {
    RIDE_ENTROPY_OFF = 0,  // никогда
    RIDE_ENTROPY_AUTO = 1, // включать, когда экономит >= 2% блочных данных
    RIDE_ENTROPY_ON = 2    // всегда
} RideEntropyMode;

// Опции кодирования. Все нулевые значения числовых полей = значения
// по умолчанию reference-реализации (см. ride::EncodeOptions).
typedef struct RideEncodeOptions {
    uint32_t struct_size;      // sizeof(RideEncodeOptions)
    int32_t profile_choice;    // RideProfileChoice
    int32_t entropy;           // RideEntropyMode
    int32_t linear;            // 1 = сохранить флаг kFlagLinear (линейные цвета)
    uint32_t block_pixels;     // 0 = 262144 (пикселей на блок)
    uint32_t block_rows;       // 0 = определять по block_pixels
    int32_t try_both_parses;   // 1 = пробовать оба COPY-парса (лучшее сжатие)
    uint32_t threads;          // 0 = hardware concurrency
    // v2 (квантование для профиля INDEXED; v1-хосты, передавшие короткий
    // struct_size, получают поведение без квантования):
    uint32_t quantize_colors;  // 0 = не квантовать; 1..256 — построить
                               //   палитру до N цветов, когда уникальных
                               //   цветов больше N (Median Cut в Oklab +
                               //   k-means ΔE); при AUTO участвует в
                               //   конкурсе «кто меньше»
    int32_t dither;            // 0 = нет; 1 = Floyd–Steinberg (только при
                               //   фактическом квантовании)
    // v3 (для чёрно-белого дизеринга):
    int32_t bw_palette;        // 1 = при quantize_colors == 2 использовать
                               //   жёсткую палитру чёрный/белый вместо
                               //   кластеров изображения; 0 = из изображения
} RideEncodeOptions;

typedef struct RideDecodeOptions {
    uint32_t struct_size;      // sizeof(RideDecodeOptions)
    uint32_t threads;          // 0 = hardware concurrency
    uint64_t max_pixels;       // 0 = 1<<30 (отказ для слишком больших изображений)
} RideDecodeOptions;

typedef struct RideError {
    uint32_t code;             // 0 ok / 1..5 (см. пункт 5)
    uint32_t reserved;
    char message[160];         // UTF-8, завершён нулём
} RideError;

// Кодирование BGRA-буфера в полный RIDE-файл (заголовок + блоки [+ footer]).
//   bgra     — пиксели (B,G,R,A), w*h*4 байт, top-down
//   opts     — может быть NULL (значения по умолчанию)
//   out      — *out = malloc-буфер с RIDE-файлом, *out_size = размер.
//              Освобождать через RideCodecFree().
// Возвращает 0 при успехе.
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecEncode(
    const uint8_t* bgra, uint32_t w, uint32_t h,
    const RideEncodeOptions* opts,
    uint8_t** out, size_t* out_size,
    RideError* err);

// Декодирование RIDE-файла в BGRA-буфер (w*h*4 байт, A = 255).
//   data/size  — полный RIDE-файл
//   w, h       — *w/*h = размер декодированного изображения
//   out        — *out = malloc-буфер, освобождать через RideCodecFree()
// Поддержанные профили: RGB (0), INDEXED (2), QOI (5).
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size,
    uint32_t* w, uint32_t* h,
    const RideDecodeOptions* opts,
    uint8_t** out,
    RideError* err);

// Освобождение буферов, возвращённых RideCodecEncode/Decode.
RIDE_CODEC_EXPORT void RIDE_CODEC_CALL RideCodecFree(void* ptr);

#ifdef __cplusplus
} // extern "C"
#endif
