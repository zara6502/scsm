// Реестр библиотечных плагинов: Libraries/*.dll + настройки из <id>.ini.
// Формат ini: строки key=value; ключи — ID параметров из схемы плагина
// (например email=... password=... для BotB).
#pragma once

#include "common.h"
#include "chipdeck_api.h"

namespace host {

struct LibModule {
    HMODULE mod = nullptr;
    ChipLibInfo info{};

    ChipLibGetConfigSchemaFn schema_fn = nullptr;
    ChipLibConnectFn connect_fn = nullptr;
    ChipLibDisconnectFn disconnect_fn = nullptr;
    ChipLibQueryFn query_fn = nullptr;
    ChipLibFetchFn fetch_fn = nullptr;
    ChipLibFreeBufferFn free_buffer = nullptr;

    ChipHandle session = nullptr;    // подключение (после connect_all)
    ChipLibConnectConfig cfg{};
    std::vector<ChipLibStringParam> params;
};

class LibraryManager {
public:
    // Загружает DLL и настройки из каталога (dir/<id>.ini).
    size_t load_directory(const std::wstring& dir);

    // Подключает все плагины (с настройками из ini). Возвращает число
    // успешно подключённых.
    size_t connect_all();

    void disconnect_all();

    size_t count() const { return libs_.size(); }
    const LibModule& at(size_t i) const { return libs_[i]; }
    LibModule& mutable_at(size_t i) { return libs_[i]; }

private:
    bool connect_one(LibModule& m);

    std::vector<LibModule> libs_;
};

} // namespace host
