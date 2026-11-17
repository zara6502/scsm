// ChipDeck host: точка входа.
//
// GUI-режим:  chipdeck_host.exe            — лоток + окна
// Headless:   chipdeck_host.exe --play <file> [seconds] [track]
//               играет файл на устройстве по умолчанию и выходит;
//               используется для автоматической проверки тракта.
// Каталог движков: <exe>/Engines или переменная окружения CHIPDECK_ENGINES.

#include "common.h"
#include "engine_manager.h"
#include "player.h"
#include "ui.h"

#include <cstdio>
#include <thread>

using namespace host;

int wmain(int argc, wchar_t** argv) {
    // headless-режим GUI-приложения: подключаемся к консоли родителя.
    if (argc >= 2 && wcscmp(argv[1], L"--play") == 0) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    EngineManager em;

    std::wstring engines_dir;
    wchar_t env_buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"CHIPDECK_ENGINES", env_buf, MAX_PATH))
        engines_dir = env_buf;
    else
        engines_dir = exe_dir() + L"\\Engines";

    const size_t loaded = em.load_directory(engines_dir);

    // ---- headless-режим ----
    if (argc >= 3 && wcscmp(argv[1], L"--play") == 0) {
        std::printf("engines loaded: %zu\n", loaded);
        if (loaded == 0) return 2;

        Player player;
        // headless: без notify-окна; конец трека игнорируем.
        if (!player.init(nullptr, &em)) {
            std::printf("WASAPI init FAILED\n");
            return 3;
        }
        const int idx = player.add_file(argv[2]);
        if (idx < 0) {
            std::printf("add_file FAILED (не распознан или не читается)\n");
            return 4;
        }
        const uint32_t seconds = argc > 3 ? wcstoul(argv[3], nullptr, 10) : 5;
        const uint32_t track = argc > 4 ? wcstoul(argv[4], nullptr, 10) : 0;
        if (!player.play(idx, track)) {
            std::printf("play FAILED\n");
            return 5;
        }
        std::printf("playing '%ls' (track %u) for %u s @ %u Hz...\n", argv[2],
                    track, seconds, player.sample_rate());
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        const uint32_t played_ms = player.position_ms();
        std::printf("position after: %u ms\n", played_ms);
        player.shutdown();
        return played_ms > 0 ? 0 : 6;
    }

    // ---- GUI-режим ----
    if (loaded == 0)
        MessageBoxW(nullptr,
                    L"Не найден ни один движок в каталоге Engines.",
                    L"ChipDeck", MB_ICONWARNING);

    Player player;
    const bool minimized = argc >= 2 && wcscmp(argv[1], L"--tray") == 0;
    return run_ui(em, player, minimized);
}
