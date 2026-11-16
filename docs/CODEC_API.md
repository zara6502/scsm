# Ride Codec API — ABI v1 and optional ABI v2 extensions

[Русская версия](CODEC_API.ru.md)

This document is the normative implementation guide for the codec boundary used by Far Ride Viewer 2. It describes the unchanged base ABI v1 and the optional, additive extensions commonly called ABI v2.

## 1. Version model and compatibility

The interface is deliberately additive:

- The required C ABI remains **version 1**. `RIDE_CODEC_API_VERSION` is still `1u`.
- Existing v1 structures and required function signatures must not be changed.
- Optional extensions are discovered by exported symbol name. A codec can implement only the extensions it needs.
- A DLL that exports the four required v1 functions and no optional functions remains a valid **decode-only** codec.
- The capabilities structure has its own version field. `RIDE_CODEC_CAPABILITIES_VERSION` is `2u`; this does not change `RIDE_CODEC_API_VERSION`.

The other extension-specific versions are also independent:

| Constant | Value | Applies to |
|---|---:|---|
| `RIDE_CODEC_API_VERSION` | 1 | Required base codec API |
| `RIDE_CODEC_CAPABILITIES_VERSION` | 2 | `RideCodecCapabilities` |
| `RIDE_CODEC_IMAGE_METADATA_VERSION` | 1 | `RideImageMetadata` |
| `RIDE_CODEC_ENCODE_OPTIONS_VERSION` | 1 | Generic Encode schema and request |

Use the shared header `include/ride_viewer_codec_api.h`. Do not copy the structure definitions into a codec project or modify their layout locally.

## 2. Platform ABI and DLL exports

The public interface uses C-compatible structures and functions:

- Windows calling convention: `__cdecl`, through `RIDE_CODEC_CALL`.
- C linkage: exports must be declared inside `extern "C"`.
- When compiling a codec DLL, define `RIDE_CODEC_BUILD`; the header then applies `__declspec(dllexport)`.
- Use fixed-width integer types for ABI fields. The ABI intentionally uses `size_t` for buffer sizes and byte offsets, so the codec DLL must have the same architecture as the host process.
- Do not expose C++ classes, STL containers, exceptions, references, or compiler-specific C++ types across the DLL boundary.
- Catch all C++ exceptions inside exported functions. No C++ exception may cross the C ABI.

The host and codec must be built for compatible Windows runtimes and the same process architecture.

## 3. Required ABI v1 contract

Every codec DLL must export all four required functions:

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

The caller initializes `struct_size` to `sizeof(RideCodecInfo)`. The codec must validate that the supplied structure is large enough, set `api_version` to `RIDE_CODEC_API_VERSION`, and return non-empty, null-terminated UTF-8 strings.

- `name`: short display name, such as `QOI` or `PCX`.
- `extensions`: semicolon-separated extensions without leading dots, for example `png;jpg;jpeg`.

The host validates the strings and extension list. Matching is case-insensitive. An extension is only a probe-order hint; it is not proof that the file has that format.

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

The base pixel contract is **top-down RGBA8**:

- `pixel_format` must be `1` (`RIDE_CODEC_PIXEL_RGBA8` is the capability bit for this format).
- Each pixel has four 8-bit channels in R, G, B, A order.
- The first row is the top row of the image.
- Alpha is straight/non-premultiplied.
- Fully opaque pixels must have alpha 255.
- `stride` is the byte distance between row starts and must be at least `width * 4`.
- The readable buffer extent must cover at least `stride * (height - 1) + width * 4` bytes.
- Width and height must be non-zero.

Check all multiplication and addition for overflow before allocating or indexing. Set reasonable dimension and pixel-count limits. The host validates the returned structure, dimensions, pixel format, stride, and minimum buffer size before copying pixels.

The host initializes `struct_size` before calling Decode. A codec must preserve the caller-provided `struct_size` when clearing or filling an output structure.

### 3.3 `RideCodecError`

```cpp
typedef struct RideCodecError {
    uint32_t code;
    size_t byte_offset;
    char message[160];
} RideCodecError;
```

- `code` is a codec-defined non-zero error code.
- `byte_offset` is the relevant offset in the source file when known; otherwise use 0.
- `message` is a short, null-terminated UTF-8 diagnostic.

