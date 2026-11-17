#!/usr/bin/env bash
# Статическая сборка libresidfp + libsidplayfp для движка SID ChipDeck.
# Окружение: MSYS2 UCRT64. Обе библиотеки — autotools.
# Результат: deps/inst/{include,lib} + .pc для pkg-config.
# Требует в MSYS2: autoconf automake libtool pkgconf make.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/deps"
PREFIX="$DEPS/inst"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

build_autotools() { # <src-dir> <build-dir> <extra configure flags...>
  local src="$1" bld="$2"; shift 2
  mkdir -p "$bld"
  ( cd "$bld" && "$src/configure" \
      --prefix="$PREFIX" --enable-static --disable-shared \
      --disable-dependency-tracking "$@" ) >> "$bld/configure.log" 2>&1 || {
    tail -30 "$bld/configure.log"; exit 1; }
  make -C "$bld" >> "$bld/make.log" 2>&1 || { tail -40 "$bld/make.log"; exit 1; }
  make -C "$bld" install >> "$bld/install.log" 2>&1
}

# --- libresidfp (эмуляция SID-чипа) ---
if [ ! -f "$PREFIX/lib/libresidfp.a" ]; then
  echo "== libresidfp =="
  if [ ! -f "$DEPS/src-residfp/configure.ac" ]; then
    mkdir -p "$DEPS/src-residfp"
    curl -sL https://github.com/libsidplayfp/libresidfp/archive/refs/tags/v1.3.0.tar.gz \
      | tar xz -C "$DEPS/src-residfp" --strip-components=1
  fi
  ( cd "$DEPS/src-residfp" && autoreconf -fi ) > "$DEPS/residfp-autoreconf.log" 2>&1 \
    || { tail -20 "$DEPS/residfp-autoreconf.log"; exit 1; }
  build_autotools "$DEPS/src-residfp" "$DEPS/build-residfp"
fi

# --- libsidplayfp (загрузчик PSID/RSID + движок) ---
if [ ! -f "$PREFIX/lib/libsidplayfp.a" ]; then
  echo "== libsidplayfp =="
  if [ ! -f "$DEPS/src/configure.ac" ]; then
    mkdir -p "$DEPS/src"
    curl -sL https://github.com/libsidplayfp/libsidplayfp/archive/refs/tags/v3.1.2.tar.gz \
      | tar xz -C "$DEPS/src" --strip-components=1
  fi
  ( cd "$DEPS/src" && autoreconf -fi ) > "$DEPS/sid-autoreconf.log" 2>&1 \
    || { tail -20 "$DEPS/sid-autoreconf.log"; exit 1; }
  build_autotools "$DEPS/src" "$DEPS/build-sid" \
      --without-exsid --without-usbsid --disable-tests --disable-testsuite
fi

echo "Готово: $PREFIX"
ls "$PREFIX/lib"
