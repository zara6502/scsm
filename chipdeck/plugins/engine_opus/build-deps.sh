#!/usr/bin/env bash
# Статическая сборка opusfile (декодер Ogg Opus) для движка Opus ChipDeck.
# Окружение: MSYS2 UCRT64. opusfile — autotools; libopus и libogg
# берутся из системы (pacman: mingw-w64-ucrt-x86_64-libopus libogg,
# статические libopus.a/libogg.a + .pc уже есть).
# Результат: deps/inst/{include,lib}.
# Требует в MSYS2: autoconf automake libtool pkgconf make.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/deps"
PREFIX="$DEPS/inst"
export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CC=gcc CXX=g++
# -pipe: gcc не создаёт временные файлы (в песочнице TMP может быть
# недоступен — "Cannot create temporary file in C:\WINDOWS").
export CFLAGS="-O2 -pipe"
export TMP="${TMP:-C:\\Users\\jimdg\\AppData\\Local\\Temp}"
export TEMP="$TMP"

build_autotools() { # <src-dir> <build-dir> <extra configure flags...>
  local src="$1" bld="$2"; shift 2
  mkdir -p "$bld"
  ( cd "$bld" && "$src/configure" \
      --prefix="$PREFIX" --enable-static --disable-shared \
      --disable-dependency-tracking --disable-examples \
      --disable-doc --disable-http "$@" ) >> "$bld/configure.log" 2>&1 || {
    tail -30 "$bld/configure.log"; exit 1; }
  make -C "$bld" >> "$bld/make.log" 2>&1 || { tail -40 "$bld/make.log"; exit 1; }
  make -C "$bld" install >> "$bld/install.log" 2>&1
}

# --- opusfile (демуксер Ogg + декодер Opus поверх libopus/libogg) ---
if [ ! -f "$PREFIX/lib/libopusfile.a" ]; then
  echo "== opusfile =="
  if [ ! -f "$DEPS/src-opusfile/configure.ac" ]; then
    mkdir -p "$DEPS/src-opusfile"
    curl -sL https://github.com/xiph/opusfile/archive/refs/tags/v0.12.tar.gz \
      | tar xz -C "$DEPS/src-opusfile" --strip-components=1
  fi
  if [ ! -f "$DEPS/src-opusfile/configure" ]; then
    ( cd "$DEPS/src-opusfile" && ./autogen.sh ) \
      >> "$DEPS/src-opusfile/autogen.log" 2>&1
  fi
  build_autotools "$DEPS/src-opusfile" "$DEPS/build-opusfile" \
    OPUS_CFLAGS="-I/ucrt64/include/opus" OPUS_LIBS="-L/ucrt64/lib -lopus" \
    OGG_CFLAGS="-I/ucrt64/include" OGG_LIBS="-L/ucrt64/lib -logg"
fi

echo "Готово: $PREFIX"
ls -la "$PREFIX/lib" | grep -i opus
