#!/usr/bin/env bash
# Статическая сборка минимальных libde265 + libheif для кодека HEIC.
# Окружение: MSYS2 UCRT64 (cmake, ninja, gcc в PATH).
# Результат: deps/inst/{include,lib} — подключается основным CMakeLists.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/deps/src"
PREFIX="$ROOT/deps/inst"
mkdir -p "$PREFIX"

echo "== libde265 ${LIBDE265_VER:-v1.1.3} (static) =="
cmake -S "$SRC/libde265" -B "$ROOT/deps/build-de265" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_SDL=OFF
cmake --build "$ROOT/deps/build-de265" --target install

echo "== libheif ${LIBHEIF_VER:-v1.23.7} (static, только HEVC-декодер) =="
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$SRC/libheif" -B "$ROOT/deps/build-heif" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_CXX_FLAGS="-DLIBDE265_STATIC_BUILD" \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_PLUGIN_LOADING=OFF \
  -DWITH_LIBDE265=ON \
  -DWITH_X265=OFF -DWITH_X264=OFF -DWITH_KVAZAAR=OFF -DWITH_UVG266=OFF \
  -DWITH_VVDEC=OFF -DWITH_VVENC=OFF -DWITH_OpenH264_DECODER=OFF \
  -DWITH_DAV1D=OFF -DWITH_AOM_DECODER=OFF -DWITH_AOM_ENCODER=OFF \
  -DWITH_SvtEnc=OFF -DWITH_RAV1E=OFF \
  -DWITH_JPEG_DECODER=OFF -DWITH_JPEG_ENCODER=OFF \
  -DWITH_OpenJPEG_DECODER=OFF -DWITH_OpenJPEG_ENCODER=OFF \
  -DWITH_FFMPEG_DECODER=OFF -DWITH_OPENJPH_ENCODER=OFF \
  -DWITH_LIBSHARPYUV=OFF -DWITH_UNCOMPRESSED_CODEC=OFF \
  -DWITH_EXAMPLES=OFF -DWITH_GDK_PIXBUF=OFF \
  -DBUILD_TESTING=OFF -DBUILD_DOCUMENTATION=OFF
cmake --build "$ROOT/deps/build-heif" --target install

echo "Готово: $PREFIX"
ls -la "$PREFIX/lib"
