// Движок ChipDeck на libgme 0.6.5: ZX Spectrum AY (.ay) и Atari POKEY
// (.sap). Pull-модель рендера в int16 (GME выдаёт только short).
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "gme/gme.h"

#include <zlib.h> // распаковка пакованного GYM (GYMX + deflate)

namespace {

// Коды ошибок движка gme (>= 16, базовые CHP_ERR_* не пересекаются).
constexpr uint32_t GME_ERR_ENGINE = 16u; // внутренняя ошибка libgme

// Ограничение размера входного файла (гигантских VGM-подобных нет, но
// защититься стоит).
constexpr size_t kMaxFileSize = 64u * 1024 * 1024;

// Частота по умолчанию для сессии, открытой без Select.
constexpr uint32_t kDefaultRate = 44100;

struct Session {
    std::vector<uint8_t> data; // копия файла: gme переоткрывает на Select
    Music_Emu* emu = nullptr;
    uint32_t track = 0;
    uint32_t rate = 0;
    char format[8] = {0}; // "AY" или "SAP" из gme_identify_header
};

void set_error(ChipError* err, uint32_t code, const char* message) {
    if (!err) return;
    err->code = code;
    err->reserved = 0;
    if (message) {
        std::snprintf(err->message, sizeof(err->message), "%s", message);
    } else {
        err->message[0] = '\0';
    }
}

void close_session(Session* s) {
    if (s && s->emu) {
        gme_delete(s->emu);
        s->emu = nullptr;
    }
}

// Форматы, которые берёт на себя этот движок (остальные форматы GME
// отключены в сборке и должны оставаться чужим полем деятельности).
bool format_supported(const char* fmt) {
    return fmt &&
           (std::strcmp(fmt, "AY") == 0 || std::strcmp(fmt, "SAP") == 0 ||
            std::strcmp(fmt, "NSF") == 0 || std::strcmp(fmt, "NSFE") == 0 ||
            std::strcmp(fmt, "SPC") == 0 || std::strcmp(fmt, "VGM") == 0 ||
            std::strcmp(fmt, "VGZ") == 0 ||
            std::strcmp(fmt, "GYM") == 0 || std::strcmp(fmt, "HES") == 0 ||
            std::strcmp(fmt, "KSS") == 0 || std::strcmp(fmt, "GBS") == 0);
}

// Пакованный GYM: "GYMX" + 428-байтовый заголовок + zlib-поток. libgme
// такие не берёт — распаковываем deflate и пересобираем как непакованный
// GYMX (тот же заголовок с нулевым флагом packed + сырой GYM-поток),
// чтобы сработал и gme_open_data, и метаданные из заголовка.
constexpr size_t kGymHeaderSize = 428;
constexpr size_t kGymPackedOffset = 424; // packed[4] в конце заголовка

bool unpack_packed_gym(std::vector<uint8_t>& data) {
    if (data.size() <= kGymHeaderSize + 2) return false;
    if (std::memcmp(data.data(), "GYMX", 4) != 0) return false;
    // флаг packed != 0 и zlib-сигнатура сразу после заголовка
    if (data[kGymPackedOffset] == 0 && data[kGymPackedOffset + 1] == 0 &&
        data[kGymPackedOffset + 2] == 0 && data[kGymPackedOffset + 3] == 0)
        return false; // не пакованный — gme сам его съест

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in = data.data() + kGymHeaderSize;
    zs.avail_in = uInt(data.size() - kGymHeaderSize);

    std::vector<uint8_t> out;
    uint8_t chunk[65536];
    int rc = Z_OK;
    do {
        zs.next_out = chunk;
        zs.avail_out = sizeof(chunk);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        out.insert(out.end(), chunk, chunk + (sizeof(chunk) - zs.avail_out));
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);

    if (out.empty() || out[0] > 3) return false; // не похоже на GYM-поток
    // Пересобираем как непакованный GYMX: исходный заголовок с packed=0.
    std::vector<uint8_t> packed_zero(data.begin(),
                                     data.begin() + kGymHeaderSize);
    packed_zero[kGymPackedOffset] = 0;
    packed_zero[kGymPackedOffset + 1] = 0;
    packed_zero[kGymPackedOffset + 2] = 0;
    packed_zero[kGymPackedOffset + 3] = 0;
    packed_zero.insert(packed_zero.end(), out.begin(), out.end());
    data.swap(packed_zero);
    return true;
}

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "gme");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "GME (ZX AY / Atari POKEY / NES / SNES / Sega / PC Engine / MSX / GB)");
    // .emul — вариант именования .ay на ModLand; Probe распознаёт по
    // содержимому (ZXAYEMUL), расширение влияет только на фильтр диалога.
    std::snprintf(info->extensions, sizeof(info->extensions), "%s",
                  "ay;emul;sap;nsf;nsfe;spc;vgm;vgz;gym;hes;kss;gbs");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size < 8) return 0;
    return format_supported(gme_identify_header(data)) ? 1u : 0u;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineOpen(const uint8_t* data, size_t size,
                                            ChipHandle* out, ChipError* err) {
    if (!out) return 0;
    *out = nullptr;
    if (!data || size == 0 || size > kMaxFileSize) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad file buffer");
        return 0;
    }
    if (!ChipEngineProbe(data, size)) {
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not an AY/SAP file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size);

    const char* fmt = gme_identify_header(s->data.data());
    std::snprintf(s->format, sizeof(s->format), "%s", fmt ? fmt : "");

    // Открываем emu сразу: GetTrackCount/GetTrackInfo должны работать
    // до первого Select. Select при смене частоты переоткроет.
    unpack_packed_gym(s->data); // пакованный GYM -> сырой поток
    gme_err_t e = gme_open_data(s->data.data(), long(s->data.size()), &s->emu,
                                kDefaultRate);
    if (e) {
        s->emu = nullptr;
        set_error(err, GME_ERR_ENGINE, e);
        delete s;
        return 0;
    }
    s->rate = kDefaultRate;

    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle) {
    if (!handle) return;
    Session* s = static_cast<Session*>(handle);
    close_session(s);
    delete s;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !count) return 0;
    *count = s->emu ? uint32_t(gme_track_count(s->emu)) : 0u;
    return 1;
}

