/* ChipDeck Plugin API — ABI v1 (design approved 2026-11-17)
 *
 * Два типа плагинов:
 *   1. ChipEngine  — декодер/эмулятор чипа: файл -> PCM-поток
 *   2. ChipLibrary — провайдер онлайн-библиотек: поиск/рейтинг/скачивание
 *
 * Решения v1: воспроизведение только после полного скачивания
 * (CHP_ENTRY_STREAMABLE зарезервирован), локальные файлы — средствами
 * host, host предпочитает float32, плейлист — отдельное окно host.
 *
 * Общие правила:
 *   - чистый C ABI, __cdecl (CHP_CALL), extern "C";
 *   - при сборке плагина определять CHP_BUILD -> __declspec(dllexport);
 *   - все структуры начинаются с uint32_t struct_size (версионирование);
 *   - строки UTF-8, завершённые нулём, в полях фиксированной длины;
 *   - исключения не пересекают границу ABI;
 *   - память освобождает тот, кто её выделил (Free* экспорты из той же DLL);
 *   - unknown flags/versions host маскирует; плагин объявляет только то,
 *     что реально реализует.
 */

#ifndef CHIPDECK_API_H
#define CHIPDECK_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(CHP_BUILD)
#    define CHP_EXPORT __declspec(dllexport)
#  else
#    define CHP_EXPORT
#  endif
#  define CHP_CALL __cdecl
#else
#  define CHP_EXPORT
#  define CHP_CALL
#endif

#define CHP_API_VERSION 1u

/* ---------- общие типы ---------- */

#ifdef __cplusplus
extern "C" {
#endif

typedef void* ChipHandle; /* непрозрачный дескриптор, владеет плагин */

typedef struct ChipError {
    uint32_t code;            /* 0 = ok; коды кодека самозаданные */
    uint32_t reserved;
    char message[160];        /* UTF-8 диагностическое сообщение */
} ChipError;

/* ---------- 1. Движок-декодер (ChipEngine) ---------- */

#define CHP_ENGINE_ID_CAPACITY 64u
#define CHP_ENGINE_NAME_CAPACITY 64u
#define CHP_ENGINE_EXTENSIONS_CAPACITY 128u

typedef struct ChipEngineInfo {
    uint32_t struct_size;
    uint32_t api_version;        /* = CHP_API_VERSION */
    char id[CHP_ENGINE_ID_CAPACITY];          /* стабильный: "gme" */
    char name[CHP_ENGINE_NAME_CAPACITY];      /* отображаемое: "GME" */
    char extensions[CHP_ENGINE_EXTENSIONS_CAPACITY]; /* "ay;sap;vgm" */
} ChipEngineInfo;

/* поля TrackInfo, которые движок реально знает */
enum ChipTrackInfoFlags {
    CHP_TRACK_HAS_TITLE   = 1u << 0,
    CHP_TRACK_HAS_AUTHOR  = 1u << 1,
    CHP_TRACK_HAS_DURATION = 1u << 2
};

typedef struct ChipTrackInfo {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t track_index;
    uint32_t reserved;
    char title[96];
    char author[96];
    char format[32];             /* "PT3", "SAP RMT", "SID PSID" */
    uint32_t duration_ms;        /* 0 если неизвестна (см. flags) */
} ChipTrackInfo;

enum ChipEngineCapsFlags {
    CHP_ENGINE_FLOAT32    = 1u << 0,  /* Render с float-буфером */
    CHP_ENGINE_INT16      = 1u << 1,  /* Render с int16-буфером  */
    CHP_ENGINE_VOICES     = 1u << 2,  /* GetVoiceLevels доступна */
    CHP_ENGINE_SPECTRUM   = 1u << 3,  /* GetSpectrum доступна    */
    CHP_ENGINE_SEEK       = 1u << 4   /* Seek реально реализована */
};

typedef struct ChipEngineCapabilities {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t voice_count;        /* для CHP_ENGINE_VOICES, напр. 3 у AY */
    uint32_t spectrum_bands;     /* число полос для CHP_ENGINE_SPECTRUM */
    uint32_t max_tracks;
    uint32_t reserved[4];
} ChipEngineCapabilities;

/* --- обязательные экспорты движка --- */

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info);

