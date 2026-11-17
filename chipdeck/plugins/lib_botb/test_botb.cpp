// Тест библиотечного плагина BotB: Query TOP/SEARCH/RANDOM + Fetch.
// Проверяется реальная сеть (нужен интернет).

#include "chipdeck_api.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

// колбэк Query: печатает запись, сохраняет первую для Fetch
struct Collector {
    ChipLibEntry first{};
    bool have_first = false;
    uint32_t count = 0;

    static uint32_t CHP_CALL cb(void* user, const ChipLibEntry* e) {
        Collector* c = static_cast<Collector*>(user);
        if (c->count < 5)
            std::printf("       [%s] %s — %s (%s, score→%u/1000%s)\n",
                        e->entry_id, e->title, e->author, e->format,
                        e->rating,
                        (e->flags & CHP_ENTRY_HAS_DURATION) ? "" : ", dur?");
        if (!c->have_first) {
            c->first = *e;
            c->have_first = true;
        }
        ++c->count;
        return 0;
    }
};

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE mod = LoadLibraryA(argc > 1 ? argv[1] : "build/Libraries/lib_botb.dll");
    if (!mod) {
        std::printf("[FAIL] LoadLibrary: %lu\n", GetLastError());
        return 1;
    }
    auto get_info = reinterpret_cast<ChipLibGetInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibGetInfo")));
    auto connect_fn = reinterpret_cast<ChipLibConnectFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibConnect")));
    auto disconnect_fn = reinterpret_cast<ChipLibDisconnectFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibDisconnect")));
    auto query_fn = reinterpret_cast<ChipLibQueryFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibQuery")));
    auto fetch_fn = reinterpret_cast<ChipLibFetchFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibFetch")));
    auto free_buf = reinterpret_cast<ChipLibFreeBufferFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipLibFreeBuffer")));

    check(get_info && connect_fn && disconnect_fn && query_fn && fetch_fn &&
              free_buf,
          "экспорты присутствуют");

    ChipLibInfo info{};
    info.struct_size = sizeof(info);
    check(get_info(&info) != 0 && info.api_version == CHP_API_VERSION,
          "GetInfo: успех, версия 1");
    std::printf("       id='%s' flags=0x%X\n", info.id, info.flags);
    check((info.flags & CHP_LIB_TOP) && (info.flags & CHP_LIB_RATINGS) &&
              (info.flags & CHP_LIB_DOWNLOAD),
          "GetInfo: TOP+RATINGS+DOWNLOAD объявлены");
    check(get_info(nullptr) == 0, "GetInfo: null -> 0");

    ChipError err{};
    ChipHandle h = nullptr;
    check(connect_fn(nullptr, &h, &err) != 0 && h, "Connect: сессия создана");
    if (!h) return 1;

    volatile int cancel = 0;

    // TOP: топ по рейтингу.
    {
        Collector c;
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = CHP_QUERY_TOP;
        req.limit = 10;
        check(query_fn(h, &req, Collector::cb, &c, &cancel, &err) != 0,
              "Query TOP: успех");
        std::printf("       записей получено: %u\n", c.count);
        check(c.count >= 5, "Query TOP: >= 5 записей");
        check(c.have_first && c.first.entry_id[0] != 0,
              "Query TOP: entry_id заполнен");
        check(c.have_first && c.first.rating <= 1000 &&
                  c.first.rating != CHP_LIB_RATING_UNKNOWN,
              "Query TOP: рейтинг нормирован");
    }

    // SEARCH.
    {
        Collector c;
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = CHP_QUERY_SEARCH;
        req.limit = 5;
        req.text = "cat";
        check(query_fn(h, &req, Collector::cb, &c, &cancel, &err) != 0,
              "Query SEARCH: успех");
        std::printf("       найдено: %u\n", c.count);
    }

    // RANDOM + Fetch первой записи.
    {
        Collector c;
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = CHP_QUERY_RANDOM;
        check(query_fn(h, &req, Collector::cb, &c, &cancel, &err) != 0,
              "Query RANDOM: успех");
    }

    // Fetch: без логина BotB отдаёт CHP_ERR_AUTH (документировано);
    // с BOTB_EMAIL/BOTB_PASSWORD в окружении — проверяем и скачивание.
    {
        Collector c;
        ChipLibRequest req{};
        req.struct_size = sizeof(req);
        req.query_type = CHP_QUERY_TOP;
        req.limit = 5;
        if (query_fn(h, &req, Collector::cb, &c, &cancel, &err) != 0 &&
            c.have_first && c.first.download_hint[0] == '/') {
            uint8_t* data = nullptr;
            size_t size = 0;
            const bool ok = fetch_fn(h, &c.first, nullptr, nullptr, &cancel,
                                     &data, &size, &err) != 0;
            const bool auth_expected = err.code == CHP_ERR_AUTH;
            check(ok || auth_expected,
                  "Fetch: файл скачан или CHP_ERR_AUTH без логина");
            if (ok) {
                std::printf("       размер: %zu байт, первые байты: "
                            "%02X %02X %02X %02X\n",
                            size, data[0], data[1], data[2], data[3]);
                check(size > 512, "Fetch: размер правдоподобный");
                free_buf(h, data);
            }
        } else {
            std::printf("       (TOP не вернул download_hint, Fetch пропущен)\n");
        }
    }

    disconnect_fn(h);
    std::printf("\n%s: %d неудачных проверок\n",
                g_failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ПРОШЛО", g_failures);
    FreeLibrary(mod);
    return g_failures;
}