There is no global enumeration of error codes. Document each codec's own codes if callers need to interpret them.

### 3.4 Required function behavior

**GetInfo**

- Return non-zero on success and zero on failure.
- Reject null or undersized structures.
- Set the API version and both null-terminated strings.
- Do not return an empty name or extension list.

**Probe**

- Return non-zero when the provided bytes are recognized and zero otherwise.
- Never read outside the `size` byte range.
- Safely handle null/empty input and short or truncated prefixes.
- Do not trust the filename extension; Probe receives bytes only.
- Keep probing inexpensive. It should validate enough structure to avoid common false positives, not decode the full image.

**Decode**

- Receives the complete file buffer.
- On success, return a valid top-down RGBA8 `RideImage`.
- On failure, return zero, set `RideCodecError` when supplied, and leave the image empty and safe to free.
- Preserve the caller-provided `struct_size`.
- Check dimensions, buffer sizes, offsets, and arithmetic before allocating or reading.

**FreeImage**

- Frees only memory allocated by that codec DLL.
- Must safely accept null, empty, and partially initialized images.
- Is called by the host after Decode, including after a failed Decode.
- Clear pointer and size fields after releasing the buffer.

The host retains the codec module while it owns a decoded image, then calls that module's own `RideCodecFreeImage`. Never allocate in one DLL and free in another.

## 4. Optional stable codec identity

Optional export:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetId(char* output, uint32_t capacity);
```

The identifier is used by settings and export profiles. It must be stable across releases and unique among all loaded codecs.

Validation rules:

- ASCII lowercase identifier.
- First character: `a`–`z`.
- Remaining characters: `a`–`z`, `0`–`9`, underscore, or hyphen.
- Maximum storage capacity: `RIDE_CODEC_ID_CAPACITY` (64 bytes including the null terminator).
- Return non-zero on success and always terminate the output string.

If this export is absent, the host derives a fallback ID from the DLL filename stem. The fallback is suitable for loading, but it is less stable than an explicitly declared ID. Duplicate IDs cause the later codec to be rejected.

## 5. Optional per-image metadata

Optional export:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecDecodeEx(
    const uint8_t* data, size_t size,
    RideImage* image,
    RideImageMetadata* metadata,
    RideCodecError* error);
```

`DecodeEx` replaces `Decode` for that call when the export is present. It still returns the same normalized top-down RGBA8 `RideImage`; metadata describes the source file, not a different output pixel buffer.

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

Initialize `struct_size` and `version` before the call. On success, set the version and fill any fields that are known:

- `format_name`: source format name, such as `PCX`.
- `pixel_format`: source representation, such as `Indexed`, `RGB`, or `RGBA`.
- `bits_per_pixel`: source bits per pixel, if known.
- `palette_entries`: number of palette entries, if applicable.
- `flags`: zero or a combination of the following bits.

| Flag | Value | Meaning |
|---|---:|---|
| `RIDE_CODEC_IMAGE_META_INDEXED` | `1 << 0` | Source uses indexed/palette pixels |
| `RIDE_CODEC_IMAGE_META_GRAYSCALE` | `1 << 1` | Source is grayscale |
| `RIDE_CODEC_IMAGE_META_ALPHA` | `1 << 2` | Source contains alpha |
| `RIDE_CODEC_IMAGE_META_PLANAR` | `1 << 3` | Source data is planar |

All text must be null-terminated UTF-8. Clear reserved fields. Metadata is advisory: the current host ignores malformed or unsupported optional metadata without rejecting an otherwise valid decoded image. A codec without `DecodeEx` continues to work through `Decode`.

## 6. Capability discovery

