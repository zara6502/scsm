// Минимальная заглушка движка ChipDeck: проверяет ABI-контракт GetInfo/Probe.
// Никакого декодирования: распознаёт только тестовую магию "CHPSTUB1".
// См. ../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr char kMagic[8] = {'C', 'H', 'P', 'S', 'T', 'U', 'B', '1'};

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "stub");
    std::snprintf(info->name, sizeof(info->name), "%s", "ChipDeck Stub");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "stub");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(kMagic)) return 0;
    return std::memcmp(data, kMagic, sizeof(kMagic)) == 0 ? 1u : 0u;
}

} // extern "C"
