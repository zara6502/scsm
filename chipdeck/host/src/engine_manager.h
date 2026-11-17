// Реестр движков: сканирует каталог, грузит DLL ChipEngine, даёт Probe.
#pragma once

#include "common.h"
#include "chipdeck_api.h"

namespace host {

struct EngineModule {
    HMODULE mod = nullptr;
    ChipEngineInfo info{};

    ChipEngineProbeFn probe = nullptr;
    ChipEngineOpenFn open_fn = nullptr;
    ChipEngineCloseFn close_fn = nullptr;
    ChipEngineGetTrackCountFn track_count = nullptr;
    ChipEngineSelectFn select_fn = nullptr;
    ChipEngineRenderInt16Fn render_i16 = nullptr;
    ChipEngineGetTrackInfoFn track_info = nullptr;
    ChipEngineSeekFn seek_fn = nullptr;
    ChipEngineGetCapabilitiesFn caps_fn = nullptr;
};

class EngineManager {
public:
    // Загружает все *.dll из каталога; возвращает число принятых движков.
    size_t load_directory(const std::wstring& dir);

    size_t count() const { return engines_.size(); }
    const EngineModule& at(size_t i) const { return engines_[i]; }

    // Ищет движок по содержимому (Probe в порядке загрузки). nullptr, если
    // никто не распознал.
    const EngineModule* find_for_content(const uint8_t* data,
                                         size_t size) const;

    // Строка фильтра для GetOpenFileNameW: "Чиптюн\0*.ay;*.sap\0...\0\0".
    std::wstring open_dialog_filter() const;

private:
    std::vector<EngineModule> engines_;
};

} // namespace host