Optional export:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities* capabilities);
```

The caller initializes `struct_size`. The codec fills:

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

Set `version = RIDE_CODEC_CAPABILITIES_VERSION`. The fields describe:

- `flags`: operations and pixel capabilities.
- `input_pixel_formats`: pixel formats accepted by Encode.
- `output_pixel_formats`: pixel formats produced by Decode.
- `max_width`, `max_height`: codec-declared dimension limits; zero means no additional limit is declared by that field.
- `reserved`: reserved for future use; initialize to zero.

Known flag bits:

| Flag | Value | Meaning |
|---|---:|---|
| `RIDE_CODEC_CAP_DECODE` | `1 << 0` | Decode supported |
| `RIDE_CODEC_CAP_ENCODE` | `1 << 1` | Encode supported |
| `RIDE_CODEC_CAP_RGB` | `1 << 2` | RGB encoding/format capability |
| `RIDE_CODEC_CAP_RGBA` | `1 << 3` | RGBA encoding/format capability |
| `RIDE_CODEC_CAP_ALPHA` | `1 << 4` | Alpha is supported |

Known pixel-format bit:

| Pixel format | Value | Meaning |
|---|---:|---|
| `RIDE_CODEC_PIXEL_RGBA8` | `1 << 0` | Top-down RGBA8 buffer |

For the current host:

- A codec that exports `GetCapabilities` must report the correct capabilities version and enough structure bytes to cover the defined fields.
- Decode support and RGBA8 output are required.
- `ALPHA` without `RGBA` is rejected as contradictory.
- When `ENCODE` is declared, RGBA8 input and the complete encoded-data release interface are required, along with at least one encoding function.
- The generic schema export and `EncodeEx` must be supplied together.
- Unknown capability bits are masked by the current loader; do not rely on unknown bits being preserved.

When `GetCapabilities` is absent, the host registers the codec as a legacy decode-only module with the base RGBA8 output contract. Optional `GetId` and `DecodeEx` symbols are resolved independently.

## 7. Optional encoding interfaces

Both encoding interfaces accept an input `RideImage` in top-down RGBA8 format and return a module-owned `RideCodecEncodedData` buffer.

```cpp
typedef struct RideCodecEncodedData {
    uint32_t struct_size;
    uint8_t* data;
    size_t data_size;
} RideCodecEncodedData;
```

Initialize `struct_size` before the call. On success, `data` points to the encoded file bytes and `data_size` contains the exact length. On failure, return zero, report an error if possible, and leave the output empty.

Optional release export:

```cpp
void RIDE_CODEC_CALL RideCodecFreeEncodedData(
    RideCodecEncodedData* encoded);
```

The host calls this function from the same DLL that allocated the encoded buffer. It must safely accept null and empty structures and clear the pointer and size after release. Do not use the host allocator or a different DLL to free codec-owned data.

### 7.1 Legacy Encode

Optional export:

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

The common fields are:

- `channels`: codec-defined channel selection. The QOI codec supports 0 (Auto), 3 (RGB), and 4 (RGBA).
- `colorspace`: 0 (sRGB) or 1 (linear), when supported by the codec.
- `reserved`: set to zero.

This interface has fixed common options and does not provide a discoverable schema. It remains available for compatibility and specialized callers.

### 7.2 Generic EncodeEx and option schema

For settings-driven encoding, the codec provides both exports:

```cpp
uint32_t RIDE_CODEC_CALL RideCodecGetEncodeOptions(
    RideCodecEncodeOptionsSchema* schema);

uint32_t RIDE_CODEC_CALL RideCodecEncodeEx(
    const RideImage* image,
    const RideCodecEncodeRequest* request,
    RideCodecEncodedData* output,
    RideCodecError* error);