/* быстрое распознавание по байтам; не декодирует всё */
CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size);

/* открыть файл -> сессия (handle владеет плагин) */
CHP_EXPORT uint32_t CHP_CALL ChipEngineOpen(const uint8_t* data, size_t size,
                                            ChipHandle* out, ChipError* err);
CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle);

/* число подтюнов (>= 1) */
CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count);

/* выбрать трек и частоту; сбрасывает состояние рендера; после неё
   позиция = 0 */
CHP_EXPORT uint32_t CHP_CALL ChipEngineSelect(ChipHandle handle,
                                              uint32_t track_index,
                                              uint32_t sample_rate,
                                              ChipError* err);

/* pull-модель: host тянет сэмплы своим темпом; interleaved stereo.
   Возвращает число записанных кадров (frames). Значение меньше запрошенного
   = конец трека (остаток буфера должен быть заполнен тишиной).
   Вызывается host'ом только с одного потока (поток вывода звука). */
CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderFloat(ChipHandle handle,
                                                   float* buffer,
                                                   uint32_t frames);
CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderInt16(ChipHandle handle,
                                                   int16_t* buffer,
                                                   uint32_t frames);
/* движок реализует хотя бы одну из двух (объявляет в capabilities) */

/* --- опциональные экспорты движка --- */

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetCapabilities(
    ChipEngineCapabilities* caps);

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackInfo(ChipHandle handle,
                                                    uint32_t track_index,
                                                    ChipTrackInfo* info);

/* позиция в сэмплах от начала трека */
CHP_EXPORT uint32_t CHP_CALL ChipEngineSeek(ChipHandle handle,
                                            uint64_t sample_position,
                                            ChipError* err);

/* визуализации: уровни каналов 0..1 (voice_count значений) */
CHP_EXPORT uint32_t CHP_CALL ChipEngineGetVoiceLevels(ChipHandle handle,
                                                      float* levels,
                                                      uint32_t count);
/* спектр: spectrum_bands значений 0..1 */
CHP_EXPORT uint32_t CHP_CALL ChipEngineGetSpectrum(ChipHandle handle,
                                                   float* bands,
                                                   uint32_t count);

/* указатели функций для host (GetProcAddress -> reinterpret_cast) */

typedef uint32_t (CHP_CALL* ChipEngineGetInfoFn)(ChipEngineInfo*);
typedef uint32_t (CHP_CALL* ChipEngineProbeFn)(const uint8_t*, size_t);
typedef uint32_t (CHP_CALL* ChipEngineOpenFn)(const uint8_t*, size_t,
                                              ChipHandle*, ChipError*);
typedef void (CHP_CALL* ChipEngineCloseFn)(ChipHandle);
typedef uint32_t (CHP_CALL* ChipEngineGetTrackCountFn)(ChipHandle, uint32_t*);
typedef uint32_t (CHP_CALL* ChipEngineSelectFn)(ChipHandle, uint32_t,
                                                uint32_t, ChipError*);
typedef uint32_t (CHP_CALL* ChipEngineRenderFloatFn)(ChipHandle, float*,
                                                     uint32_t);
typedef uint32_t (CHP_CALL* ChipEngineRenderInt16Fn)(ChipHandle, int16_t*,
                                                     uint32_t);
typedef uint32_t (CHP_CALL* ChipEngineGetCapabilitiesFn)(
    ChipEngineCapabilities*);
typedef uint32_t (CHP_CALL* ChipEngineGetTrackInfoFn)(ChipHandle, uint32_t,
                                                      ChipTrackInfo*);
typedef uint32_t (CHP_CALL* ChipEngineSeekFn)(ChipHandle, uint64_t,
                                              ChipError*);
typedef uint32_t (CHP_CALL* ChipEngineGetVoiceLevelsFn)(ChipHandle, float*,
                                                        uint32_t);
typedef uint32_t (CHP_CALL* ChipEngineGetSpectrumFn)(ChipHandle, float*,
                                                     uint32_t);

/* ---------- 2. Провайдер библиотеки (ChipLibrary) ---------- */

