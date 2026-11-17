// Минимальный тест заглушки движка: грузит DLL как host и проверяет
// контракт GetInfo/Probe (первую часть ABI). Код возврата = число ошибок.

#include "chipdeck_api.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: stub_test <engine_stub.dll>\n");
        return 1;
    }

    HMODULE mod = LoadLibraryA(argv[1]);
    if (!mod) {
        std::printf("[FAIL] LoadLibrary: %lu\n", GetLastError());
        return 1;
    }

    auto get_info = reinterpret_cast<ChipEngineGetInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineGetInfo")));
    auto probe = reinterpret_cast<ChipEngineProbeFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineProbe")));

    check(get_info && probe, "экспорты GetInfo и Probe присутствуют");

    ChipEngineInfo info{};
    info.struct_size = sizeof(info);
    check(get_info && get_info(&info) != 0, "GetInfo: успех");
    check(info.api_version == CHP_API_VERSION, "GetInfo: api_version == 1");
    check(std::strcmp(info.id, "stub") == 0, "GetInfo: id == 'stub'");
    check(info.name[0] != 0 && info.extensions[0] != 0,
          "GetInfo: строки непустые");

    const uint8_t magic[] = {'C', 'H', 'P', 'S', 'T', 'U', 'B', '1'};
    check(probe && probe(magic, sizeof(magic)) != 0, "Probe: магия распознана");
    check(probe && probe(magic, 7) == 0, "Probe: усечённая магия отклонена");
    check(probe && probe(nullptr, 8) == 0, "Probe: null отклонён");
    const uint8_t garbage[16] = {0};
    check(probe && probe(garbage, sizeof(garbage)) == 0, "Probe: нули -> 0");

    FreeLibrary(mod);
    std::printf("\n%s: %d неудачных проверок\n",
                g_failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ПРОШЛО", g_failures);
    return g_failures;
}