```

The two exports must be present together.

The schema describes the controls shown by the host. The request passes the selected values back to the codec. Labels are presentation text; IDs and numeric values form the stable contract.

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

Common schema rules enforced by the current loader:

- `struct_size` must cover the complete schema.
- `version` must equal `RIDE_CODEC_ENCODE_OPTIONS_VERSION`.
- `option_count` may not exceed 16.
- All schema-level and option-level reserved fields and flags must be zero.
- Option IDs must be unique, null-terminated stable identifiers. The first character is lowercase ASCII `a`–`z`; following characters may be lowercase letters, digits, underscore, or hyphen.
- Option labels and choice labels must be non-empty, null-terminated UTF-8.
- Every option ID and every choice value ID must fit its fixed-size field.

#### CHOICE

- `value_count` must be from 1 through 16.
- Each entry has a stable numeric `value`, a stable symbolic `id`, and a presentation `label`.
- Numeric values and symbolic IDs must be unique within the option.
- `default_value` must equal one of the declared numeric values.
- The request stores the selected numeric value. The symbolic ID remains part of the schema contract.

#### INTEGER

- `value_count` must be zero.
- `min_value <= max_value`.
- `step > 0`.
- The default must be inside the range and lie on the sequence beginning at `min_value`: `(default_value - min_value) % step == 0`.
- The host validates the entered value against the same range and step.

#### BOOLEAN

- `value_count` must be zero.
- `default_value` must be 0 or 1.
- The request value is 0 (false) or 1 (true).

The host rejects a malformed schema and does not register an invalid generic encoder. Keep option IDs and choice IDs stable between codec releases. Labels may be translated or revised without changing saved settings.

### 7.3 Encode request behavior

The host initializes `RideCodecEncodeRequest` with its structure size and the schema version. Each parameter contains the option ID and its numeric value.

A codec should:

- Validate the request header, version, count, reserved fields, and null termination of every ID.
- Reject unknown and duplicate parameter IDs.
- Validate each value against the matching option definition.
- Use the schema default for any omitted option, as specified by the generic interface contract.
- Clear the output structure before work, check the input image layout and limits, and avoid integer overflow.
- Return non-zero only when a complete encoded buffer is available.

The Viewer 2 profile and Save As flows pass the full option set in schema order and revalidate saved profile values against the currently loaded schema before encoding.

## 8. Codec loading and runtime rules

The host scans the `Codecs` directory next to the plugin DLL and attempts to load each DLL.

1. The four required v1 exports must exist.
2. `GetInfo` must return valid metadata and the base API version.
3. Extensions are parsed and normalized. A matching extension only changes the order in which Probe is called.
4. `GetId` is used when present; otherwise a fallback ID is derived from the DLL filename. Duplicate IDs are rejected.
5. `GetCapabilities`, when present, is queried and validated.
6. Optional Encode interfaces are accepted only when they form a complete interface consistent with the declared capabilities.
7. When a complete file is read, the host calls Probe with the full byte buffer. Decode begins only after a codec recognizes the data.
8. The returned image is validated and copied into host-owned memory before the codec's `FreeImage` function is called.

The codec DLL remains loaded while the host holds a codec handle or an image allocated by that codec. Restart Far Manager after adding, removing, or replacing codec DLLs; the registry is cached for the session.

## 9. Minimal DLL declaration example

A codec should include the shared header and export C-linkage functions. The following is only the declaration pattern; the decoder, validation, allocation, and error handling must be implemented by the codec.

```cpp
#include "ride_viewer_codec_api.h"

extern "C" {

uint32_t RIDE_CODEC_CALL RideCodecGetInfo(RideCodecInfo* info);
uint32_t RIDE_CODEC_CALL RideCodecProbe(const uint8_t* data, size_t size);
uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size,
    RideImage* image, RideCodecError* error);
void RIDE_CODEC_CALL RideCodecFreeImage(RideImage* image);

/* Optional ABI v2 exports. */
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

Use the full header declarations as the authoritative function signatures. When a function is optional, do not export a stub that advertises a capability it cannot actually provide.

## 10. Compatibility and migration checklist

When migrating a v1 codec:

1. Keep `RideCodecGetInfo`, `RideCodecProbe`, `RideCodecDecode`, and `RideCodecFreeImage` unchanged.
2. Keep the existing v1 structures and their field order and types unchanged.
3. First verify that the DLL still loads as a decode-only module.
4. Add `RideCodecGetId` if the codec needs stable identity for settings or profiles.
5. Add `RideCodecDecodeEx` if source-format metadata is useful.
6. Add `RideCodecGetCapabilities` to declare the capabilities actually implemented.
7. To use the Viewer 2 generic export UI, add `RideCodecFreeEncodedData`, `RideCodecGetEncodeOptions`, and `RideCodecEncodeEx`; advertise `RIDE_CODEC_CAP_ENCODE` and RGBA8 input.
8. Validate success, failure, malformed inputs, and repeated allocate/free cycles in a dedicated test.

Do not change `RIDE_CODEC_API_VERSION` to 2 for these extensions. A future incompatible base ABI requires a separately designed version and explicit host support.
