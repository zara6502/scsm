// ChipDeck host: точка входа.
//
// GUI-режим:  chipdeck_host.exe            — лоток + окна
// Headless:   chipdeck_host.exe --play <file> [seconds] [track]
//               играет файл на устройстве по умолчанию и выходит;
//               используется для автоматической проверки тракта.
// Каталог движков: <exe>/Engines или переменная окружения CHIPDECK_ENGINES.

#include "common.h"
#include "engine_manager.h"
#include "library_manager.h"
#include "player.h"
#include "skin.h"
#include "ui.h"

#include <cstdio>
#include <thread>

using namespace host;

namespace host { extern int g_alphablend_failures; }

int wmain(int argc, wchar_t** argv) {
    // COM для WIC (скины) и WASAPI: один раз на весь процесс, MTA.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // headless-режим GUI-приложения: подключаемся к консоли родителя.
    if (argc >= 2 && (wcscmp(argv[1], L"--play") == 0 ||
                      wcscmp(argv[1], L"--top") == 0)) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    EngineManager em;
    LibraryManager lm;

    std::wstring engines_dir;
    wchar_t env_buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"CHIPDECK_ENGINES", env_buf, MAX_PATH))
        engines_dir = env_buf;
    else
        engines_dir = exe_dir() + L"\\Engines";

    std::wstring libs_dir;
    if (GetEnvironmentVariableW(L"CHIPDECK_LIBS", env_buf, MAX_PATH))
        libs_dir = env_buf;
    else
        libs_dir = exe_dir() + L"\\Libraries";

    const size_t loaded = em.load_directory(engines_dir);
    const size_t libs = lm.load_directory(libs_dir);

    // ---- headless: топ онлайн-библиотеки ----
    if (argc >= 2 && wcscmp(argv[1], L"--top") == 0) {
        std::printf("engines: %zu, libraries: %zu\n", loaded, libs);
        if (libs == 0) return 2;
        lm.connect_all();
        LibModule& m = lm.mutable_at(0);
        if (!m.session) {
            std::printf("connect FAILED\n");
            return 3;
        }
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = CHP_QUERY_TOP;
        req.limit = 10;
        volatile int cancel = 0;
        ChipError qerr{};
        uint32_t n = 0;
        ChipLibEntry first{};
        auto ctx = std::make_pair(&n, &first);
        m.query_fn(m.session, &req,
                   [](void* user, const ChipLibEntry* e) -> uint32_t {
                       // user = {счётчик, первая запись}
                       auto* ctx =
                           static_cast<std::pair<uint32_t*, ChipLibEntry*>*>(
                               user);
                       std::printf("%u. [%s] %s - %s (rating %u%s)\n",
                                   ++(*ctx->first), e->format, e->title,
                                   e->author, e->rating,
                                   (e->flags & CHP_ENTRY_HAS_DURATION)
                                       ? "" : ", dur n/a");
                       if (!ctx->second->entry_id[0]) *ctx->second = *e;
                       return 0;
                   },
                   &ctx, &cancel, &qerr);
        // пробное скачивание первой записи (проверка логина/Fetch)
        FILE* log = std::fopen("top_log.txt", "w");
        if (log)
            std::fprintf(log, "first_id=%s dl=%s\n", first.entry_id,
                         first.download_hint);
        if (first.entry_id[0]) {
            uint8_t* data = nullptr;
            size_t size = 0;
            ChipError ferr{};
            if (m.fetch_fn(m.session, &first, nullptr, nullptr, &cancel,
                           &data, &size, &ferr)) {
                if (log)
                    std::fprintf(log,
                                 "fetch OK: %zu bytes (%02X %02X %02X %02X)\n",
                                 size, data[0], data[1], data[2], data[3]);
                m.free_buffer(m.session, data);
            } else {
                if (log)
                    std::fprintf(log, "fetch FAILED: %s\n", ferr.message);
            }
        }
        if (log) std::fclose(log);
        lm.disconnect_all();
        return n > 0 ? 0 : 4;
    }

    // ---- headless: визуальная проверка скина ----
    if (argc >= 3 && wcscmp(argv[1], L"--skintest") == 0) {
        std::printf("engines: %zu\n", loaded);
        Skin skin;
        std::string serr;
        if (!skin_load(exe_dir() + L"\\Skin", skin, serr)) {
            std::printf("skin_load FAILED: %s\n", serr.c_str());
            return 3;
        }
        Player player;
        if (!player.init(nullptr, &em)) return 4;
        const int idx = player.add_file(argv[2]);
        if (idx < 0 || !player.play(idx, 0)) {
            std::printf("add/play FAILED\n");
            return 5;
        }
        Sleep(800); // немного рендера

        // рисуем скин в DIB и сохраняем BMP
        const int W = skin.w, H = skin.h;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        bi.bmiHeader.biBitCount = 32;
        void* pbits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pbits,
                                       nullptr, 0);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old_bmp = SelectObject(mem, bmp);
        skin_draw(mem, skin, player);
        // 32bpp -> 24bpp BMP: читаем ИЗ БИТОВ DIB (pbits), а не из вектора
        std::vector<uint8_t> out(size_t(W) * H * 3);
        const uint8_t* dib_bits = static_cast<const uint8_t*>(pbits);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const uint8_t* px = dib_bits + (size_t(y) * W + x) * 4;
                uint8_t* o = out.data() + (size_t(y) * W + x) * 3;
                o[0] = px[0]; o[1] = px[1]; o[2] = px[2];
            }
        BITMAPINFOHEADER bh{};
        bh.biSize = sizeof(bh); bh.biWidth = W; bh.biHeight = -H;
        bh.biPlanes = 1; bh.biBitCount = 24; bh.biCompression = BI_RGB;
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bh);
        fh.bfSize = DWORD(sizeof(fh) + sizeof(bh) + out.size());
        FILE* f = std::fopen("skin_test.bmp", "wb");
        std::fwrite(&fh, sizeof(fh), 1, f);
        std::fwrite(&bh, sizeof(bh), 1, f);
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        DeleteDC(mem); DeleteDC(dc);
        player.shutdown();
        FILE* d = std::fopen("skin_diag.txt", "w");
        if (d) {
            std::fprintf(d, "bg=%d bgw=%d bgh=%d buttons=%u texts=%u prog=%u\n",
                         skin.bg != nullptr, skin.bg_w, skin.bg_h,
                         unsigned(skin.buttons.size()),
                         unsigned(skin.texts.size()),
                         unsigned(skin.progress.size()),
                         g_alphablend_failures);
            std::fclose(d);
        }
        return 0;
    }

    // ---- headless: воспроизведение ----
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
    return run_ui(em, lm, player, minimized);
}
