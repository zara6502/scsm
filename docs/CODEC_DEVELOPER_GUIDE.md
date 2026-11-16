# Codec Developer Guide

[Русская версия](CODEC_DEVELOPER_GUIDE.ru.md)

This guide explains how to build a codec DLL for Far Ride Viewer 2, add the optional ABI v2 extensions, and validate the module before distribution.

For exact structure layouts and field validation rules, see the [Codec API reference](CODEC_API.md). The shared header `include/ride_viewer_codec_api.h` is authoritative.

## 1. Choose the interface level

| Level | Exports | Result in Viewer 2 |
|---|---|---|
| ABI v1 | `GetInfo`, `Probe`, `Decode`, `FreeImage` | Decode-only codec |
| Stable identity | Add `GetId` | Stable ID for settings and profiles |
| Source metadata | Add `DecodeEx` | Source pixel-format details on the F1 screen |
| Capabilities | Add `GetCapabilities` | Explicit operation and pixel-format declarations |
| Generic encoding | Add `FreeEncodedData`, `GetEncodeOptions`, `EncodeEx`, declare Encode | Save As and profile-based export |

ABI v2 is additive. Start with a working v1 decoder and add optional exports only when needed. `RIDE_CODEC_API_VERSION` remains 1; the capabilities structure has its own version 2.

## 2. Create a codec project

A codec is a Windows DLL loaded from the `Codecs` directory beside `FarRideViewer.dll`.

```text
my-codec/
|-- CMakeLists.txt
|-- codec.cpp
`-- tests/
    |-- codec_test.cpp
    `-- samples/
```

Include the shared header:

```cpp
#include "ride_viewer_codec_api.h"
```

Do not duplicate ABI structures in a private header. A local copy can drift from the host and cause memory corruption.

## 3. Build the DLL with CMake

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

Build with MSYS2 UCRT64:

```bash
cmake -S . -B build -G Ninja \\
  -DCMAKE_BUILD_TYPE=Release \\
  -DFAR_RIDE_VIEWER_ROOT="/path/to/far-ride-viewer-2"
cmake --build build
```

The result should be `build/Codecs/my_codec.dll`. Its architecture must match Far Manager. For MinGW distribution builds, statically linking the GCC and C++ runtime libraries avoids an extra dependency on local MSYS2 runtime DLLs:

```cmake
if(MINGW)
  target_link_options(my_codec PRIVATE
    -static -static-libgcc -static-libstdc++
  )
endif()
```

Ship any additional DLL dependencies in a location where Windows can resolve them at load time.

## 4. Implement the required v1 functions

### GetInfo

Return a short codec name and semicolon-separated extension list. Strings are UTF-8, null-terminated, and must fit their fixed-size fields. Extensions have no leading dots, for example `foo;bar`.

### Probe

Probe receives a pointer and exact byte count, not a filename. It must check `data` and `size` before reading, never read past the supplied range, safely reject truncated headers, and avoid decoding the full image. It should validate enough of the format to avoid common false positives.

The extension only affects the order in which the host calls Probe. A successful full-file Probe is required before Decode.

### Decode

Decode receives the complete file buffer and returns top-down RGBA8. Before allocating memory, validate the header, format variant, dimensions, pixel count, required input bytes, and all arithmetic used to calculate offsets and buffer sizes.

Use straight alpha, not premultiplied alpha. Set alpha to 255 for opaque input.

### FreeImage

Allocate and free the image buffer inside the same DLL. The host calls FreeImage even after a failed Decode, so it must safely accept null, empty, and partially initialized structures.

Never let exceptions cross the C ABI. Catch allocation and decoding exceptions inside the export and convert them into a failure return plus `RideCodecError`.

## 5. Add stable identity and capabilities

### Stable ID

Export `RideCodecGetId` and return a stable lowercase ASCII ID such as `my_codec`. Do not use a version string or display name. The host uses this ID for settings and saved export profiles.

### Capabilities

A v2-aware codec should export `RideCodecGetCapabilities`:

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

