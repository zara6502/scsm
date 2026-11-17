#!/usr/bin/env bash
# Собирает готовый к копированию дистрибутив: dist/ChipDeck/
#   chipdeck_host.exe
#   Engines/   — DLL движков (gme, sidplayfp, openmpt)
#   Libraries/ — DLL библиотечных плагинов (botb)
#   README.txt — короткая инструкция
# Предполагается, что все проекты уже собраны (cmake --build <build>).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$ROOT/dist/ChipDeck"

rm -rf "$DIST"
mkdir -p "$DIST/Engines" "$DIST/Libraries" "$DIST/Skin"

cp "$ROOT/host/build/chipdeck_host.exe" "$DIST/"

for eng in engine_gme engine_sidplayfp engine_openmpt engine_stub; do
  src="$ROOT/plugins/$eng/build/Engines/$eng.dll"
  [ -f "$src" ] && cp "$src" "$DIST/Engines/" || echo "SKIP (не собран): $eng"
done

for lib in lib_botb; do
  src="$ROOT/plugins/$lib/build/Libraries/$lib.dll"
  [ -f "$src" ] && cp "$src" "$DIST/Libraries/" || echo "SKIP (не собран): $lib"
done

# скин по умолчанию
if [ -d "$ROOT/skins/default" ]; then
  cp "$ROOT/skins/default/"* "$DIST/Skin/"
fi

cat > "$DIST/README.txt" << 'EOF'
ChipDeck — чиптюн-плеер в системном лотке (Windows x64)
========================================================

Запуск: chipdeck_host.exe
  - иконка в лотке: одинарный клик = Play/Pause,
    ПКМ = меню (Player / Playlist / Library / Quit)
  - drag&drop файлов .ay .emul .sap .sid .mod .s3m .xm .it в окна

Engines\  — движки-декодеры (DLL): AY/POKEY, SID, трекеры (MOD/S3M/XM/IT),
            NSF/NSFE. Каждый файл — отдельный плагин, можно удалять.
Libraries\ — плагины онлайн-библиотек (Battle of the Bits).
Cache\    — создаётся автоматически для скачанных треков.

Настройка библиотек (для скачивания с BotB нужен логин):
  создайте Libraries/botb.ini:
      email=ваш@botbr.mail
      password=пароль

Настройки движков не требуются; каталоги по умолчанию:
  Engines\ рядом с exe, Libraries\ рядом с exe
  (переопределяются переменными CHIPDECK_ENGINES / CHIPDECK_LIBS).

Headless-проверка без GUI:
  chipdeck_host.exe --play <файл> [секунд] [подтюн]
  chipdeck_host.exe --top
EOF

echo "Дистрибутив: $DIST"
find "$DIST" -type f | sort
