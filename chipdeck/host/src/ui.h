// UI: главное (скрытое) окно с лотком, окно плеера, окно плейлиста.
#pragma once

#include "player.h"

namespace host {

// Запускает UI (блокирует до выхода: цикл сообщений).
int run_ui(EngineManager& em, Player& player, bool start_minimized);

} // namespace host