Set only flags and pixel-format bits that the module really supports. A decoder must declare Decode and RGBA8 output. Do not declare Encode until the full encoding interface and memory-release function are available.

## 6. Provide source-format metadata with DecodeEx

Use `RideCodecDecodeEx` when normalized RGBA8 output does not communicate important details about the original file. For example, a 4-bit indexed PCX still has an indexed source representation and a palette even though the viewer receives RGBA8 pixels.

Initialize `RideImageMetadata::struct_size` and `version` before the call. Fill only fields that are known. All strings must be null-terminated UTF-8. Metadata is advisory; decoding must remain valid without it.

## 7. Add generic encoding

The current Viewer 2 Save As dialog and the `s1`–`s9` profile shortcuts use the generic schema interface. A legacy `RideCodecEncode` export alone is not enough to appear in these UI flows.

To participate in Save As and export profiles, provide:

- `RideCodecGetCapabilities` with `RIDE_CODEC_CAP_ENCODE`.
- `RIDE_CODEC_PIXEL_RGBA8` in `input_pixel_formats`.
- `RideCodecFreeEncodedData`.
- `RideCodecGetEncodeOptions`.
- `RideCodecEncodeEx`.

The two generic exports must be present together.

### Example schema

This example declares an output-depth choice and an RLE Boolean option:

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

The host validates schema size, version, identifiers, labels, duplicate IDs and values, defaults, ranges, steps, and reserved fields. A malformed schema prevents registration of the generic encoder.

### Implement EncodeEx defensively

The encoder receives RGBA8 input. Respect `RideImage::stride`; do not assume rows are tightly packed. Validate structure size, version, parameter count, IDs, reserved fields, values, dimensions, and source-buffer extent. Use the schema default for any omitted option. Reject duplicate and unknown IDs.

On success, allocate the encoded byte buffer inside the codec DLL, set `RideCodecEncodedData::data` and `data_size`, and return non-zero. On failure, leave output data null and size zero, set `RideCodecError` if supplied, release temporary allocations, and return zero.

`RideCodecFreeEncodedData` must release the returned buffer using the same allocator and clear the structure.

## 8. Test the module

At minimum, test:

1. Export table: all required and intended optional symbols are present.
2. GetInfo: base API version, non-empty UTF-8 strings, terminating null bytes.
3. Probe: positive and negative samples, empty input, short headers, truncation, and random data.
4. Decode: dimensions, exact pixel values, row order, alpha, stride, and data size.
5. Malformed input: bad signatures, unsupported variants, invalid dimensions, overflow, truncation, and excessive pixel counts.
6. Memory: successful free, free after failed Decode, repeated free, and repeated decode/free cycles.
7. Capabilities: flags agree with implemented functions and supported pixel formats.
8. Encode: each option type, defaults, range/step validation, invalid IDs, output validity, and FreeEncodedData.
9. Integration: load the DLL through the Viewer 2 registry and open a real file in Far Manager.

Run the repository test suite after configuring with the FAR SDK:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite checks ABI loading, legacy modules, QOI, PCX, RIDE decoding, real image files, and thumbnail resampling.

## 9. Release checklist

- [ ] DLL architecture matches Far Manager.
- [ ] Four required v1 exports are present.
- [ ] Every export uses C linkage and `RIDE_CODEC_CALL`.
- [ ] No C++ exception crosses the ABI boundary.
- [ ] GetInfo strings are valid UTF-8 and null-terminated.
- [ ] Probe is bounded and rejects malformed prefixes safely.
- [ ] Decode returns top-down, straight-alpha RGBA8.
- [ ] All sizes and offsets are checked for overflow.
- [ ] FreeImage is safe after successful and failed Decode.
- [ ] Capabilities match the functions and pixel formats actually supported.
- [ ] Stable IDs and parameter IDs do not change accidentally between releases.
- [ ] Encoded output is freed only through the same codec DLL.
- [ ] Tests cover valid, invalid, truncated, and real-world files.
- [ ] External dependencies and license notices are included.
