# Viewer file integration

This document describes the file-facing helper layer in `viewer_files.h` and `viewer_files.cpp`. It sits between the codec registry/image loader and the viewer UI.

## Supported formats

- `get_supported_image_formats(registry)` returns the names and extension lists of all registered Decode codecs, preserving registry order.
- `get_supported_image_extensions(registry)` returns a unique list of supported extensions.
- `build_open_file_dialog_filter(registry)` builds the MULTI_SZ string expected by `OPENFILENAMEW::lpstrFilter`: one combined supported-images filter, one filter per codec, and an All files filter.

The filter is generated from the DLLs actually loaded by the registry. The application does not hard-code a format list, so new codec modules appear automatically.

## ImageDocument

`open_image_document(registry, path, error)` opens an image through the shared image loader and returns an `ImageDocument`.

The document stores:

- The original filesystem path.
- The decoded image, including ownership of the codec module required to release its pixels.
- The codec name.
- Convenience accessors for the filename, width, and height.

The normalized pixel buffer is available through `decoded_image()`. The `ImageDocument` owns the `DecodedImage`; the latter retains the codec DLL until its image buffer is released.

The document is movable but not copyable, preventing accidental duplication of the underlying image ownership.

## Ownership and lifetime

The document owns a `std::unique_ptr<DecodedImage>`. The decoded image in turn holds a shared reference to its codec module. Therefore, moving an `ImageDocument` does not invalidate its pixel data or unload the DLL.

Destroy the document when the viewer no longer needs the image. The codec's matching `RideCodecFreeImage` is called as the decoded image is released.

## Scope

This module provides format enumeration, file-dialog filter construction, and an owning image-document abstraction. The actual viewer window, panel navigation, thumbnail UI, and drawing are implemented by the application layer.
