#!/usr/bin/env bash
# Статическая сборка libopenmpt 0.8.10 для движка ChipDeck (autotools).
# Берём ОФИЦИАЛЬНЫЙ autotools-тарбол с lib.openmpt.org: GitHub-тег архива
# не содержит готового configure. Без zlib/mpg123/ogg/vorbis, без примеров.
# Результат: deps/inst/{include,lib}.
# Требует в MSYS2: autoconf automake libtool pkgconf make.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/deps"
PREFIX="$DEPS/inst"
SRC="$DEPS/src"

if [ ! -f "$PREFIX/lib/libopenmpt.a" ]; then
  if [ ! -f "$SRC/configure" ]; then
    echo "== загрузка libopenmpt 0.8.10 (release autotools tarball) =="
    rm -rf "$SRC"; mkdir -p "$SRC"
    curl -sL "https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.10+release.autotools.tar.gz" \
      | tar xz -C "$SRC" --strip-components=1
  fi

  echo "== configure + make =="
  mkdir -p "$DEPS/build"
  # -g по умолчанию раздувает .a до ~160 МБ: собираем релиз.
  ( cd "$DEPS/build" && \
      CFLAGS="-O2 -DNDEBUG" CXXFLAGS="-O2 -DNDEBUG" LDFLAGS="-s" \
      "$SRC/configure" \
      --prefix="$PREFIX" --enable-static --disable-shared \
      --without-zlib --without-mpg123 --without-ogg \
      --without-vorbis --without-vorbisfile \
      --disable-openmpt123 --disable-examples --disable-tests \
      --disable-dependency-tracking ) \
    > "$DEPS/configure.log" 2>&1 || { tail -30 "$DEPS/configure.log"; exit 1; }
  make -C "$DEPS/build" >> "$DEPS/make.log" 2>&1 || { tail -40 "$DEPS/make.log"; exit 1; }
  make -C "$DEPS/build" install >> "$DEPS/install.log" 2>&1
  # сборка autotools идёт с -g: снимаем debug-информацию из архива
  # (200 МБ -> ~10 МБ)
  strip --strip-debug "$PREFIX/lib/libopenmpt.a"
fi

echo "Готово: $PREFIX"
ls "$PREFIX/lib"
