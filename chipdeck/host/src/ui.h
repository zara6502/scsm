// UI: главное (скрытое) окно с лотком, окно плеера, окно плейлиста,
// окно Library (онлайн-библиотеки).
#pragma once

#include "library_manager.h"
#include "player.h"

namespace host {

// Запускает UI (блокирует до выхода: цикл сообщений).
int run_ui(EngineManager& em, LibraryManager& lm, Player& player,
           bool start_minimized);

} // namespace host
