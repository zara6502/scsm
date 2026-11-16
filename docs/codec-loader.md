# Codec loader and registry

This document describes the runtime behavior of `CodecModule` and `CodecRegistry` in Far Ride Viewer 2. The public ABI contract is documented separately in [Codec API](CODEC_API.md).

## Responsibilities

- `CodecModule` loads one codec DLL, resolves its exports, validates the ABI declarations, and owns the module handle.
- `CodecRegistry` stores successfully loaded modules, prevents duplicate registration, scans the runtime codec directory, and provides codec lookup.
- Image decoding and encoding are performed by the caller through validated function pointers. The registry does not decode or encode images by itself.

## Loading sequence

A DLL is accepted through the following checks:

1. Load the module with `LoadLibraryW`.
2. Resolve the four required ABI v1 exports:
   `RideCodecGetInfo`, `RideCodecProbe`, `RideCodecDecode`, and
   `RideCodecFreeImage`. If any is missing, reject the DLL.
3. Call `RideCodecGetInfo`. Validate the base API version, null-terminated
   name and extension strings, and the extension list syntax.
4. Resolve optional `RideCodecGetId`. If present, validate the stable ASCII ID.
   Otherwise derive a fallback ID from the DLL filename stem.
5. Resolve optional `RideCodecDecodeEx`, `RideCodecGetCapabilities`,
   `RideCodecEncode`, `RideCodecFreeEncodedData`,
   `RideCodecGetEncodeOptions`, and `RideCodecEncodeEx`.
6. If `RideCodecGetCapabilities` is absent, register the module as a legacy
   decode-only codec. Optional identity and `DecodeEx` exports can still be
   used; Encode is disabled.
7. If capabilities are present, validate the structure size and capabilities
   version. The codec must declare Decode and RGBA8 output.
8. Validate relationships between capability flags and exports. Contradictory
   declarations reject the module.
9. If generic Encode schema and `EncodeEx` are both present, query and fully
   validate the option schema before marking generic encoding as available.

## Encode validation

The loader treats the optional encoding interfaces as follows:

- If `RIDE_CODEC_CAP_ENCODE` is not set, all Encode pointers are disabled by
  the loader.
- If Encode is set, the codec must provide `RideCodecFreeEncodedData`, an
  encoding function (`RideCodecEncode` or `RideCodecEncodeEx`), and RGBA8
  input capability.
- `RideCodecGetEncodeOptions` and `RideCodecEncodeEx` must be present
  together. A schema without its matching encoder, or an encoder without its
  schema, is rejected.
- A legacy `RideCodecEncode` implementation without the generic schema can
  be loaded as an encoding-capable module, but it is not exposed by the
  Viewer 2 Save As dialog or the `s1`–`s9` profile UI. Those UI flows require
  the generic schema and `EncodeEx`.

The generic schema validator checks:

- Full schema structure size and the expected schema version.
- Option count (maximum 16) and all reserved fields.
- Unique, null-terminated stable option IDs.
- Non-empty, null-terminated UTF-8 labels.
- CHOICE option count (1–16), unique numeric values and choice IDs, and a
  default that matches one of the choices.
- INTEGER range, positive step, and a default aligned to the range/step.
- BOOLEAN default restricted to 0 or 1.
- Reserved option flags and fields set to zero.

The viewer validates the selected or saved request values again before calling
the encoder. A saved profile that no longer matches the codec schema is not
sent to the codec; the user is asked to configure that profile again.

## Capabilities and pixel format

The current host accepts Decode output in top-down RGBA8. It rejects a
capabilities structure that does not declare Decode and RGBA8 output. It also
rejects contradictory declarations such as ALPHA without RGBA.

Unknown capability and pixel-format bits are masked by the current loader.
Codec developers should advertise only implemented capabilities and should
not rely on unknown bits being retained by the host.

## Codec identity and conflicts

A stable ID returned by `RideCodecGetId` is used by export profiles and
codec-related settings. IDs must be unique among registered modules. A second
codec with an already registered ID is rejected.

If a codec does not export `GetId`, the loader derives a fallback from the DLL
filename. This is sufficient for runtime discovery, but an explicit stable ID
is recommended for any codec whose settings or profiles should survive DLL
renaming.

## Directory scanning

`CodecRegistry::load_directory()` scans one directory level. It considers
regular files whose extension is `.dll`, case-insensitively. It does not
recurse into subdirectories.

Candidate paths are sorted before loading, so registration order is
deterministic. Re-registering the same normalized absolute path returns the
already loaded module. A malformed DLL does not prevent the other candidates
from being processed.

The scan report contains:

- `discovered`: number of DLL candidates;
- `loaded`: number of modules successfully registered;
- `rejected`: DLL paths and reasons for rejection;
- `directory_error`: an error opening or enumerating the directory.

Test codec stubs are placed in a separate `TestCodecs` directory, not in the
runtime `Codecs` directory.

## Extension parsing and codec selection

The `RideCodecInfo.extensions` field contains a semicolon-separated list,
such as `qoi;png`. A leading dot is optional. Whitespace at the ends of an
entry is trimmed, and letters are normalized to lowercase. Only ASCII
letters, digits, hyphen, and underscore are accepted. Empty entries and
invalid characters reject the DLL. Duplicate extensions within one list are
collapsed.

The registry can find a codec by extension, but extension matching is only a
hint. Content selection uses `CodecRegistry::find_decoder_by_content(data,
size, extension_hint)`:

1. Reject null or empty input immediately.
2. If an extension hint is supplied, call Probe on codecs that declare that
   extension first.
3. If none recognizes the data, call Probe on the remaining codecs.
4. Within each group, preserve registry order and choose the first successful
   Probe.

A matching extension never overrides a failed Probe. Full-file recognition is
required before Decode.

## Module lifetime

The registry stores a `shared_ptr` for each loaded `CodecModule`. A caller
that holds a codec function pointer or a buffer allocated by that codec must
retain its own module handle until the operation has completed and the buffer
has been released.

Calling `CodecRegistry::clear()` removes the registry's ownership, but does
not unload a DLL while a caller still holds a `shared_ptr`. The DLL is
unloaded by the destructor of the last `CodecModule`.

This lifetime rule applies to both decoded images and encoded data. Always
call the matching module's `RideCodecFreeImage` or
`RideCodecFreeEncodedData` before releasing the last reference to the
module.

## Tests

`ride_viewer_codec_loader_test` checks:

- QOI loading and discovery of Decode plus generic Encode;
- fallback registration for a DLL without Capabilities;
- rejection of a DLL missing a required export;
- rejection of malformed capability declarations;
- rejection of inconsistent Encode declarations and incomplete generic
  schema/encoder pairs;
- rejection of duplicate codec IDs;
- extension parsing, normalization, lookup, and conflicts;
- content-based selection with correct, incorrect, and absent extension hints;
- continued module operation after registry cleanup while a caller retains a
  `shared_ptr`.
