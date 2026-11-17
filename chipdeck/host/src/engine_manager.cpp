#include "engine_manager.h"

#include <algorithm>
#include <cstring>

namespace host {

namespace {

void* proc(HMODULE m, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(m, name));
}

template <class T>
T proc_as(HMODULE m, const char* name) {
    return reinterpret_cast<T>(proc(m, name));
}

} // namespace

size_t EngineManager::load_directory(const std::wstring& dir) {
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
    std::sort(files.begin(), files.end()); // детерминированный порядок

    for (const std::wstring& path : files) {
        HMODULE mod = LoadLibraryW(path.c_str());
        if (!mod) continue;

        EngineModule em;
        em.mod = mod;
        em.probe = proc_as<ChipEngineProbeFn>(mod, "ChipEngineProbe");
        em.open_fn = proc_as<ChipEngineOpenFn>(mod, "ChipEngineOpen");
        em.close_fn = proc_as<ChipEngineCloseFn>(mod, "ChipEngineClose");
        em.track_count =
            proc_as<ChipEngineGetTrackCountFn>(mod, "ChipEngineGetTrackCount");
        em.select_fn =
            proc_as<ChipEngineSelectFn>(mod, "ChipEngineSelect");
        em.render_i16 = proc_as<ChipEngineRenderInt16Fn>(
            mod, "ChipEngineRenderInt16");
        em.track_info =
            proc_as<ChipEngineGetTrackInfoFn>(mod, "ChipEngineGetTrackInfo");
        em.seek_fn = proc_as<ChipEngineSeekFn>(mod, "ChipEngineSeek");
        em.caps_fn = proc_as<ChipEngineGetCapabilitiesFn>(
            mod, "ChipEngineGetCapabilities");

        auto get_info =
            proc_as<ChipEngineGetInfoFn>(mod, "ChipEngineGetInfo");
        if (!get_info || !em.probe || !em.open_fn || !em.close_fn ||
            !em.track_count || !em.select_fn || !em.render_i16 ||
            !em.track_info || !em.caps_fn)
            continue; // неполный движок — пропускаем

        em.info.struct_size = sizeof(em.info);
        if (!get_info(&em.info) || em.info.api_version != CHP_API_VERSION)
            continue;

        // Стабильный ID: повторные движки не регистрируем.
        bool dup = false;
        for (const auto& e : engines_)
            if (std::strcmp(e.info.id, em.info.id) == 0) dup = true;
        if (dup) continue;

        engines_.push_back(em);
    }
    return engines_.size();
}

const EngineModule* EngineManager::find_for_content(const uint8_t* data,
                                                    size_t size) const {
    for (const auto& e : engines_)
        if (e.probe(data, size)) return &e;
    return nullptr;
}

std::wstring EngineManager::open_dialog_filter() const {
    // Один общий фильтр всех расширений + All files; разделители \0.
    // Расширения движков ASCII — собираем посимвольно.
    std::string exts;
    for (const auto& e : engines_) {
        const char* p = e.info.extensions;
        while (*p) {
            if (*p == ';') exts += ";*.";
            else if (*p != ' ') exts += *p;
            ++p;
        }
        exts += ";*.";
    }
    if (exts.size() > 3) exts = "*." + exts.substr(0, exts.size() - 3);

    std::wstring filter;
    for (const char c : std::string("Chiptune (") + exts + ")")
        filter.push_back(wchar_t(c));
    filter.push_back(L'\0');
    for (const char c : exts) filter.push_back(wchar_t(c));
    filter.push_back(L'\0');
    const wchar_t* tail = L"All files\0*.*\0";
    filter.append(tail, wcslen(tail) + 1);
    return filter;
}

} // namespace host
