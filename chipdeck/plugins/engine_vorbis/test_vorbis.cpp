// Тест движка stb_vorbis: грузит DLL как host, проверяет контракт ChipEngine
// на реальных файлах .ay/.sap, пишет WAV-дамп первых секунд звука.
//
// vb_test <engine_vorbis.dll> <файл.ay или .sap> [out.wav]

#include "chipdeck_api.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(size_t(n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

void put_le32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

void put_le16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

// 16-бит стерео PCM WAV.
bool write_wav(const char* path, const std::vector<int16_t>& pcm,
               uint32_t rate) {
    const size_t data_size = pcm.size() * 2;
    std::vector<uint8_t> wav(44 + data_size, 0);
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    put_le32(wav.data() + 4, uint32_t(36 + data_size));
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    put_le32(wav.data() + 16, 16);
    put_le16(wav.data() + 20, 1);           // PCM
    put_le16(wav.data() + 22, 2);           // stereo
    put_le32(wav.data() + 24, rate);
    put_le32(wav.data() + 28, rate * 4);    // byte rate
    put_le16(wav.data() + 32, 4);           // block align
    put_le16(wav.data() + 34, 16);          // bits
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    put_le32(wav.data() + 40, uint32_t(data_size));
    std::memcpy(wav.data() + 44, pcm.data(), data_size);

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(wav.data(), 1, wav.size(), f) == wav.size();
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // видеть вывод при падении
    if (argc < 3) {
        std::printf("usage: vb_test <engine_vorbis.dll> <track.ogg> [out.wav]\n");
        return 1;
    }
    const char* dll_path = argv[1];
    const char* track_path = argv[2];
    const char* wav_path = argc > 3 ? argv[3] : "vb_test_out.wav";
    constexpr uint32_t kRate = 44100;

    HMODULE mod = LoadLibraryA(dll_path);
    if (!mod) {
        std::printf("[FAIL] LoadLibrary: %lu\n", GetLastError());
        return 1;
    }

    auto get_info = reinterpret_cast<ChipEngineGetInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineGetInfo")));
    auto probe = reinterpret_cast<ChipEngineProbeFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineProbe")));
    auto open_fn = reinterpret_cast<ChipEngineOpenFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineOpen")));
    auto close_fn = reinterpret_cast<ChipEngineCloseFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineClose")));
    auto track_count = reinterpret_cast<ChipEngineGetTrackCountFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineGetTrackCount")));
    auto select_fn = reinterpret_cast<ChipEngineSelectFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineSelect")));
    auto render_i16 = reinterpret_cast<ChipEngineRenderInt16Fn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineRenderInt16")));
    auto caps_fn = reinterpret_cast<ChipEngineGetCapabilitiesFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineGetCapabilities")));
    auto info_fn = reinterpret_cast<ChipEngineGetTrackInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineGetTrackInfo")));
    auto seek_fn = reinterpret_cast<ChipEngineSeekFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "ChipEngineSeek")));

    check(get_info && probe && open_fn && close_fn && track_count &&
              select_fn && render_i16 && caps_fn && info_fn && seek_fn,
          "все 10 экспортов присутствуют");

    ChipEngineInfo ei{};
    ei.struct_size = sizeof(ei);
    check(get_info(&ei) != 0 && ei.api_version == CHP_API_VERSION,
          "GetInfo: успех, версия 1");
    std::printf("       id='%s' name='%s' exts='%s'\n", ei.id, ei.name,
                ei.extensions);

    ChipEngineCapabilities caps{};
    caps.struct_size = sizeof(caps);
    check(caps_fn(&caps) != 0 && (caps.flags & CHP_ENGINE_INT16) != 0 &&
              (caps.flags & CHP_ENGINE_FLOAT32) == 0,
          "GetCapabilities: INT16 без FLOAT32");
    check(get_info(nullptr) == 0 && caps_fn(nullptr) == 0,
          "GetInfo/Capabilities: null отклоняются");

    std::vector<uint8_t> file;
    check(read_file(track_path, file), "чтение трека");
    if (file.empty()) return 1;

    check(probe(file.data(), file.size()) != 0, "Probe: трек распознан");
    check(probe(file.data(), 4) == 0, "Probe: усечённый ввод -> 0");
    std::vector<uint8_t> garbage(2048, 0x5A);
    check(probe(garbage.data(), garbage.size()) == 0, "Probe: мусор -> 0");

    ChipError err{};
    ChipHandle h = nullptr;
    check(open_fn(file.data(), file.size(), &h, &err) != 0 && h,
          "Open: сессия создана");
    if (!h) return 1;

    // Мусорный Open — на отдельной переменной, чтобы не затереть h.
    ChipHandle h_bad = reinterpret_cast<ChipHandle>(1);
    check(open_fn(garbage.data(), garbage.size(), &h_bad, &err) == 0 &&
              h_bad == nullptr && err.code == CHP_ERR_NOT_RECOGNIZED,
          "Open: мусор -> CHP_ERR_NOT_RECOGNIZED, handle null");
    check(open_fn(nullptr, 0, &h_bad, &err) == 0 && h_bad == nullptr,
          "Open: null -> ошибка, handle null");

    // Сессия до Select: счётчик подтюнов и метаданные доступны сразу.
    uint32_t n_tracks = 0;
    check(track_count(h, &n_tracks) != 0 && n_tracks >= 1,
          "GetTrackCount без Select: >= 1");
    std::printf("       подтюнов: %u\n", n_tracks);

    ChipTrackInfo ti{};
    ti.struct_size = sizeof(ti);
    check(info_fn(h, 0, &ti) != 0, "GetTrackInfo: успех");
    std::printf("       title='%s' author='%s' fmt='%s' dur=%u flags=0x%X\n",
                ti.title, ti.author, ti.format, ti.duration_ms, ti.flags);
    check(info_fn(h, n_tracks, &ti) == 0, "GetTrackInfo: неверный индекс -> 0");
    check(info_fn(h, 0, nullptr) == 0, "GetTrackInfo: null -> 0");

    check(select_fn(h, 0, kRate, &err) != 0, "Select: трек 0 @44100");
    check(select_fn(h, n_tracks + 5, kRate, &err) == 0 && err.code != 0,
          "Select: неверный трек -> ошибка");
    check(select_fn(h, 0, kRate, &err) != 0, "Select: повтор после ошибки");

    // Рендер 5 секунд с проверкой энергии сигнала.
    constexpr uint32_t kFrames = kRate * 5;
    std::vector<int16_t> pcm(size_t(kFrames) * 2);
    uint32_t rendered = 0;
    bool non_silent = false;
    while (rendered < kFrames) {
        const uint32_t chunk = (kFrames - rendered < 2048)
                                   ? (kFrames - rendered) : 2048;
        const uint32_t got = render_i16(h, pcm.data() + size_t(rendered) * 2,
                                        chunk);
        for (size_t i = 0; i < size_t(got) * 2; i += 97) {
            if (std::abs(pcm[size_t(rendered) * 2 + i]) > 256) non_silent = true;
        }
        rendered += got;
        if (got == 0) break; // конец трека раньше 5 секунд — допустимо
    }
    std::printf("       отрендерено: %u кадров, не-тишина: %s\n", rendered,
                non_silent ? "да" : "нет");
    check(rendered > kRate, "Render: больше секунды звука");
    check(non_silent, "Render: сигнал не нулевой");

    // Seek в середину и продолжение рендера.
    if (rendered > kRate * 2) {
        check(seek_fn(h, uint64_t(kRate), &err) != 0, "Seek: к 1 секунде");
        std::vector<int16_t> after(size_t(kRate) * 2);
        const uint32_t got = render_i16(h, after.data(), kRate);
        check(got == kRate, "Render после Seek: полный буфер");
    } else {
        std::printf("       (трек короче 2с, Seek-проверка рендера пропущена)\n");
    }

    // Ресемплинг: выбрать 48 кГц на 44.1 кГц файле и отрендерить секунду.
    {
        check(select_fn(h, 0, 48000, &err) != 0, "Select: частота 48000");
        std::vector<int16_t> rs(size_t(kRate) * 2);
        uint32_t got = render_i16(h, rs.data(), kRate);
        bool rs_non_silent = false;
        for (size_t i = 0; i < rs.size(); i += 97) {
            if (std::abs(rs[i]) > 256) rs_non_silent = true;
        }
        std::printf("       @48кГц: %u кадров, не-тишина: %s\n", got,
                    rs_non_silent ? "да" : "нет");
        check(got > kRate / 2, "Render @48кГц: ресемплинг даёт звук");
        check(rs_non_silent, "Render @48кГц: сигнал не нулевой");
    }

    check(write_wav(wav_path, pcm, kRate), "запись WAV для прослушивания");

    // Циклы open/close.
    for (int i = 0; i < 3; ++i) {
        ChipHandle h2 = nullptr;
        if (!open_fn(file.data(), file.size(), &h2, &err) || !h2) {
            check(false, "цикл Open/Close");
            break;
        }
        select_fn(h2, 0, kRate, &err);
        render_i16(h2, pcm.data(), 1000);
        close_fn(h2);
    }
    check(true, "3 цикла Open/Select/Render/Close");
    close_fn(nullptr); // null безопасен

    close_fn(h);
    std::printf("\n%s: %d неудачных проверок\n",
                g_failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ПРОШЛО", g_failures);
    FreeLibrary(mod);
    return g_failures;
}