#define CHP_LIB_ID_CAPACITY 64u
#define CHP_LIB_NAME_CAPACITY 64u
#define CHP_LIB_URL_CAPACITY 128u
#define CHP_LIB_ENTRY_ID_CAPACITY 96u
#define CHP_LIB_TITLE_CAPACITY 128u
#define CHP_LIB_AUTHOR_CAPACITY 96u
#define CHP_LIB_FORMAT_CAPACITY 32u
#define CHP_LIB_PARAM_ID_CAPACITY 32u
#define CHP_LIB_PARAM_VALUE_CAPACITY 256u

enum ChipLibFlags {
    CHP_LIB_SEARCH    = 1u << 0,
    CHP_LIB_TOP       = 1u << 1,   /* умеет выдавать «лучшее» */
    CHP_LIB_RATINGS   = 1u << 2,   /* у записей есть рейтинг */
    CHP_LIB_DOWNLOAD  = 1u << 3,
    CHP_LIB_RANDOM    = 1u << 4
};

typedef struct ChipLibInfo {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t flags;
    uint32_t reserved;
    char id[CHP_LIB_ID_CAPACITY];       /* стабильный: "modarchive" */
    char name[CHP_LIB_NAME_CAPACITY];
    char homepage[CHP_LIB_URL_CAPACITY];
} ChipLibInfo;

/* настройки подключения (API-ключи и т.п.); host рисует UI по схеме */
typedef struct ChipLibStringParam {
    char id[CHP_LIB_PARAM_ID_CAPACITY];        /* стабильный: "api_key" */
    char value[CHP_LIB_PARAM_VALUE_CAPACITY];
} ChipLibStringParam;

typedef struct ChipLibConnectConfig {
    uint32_t struct_size;
    uint32_t param_count;
    uint32_t reserved[2];
    const ChipLibStringParam* params;           /* массив count штук */
    const char* cache_dir;                      /* UTF-8 путь кеша host */
} ChipLibConnectConfig;

/* схема строковых параметров для диалога настроек host'а */
typedef struct ChipLibParamSchema {
    char id[CHP_LIB_PARAM_ID_CAPACITY];
    char label[CHP_LIB_PARAM_ID_CAPACITY * 2];
    char default_value[CHP_LIB_PARAM_VALUE_CAPACITY];
    uint32_t secret;         /* 1 = пароль/ключ: маскировать в UI */
    uint32_t required;
} ChipLibParamSchema;

typedef struct ChipLibConfigSchema {
    uint32_t struct_size;
    uint32_t version;                    /* = 1 */
    uint32_t param_count;
    uint32_t reserved;
    ChipLibParamSchema params[16];
} ChipLibConfigSchema;

/* запись каталога */
#define CHP_LIB_RATING_UNKNOWN 0xFFFFFFFFu

enum ChipEntryFlags {
    CHP_ENTRY_HAS_DURATION = 1u << 0,
    CHP_ENTRY_HAS_SIZE     = 1u << 1,
    CHP_ENTRY_STREAMABLE   = 1u << 2  /* может играть без полного скачивания */
};

typedef struct ChipLibEntry {
    uint32_t struct_size;
    uint32_t flags;
    char entry_id[CHP_LIB_ENTRY_ID_CAPACITY];  /* стабильный ID в рамках
                                                  библиотеки; для повторного
                                                  fetch / кеша host */
    char title[CHP_LIB_TITLE_CAPACITY];
    char author[CHP_LIB_AUTHOR_CAPACITY];
    char format[CHP_LIB_FORMAT_CAPACITY];
    uint32_t rating;               /* 0..1000, CHP_LIB_RATING_UNKNOWN */
    uint32_t duration_ms;          /* 0 если неизвестна */
    uint64_t size_bytes;
    uint64_t reserved;
} ChipLibEntry;

/* запрос каталога */
enum ChipLibQueryType {
    CHP_QUERY_SEARCH   = 1,   /* text: строка поиска */
    CHP_QUERY_TOP      = 2,   /* топ по рейтингу; text может быть пуст */
    CHP_QUERY_RANDOM   = 3,
    CHP_QUERY_BY_AUTHOR = 4   /* text: автор */
};

typedef struct ChipLibRequest {
    uint32_t struct_size;
    uint32_t query_type;           /* ChipLibQueryType */
    uint32_t limit;                /* максимум записей */
    uint32_t reserved;
    const char* text;              /* UTF-8, может быть NULL/пусто */
} ChipLibRequest;