// Выбор трека; при смене частоты — переоткрытие (gme задаёт
// sample_rate при открытии).
CHP_EXPORT uint32_t CHP_CALL ChipEngineSelect(ChipHandle handle,
                                              uint32_t track_index,
                                              uint32_t sample_rate,
                                              ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s || s->data.empty() || sample_rate < 8000 ||
        sample_rate > 192000) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad handle or sample rate");
        return 0;
    }

    if (sample_rate != s->rate) {
        close_session(s);
        gme_err_t e = gme_open_data(s->data.data(), long(s->data.size()),
                                    &s->emu, int(sample_rate));
        if (e) {
            s->emu = nullptr;
            set_error(err, GME_ERR_ENGINE, e);
            return 0;
        }
        s->rate = sample_rate;
    }

    const int tracks = gme_track_count(s->emu);
    if (tracks <= 0 || int(track_index) >= tracks) {
        set_error(err, CHP_ERR_NOT_FOUND, "track index out of range");
        return 0;
    }

    gme_err_t e = gme_start_track(s->emu, int(track_index));
    if (e) {
        set_error(err, GME_ERR_ENGINE, e);
        return 0;
    }

    s->track = track_index;
    return 1;
}

// Возвращает число кадров; меньше запрошенного (в т.ч. 0) = конец трека.
CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderInt16(ChipHandle handle,
                                                   int16_t* buffer,
                                                   uint32_t frames) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->emu || !buffer) return 0; // "конец" при некорректном вызове

    if (gme_track_ended(s->emu)) return 0;

    gme_err_t e = gme_play(s->emu, int(frames) * 2, buffer);
    if (e) return 0; // ошибка рендера трактуется как конец потока

    return frames;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetCapabilities(
    ChipEngineCapabilities* caps) {
    if (!caps || caps->struct_size < sizeof(*caps)) return 0;
    const uint32_t caller_size = caps->struct_size;
    std::memset(caps, 0, sizeof(*caps));
    caps->struct_size = caller_size;
    caps->version = CHP_API_VERSION;
    caps->flags = CHP_ENGINE_INT16 | CHP_ENGINE_SEEK;
    caps->voice_count = 0;  // gme не отдаёт уровни голосов
    caps->spectrum_bands = 0;
    caps->max_tracks = 0;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackInfo(ChipHandle handle,
                                                    uint32_t track_index,
                                                    ChipTrackInfo* info) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->emu || !info || info->struct_size < sizeof(*info)) return 0;

    gme_info_t* gi = nullptr;
    if (gme_track_info(s->emu, &gi, int(track_index))) return 0;

    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->track_index = track_index;

    if (gi->song && gi->song[0]) {
        std::snprintf(info->title, sizeof(info->title), "%s", gi->song);
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    if (gi->author && gi->author[0]) {
        std::snprintf(info->author, sizeof(info->author), "%s", gi->author);
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
    if (std::strcmp(s->format, "AY") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "AY (ZX Spectrum)");
    } else if (std::strcmp(s->format, "SAP") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "SAP (Atari POKEY)");
    } else if (std::strcmp(s->format, "SPC") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "SPC (SNES)");
    } else if (std::strcmp(s->format, "VGM") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "VGM (arcade/console)");
    } else if (std::strcmp(s->format, "GYM") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "GYM (Sega Mega Drive)");
    } else if (std::strcmp(s->format, "HES") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "HES (PC Engine)");
    } else if (std::strcmp(s->format, "KSS") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "KSS (MSX)");
    } else if (std::strcmp(s->format, "GBS") == 0) {
        std::snprintf(info->format, sizeof(info->format), "%s",
                      "GBS (Game Boy)");
    } else {
        std::snprintf(info->format, sizeof(info->format), "%s", "NSF (NES)");
    }
    if (gi->play_length > 0) {
        info->duration_ms = uint32_t(gi->play_length);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }

    gme_free_info(gi);
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineSeek(ChipHandle handle,
                                            uint64_t sample_position,
                                            ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->emu || !s->rate) {
        set_error(err, CHP_ERR_INVALID_ARG, "no active track");
        return 0;
    }
    if (sample_position > 0x7FFFFFFFull) {
        set_error(err, CHP_ERR_INVALID_ARG, "position too large");
        return 0;
    }
    gme_err_t e = gme_seek_samples(s->emu, long(sample_position));
    if (e) {
        set_error(err, GME_ERR_ENGINE, e);
        return 0;
    }
    return 1;
}

} // extern "C"
