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

/* ABI v1: эти определения должны оставаться совместимыми с Viewer 1.x. */
#define RIDE_CODEC_API_VERSION 1u
#define RIDE_CODEC_NAME_CAPACITY 64u
#define RIDE_CODEC_EXTENSIONS_CAPACITY 128u

typedef struct RideCodecInfo {
    uint32_t struct_size;
    uint32_t api_version;
    char name[RIDE_CODEC_NAME_CAPACITY];
    char extensions[RIDE_CODEC_EXTENSIONS_CAPACITY];
} RideCodecInfo;

typedef struct RideImage {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format; /* 1 = RGBA8, строки сверху вниз */
    uint8_t* pixels;
    size_t data_size;
} RideImage;

typedef struct RideCodecError {
    uint32_t code;
    size_t byte_offset;
    char message[160];
} RideCodecError;

/* Optional per-image source metadata; RideImage remains normalized RGBA8. */
#define RIDE_CODEC_IMAGE_METADATA_VERSION 1u
#define RIDE_CODEC_FORMAT_NAME_CAPACITY 64u
#define RIDE_CODEC_PIXEL_FORMAT_CAPACITY 64u

enum RideImageMetadataFlags {
    RIDE_CODEC_IMAGE_META_INDEXED   = 1u << 0,
    RIDE_CODEC_IMAGE_META_GRAYSCALE = 1u << 1,
    RIDE_CODEC_IMAGE_META_ALPHA     = 1u << 2,
    RIDE_CODEC_IMAGE_META_PLANAR    = 1u << 3
};

typedef struct RideImageMetadata {
    uint32_t struct_size;
    uint32_t version;
    char format_name[RIDE_CODEC_FORMAT_NAME_CAPACITY];
    char pixel_format[RIDE_CODEC_PIXEL_FORMAT_CAPACITY];
    uint32_t bits_per_pixel;
    uint32_t palette_entries;
    uint32_t flags;
    uint32_t reserved[4];
} RideImageMetadata;

#define RIDE_CODEC_ID_CAPACITY 64u

/* Optional stable identifier used for codec-specific settings and export profiles.
   IDs are lowercase ASCII: [a-z][a-z0-9_-]*. */
typedef uint32_t (RIDE_CODEC_CALL *RideCodecGetIdFn)(char*, uint32_t);

typedef uint32_t (RIDE_CODEC_CALL *RideCodecGetInfoFn)(RideCodecInfo*);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecProbeFn)(const uint8_t*, size_t);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecDecodeFn)(
    const uint8_t*, size_t, RideImage*, RideCodecError*);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecDecodeExFn)(
    const uint8_t*, size_t, RideImage*, RideImageMetadata*, RideCodecError*);
typedef void (RIDE_CODEC_CALL *RideCodecFreeImageFn)(RideImage*);

/* ABI v2: дополнительные структуры и функции необязательны для старых DLL. */
#define RIDE_CODEC_CAPABILITIES_VERSION 2u

enum RideCodecCapabilityFlags {
    RIDE_CODEC_CAP_DECODE = 1u << 0,
    RIDE_CODEC_CAP_ENCODE = 1u << 1,
    RIDE_CODEC_CAP_RGB    = 1u << 2,
    RIDE_CODEC_CAP_RGBA   = 1u << 3,
    RIDE_CODEC_CAP_ALPHA  = 1u << 4
};

enum RideCodecPixelFormats {
    RIDE_CODEC_PIXEL_RGBA8 = 1u << 0
};

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

/* Legacy Encode takes RGBA8 top-down. channels=3 writes RGB, channels=4 writes RGBA.
   Keep this structure and export unchanged. */
typedef struct RideCodecEncodeOptions {
    uint32_t struct_size;
    uint32_t channels;
    uint32_t colorspace; /* 0 = sRGB, 1 = linear */
    uint32_t reserved[5];
} RideCodecEncodeOptions;

typedef struct RideCodecEncodedData {
    uint32_t struct_size;
    uint8_t* data;
    size_t data_size;
} RideCodecEncodedData;

/* Optional generic Encode interface (ABI extension version 1). EncodeEx also takes
   RGBA8 top-down. Option and parameter IDs are stable lowercase ASCII identifiers;
   labels are UTF-8 presentation text. For CHOICE, both the numeric value and
   symbolic value ID are stable codec-defined identifiers. Missing request
   parameters use the schema defaults. */
#define RIDE_CODEC_ENCODE_OPTIONS_VERSION 1u
#define RIDE_CODEC_ENCODE_MAX_OPTIONS 16u
#define RIDE_CODEC_ENCODE_MAX_VALUES 16u
#define RIDE_CODEC_ENCODE_ID_CAPACITY 32u
#define RIDE_CODEC_ENCODE_LABEL_CAPACITY 64u

enum RideCodecEncodeOptionTypes {
    RIDE_CODEC_ENCODE_OPTION_CHOICE   = 1u,
    RIDE_CODEC_ENCODE_OPTION_INTEGER  = 2u,
    RIDE_CODEC_ENCODE_OPTION_BOOLEAN  = 3u
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
    uint32_t flags; /* Reserved; must be zero in version 1. */
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


typedef uint32_t (RIDE_CODEC_CALL *RideCodecGetCapabilitiesFn)(
    RideCodecCapabilities*);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecEncodeFn)(
    const RideImage*, const RideCodecEncodeOptions*,
    RideCodecEncodedData*, RideCodecError*);
typedef void (RIDE_CODEC_CALL *RideCodecFreeEncodedDataFn)(
    RideCodecEncodedData*);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecGetEncodeOptionsFn)(
    RideCodecEncodeOptionsSchema*);
typedef uint32_t (RIDE_CODEC_CALL *RideCodecEncodeExFn)(
    const RideImage*, const RideCodecEncodeRequest*,
    RideCodecEncodedData*, RideCodecError*);

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetInfo(RideCodecInfo*);
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecProbe(
    const uint8_t*, size_t);
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t*, size_t, RideImage*, RideCodecError*);
/* Optional extension; older codec DLLs need not export this function. */
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecodeEx(
    const uint8_t*, size_t, RideImage*, RideImageMetadata*, RideCodecError*);
RIDE_CODEC_EXPORT void RIDE_CODEC_CALL RideCodecFreeImage(RideImage*);

/* Optional exports. Older modules are not required to provide them. */
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetId(char*, uint32_t);
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities*);
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecEncode(
    const RideImage*, const RideCodecEncodeOptions*,
    RideCodecEncodedData*, RideCodecError*);
RIDE_CODEC_EXPORT void RIDE_CODEC_CALL RideCodecFreeEncodedData(
    RideCodecEncodedData*);
/* These two exports must be provided together when the generic interface is used. */
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetEncodeOptions(
    RideCodecEncodeOptionsSchema*);
RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecEncodeEx(
    const RideImage*, const RideCodecEncodeRequest*,
    RideCodecEncodedData*, RideCodecError*);

#ifdef __cplusplus
}
#endif