/* колбэк результата: вернуть ненулевое = отменить дальнейшие результаты */
typedef uint32_t (CHP_CALL* ChipLibResultCb)(void* user_data,
                                             const ChipLibEntry* entry);
/* прогресс скачивания: done/total байт; total может быть 0 (неизвестен) */
typedef void (CHP_CALL* ChipLibProgressCb)(void* user_data,
                                           uint64_t done, uint64_t total);

/* --- экспорты библиотеки --- */

CHP_EXPORT uint32_t CHP_CALL ChipLibGetInfo(ChipLibInfo* info);

/* схема настроек; если экспорт отсутствует — плагин без настроек */
CHP_EXPORT uint32_t CHP_CALL ChipLibGetConfigSchema(ChipLibConfigSchema* schema);

/* connect по конфигурации host'а; плагин может проверять ключ сразу */
CHP_EXPORT uint32_t CHP_CALL ChipLibConnect(const ChipLibConnectConfig* config,
                                            ChipHandle* out, ChipError* err);
CHP_EXPORT void CHP_CALL ChipLibDisconnect(ChipHandle handle);

/* блокирующий запрос каталога; результаты отдаются по одному через cb.
   Плагин выполняет сеть в своих внутренних потоках, но сам вызов
   блокирует поток host'а до конца/отмены; cancel проверяется плагином. */
CHP_EXPORT uint32_t CHP_CALL ChipLibQuery(ChipHandle handle,
                                          const ChipLibRequest* request,
                                          ChipLibResultCb result_cb,
                                          void* user_data,
                                          volatile int* cancel,
                                          ChipError* err);

/* скачать файл трека -> буфер, выделенный ПЛАГИНОМ.
   host вызывает ChipLibFreeBuffer из той же DLL после копирования.
   cb может быть NULL (без прогресса). */
CHP_EXPORT uint32_t CHP_CALL ChipLibFetch(ChipHandle handle,
                                          const ChipLibEntry* entry,
                                          ChipLibProgressCb progress_cb,
                                          void* user_data,
                                          volatile int* cancel,
                                          uint8_t** data, size_t* size,
                                          ChipError* err);
CHP_EXPORT void CHP_CALL ChipLibFreeBuffer(ChipHandle handle, uint8_t* data);

typedef uint32_t (CHP_CALL* ChipLibGetInfoFn)(ChipLibInfo*);
typedef uint32_t (CHP_CALL* ChipLibGetConfigSchemaFn)(ChipLibConfigSchema*);
typedef uint32_t (CHP_CALL* ChipLibConnectFn)(const ChipLibConnectConfig*,
                                              ChipHandle*, ChipError*);
typedef void (CHP_CALL* ChipLibDisconnectFn)(ChipHandle);
typedef uint32_t (CHP_CALL* ChipLibQueryFn)(ChipHandle,
                                            const ChipLibRequest*,
                                            ChipLibResultCb, void*,
                                            volatile int*, ChipError*);
typedef uint32_t (CHP_CALL* ChipLibFetchFn)(ChipHandle, const ChipLibEntry*,
                                            ChipLibProgressCb, void*,
                                            volatile int*, uint8_t**,
                                            size_t*, ChipError*);
typedef void (CHP_CALL* ChipLibFreeBufferFn)(ChipHandle, uint8_t*);

/* коды ошибок по умолчанию (плагин может добавлять свои >= 16):
   1  invalid arg       2  not recognized     3  network
   4  auth (ключ/логин) 5  not found          6  cancelled
   7  out of memory     8  unsupported        */
#define CHP_ERR_INVALID_ARG 1u
#define CHP_ERR_NOT_RECOGNIZED 2u
#define CHP_ERR_NETWORK 3u
#define CHP_ERR_AUTH 4u
#define CHP_ERR_NOT_FOUND 5u
#define CHP_ERR_CANCELLED 6u
#define CHP_ERR_OUT_OF_MEMORY 7u
#define CHP_ERR_UNSUPPORTED 8u

#ifdef __cplusplus
}
#endif

#endif /* CHIPDECK_API_H */
