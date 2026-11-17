#include "library_manager.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace host {

namespace {

void* proc(HMODULE m, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(m, name));
}

template <class T>
T proc_as(HMODULE m, const char* name) {
    return reinterpret_cast<T>(proc(m, name));
}

// Простой key=value ini (UTF-8).
std::string read_ini_value(const std::wstring& path, const std::string& key) {
    std::ifstream f(path.c_str());
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        // тримминг
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t\r");
        const std::string t = line.substr(first, last - first + 1);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = t.substr(0, eq);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
            k.pop_back();
        if (k == key) {
            std::string v = t.substr(eq + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                v.erase(v.begin());
            return v;
        }
    }
    return "";
}

} // namespace

size_t LibraryManager::load_directory(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = dir + L"\\*.dll";
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;

    std::vector<std::wstring> files;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(files.begin(), files.end());

    for (const std::wstring& path : files) {
        HMODULE mod = LoadLibraryW(path.c_str());
        if (!mod) continue;

        auto get_info =
            proc_as<ChipLibGetInfoFn>(mod, "ChipLibGetInfo");
        LibModule m;
        m.mod = mod;
        m.schema_fn =
            proc_as<ChipLibGetConfigSchemaFn>(mod, "ChipLibGetConfigSchema");
        m.connect_fn = proc_as<ChipLibConnectFn>(mod, "ChipLibConnect");
        m.disconnect_fn =
            proc_as<ChipLibDisconnectFn>(mod, "ChipLibDisconnect");
        m.query_fn = proc_as<ChipLibQueryFn>(mod, "ChipLibQuery");
        m.fetch_fn = proc_as<ChipLibFetchFn>(mod, "ChipLibFetch");
        m.free_buffer =
            proc_as<ChipLibFreeBufferFn>(mod, "ChipLibFreeBuffer");

        if (!get_info || !m.connect_fn || !m.disconnect_fn || !m.query_fn ||
            !m.fetch_fn || !m.free_buffer)
            continue;

        m.info.struct_size = sizeof(m.info);
        if (!get_info(&m.info) || m.info.api_version != CHP_API_VERSION)
            continue;

        bool dup = false;
        for (const auto& l : libs_)
            if (std::strcmp(l.info.id, m.info.id) == 0) dup = true;
        if (dup) continue;

        // Настройки из <id>.ini (если схема есть).
        if (m.schema_fn) {
            ChipLibConfigSchema schema{};
            schema.struct_size = sizeof(schema);
            if (m.schema_fn(&schema)) {
                const std::wstring ini = dir + L"\\" +
                                         utf8_to_wide(m.info.id) + L".ini";
                for (uint32_t i = 0; i < schema.param_count &&
                                     i < 16; ++i) {
                    const ChipLibParamSchema& ps = schema.params[i];
                    ChipLibStringParam p{};
                    std::snprintf(p.id, sizeof(p.id), "%s", ps.id);
                    // значение из ini; иначе default из схемы
                    std::string v = read_ini_value(ini, ps.id);
                    if (v.empty()) v = ps.default_value;
                    std::snprintf(p.value, sizeof(p.value), "%s", v.c_str());
                    m.params.push_back(p);
                }
                m.cfg.struct_size = sizeof(m.cfg);
                m.cfg.param_count = uint32_t(m.params.size());
                m.cfg.params = m.params.data();
            }
        }

        libs_.push_back(m);
    }
    return libs_.size();
}

bool LibraryManager::connect_one(LibModule& m) {
    if (m.session) return true;
    ChipError err{};
    if (!m.connect_fn(&m.cfg, &m.session, &err)) {
        m.session = nullptr;
        return false;
    }
    return true;
}

size_t LibraryManager::connect_all() {
    size_t ok = 0;
    for (auto& m : libs_)
        if (connect_one(m)) ++ok;
    return ok;
}

void LibraryManager::disconnect_all() {
    for (auto& m : libs_) {
        if (m.session) {
            m.disconnect_fn(m.session);
            m.session = nullptr;
        }
    }
}

} // namespace host
