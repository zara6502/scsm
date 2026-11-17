#!/usr/bin/env bash
# Статическая сборка libgme 0.6.5 для движка ChipDeck.
# Окружение: MSYS2 UCRT64 (cmake, ninja, gcc в PATH).
# Собираются только форматы AY (ZX Spectrum) и SAP (Atari) — остальное
# отключено ради размера DLL. Результат: deps/inst/{include,lib}.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/deps/src"
PREFIX="$ROOT/deps/inst"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "== загрузка libgme 0.6.5 =="
  mkdir -p "$SRC"
  curl -sL https://github.com/libgme/game-music-emu/archive/refs/tags/0.6.5.tar.gz \
    | tar xz -C "$SRC" --strip-components=1
fi

echo "== libgme (static, только AY + SAP) =="
cmake -S "$SRC" -B "$ROOT/deps/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DGME_ZLIB=OFF \
  -DUSE_GME_AY=ON \
  -DUSE_GME_SAP=ON \
  -DUSE_GME_GBS=OFF -DUSE_GME_GYM=OFF -DUSE_GME_HES=OFF \
  -DUSE_GME_KSS=OFF -DUSE_GME_NSF=OFF -DUSE_GME_NSFE=OFF \
  -DUSE_GME_SPC=OFF -DUSE_GME_VGM=OFF
cmake --build "$ROOT/deps/build" --target install

echo "Готово: $PREFIX"
ls -la "$PREFIX/lib"
