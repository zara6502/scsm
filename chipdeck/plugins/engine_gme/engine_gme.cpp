// Движок ChipDeck на libgme 0.6.5: ZX Spectrum AY (.ay) и Atari POKEY
// (.sap). Pull-модель рендера в int16 (GME выдаёт только short).
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "gme/gme.h"

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
    return fmt && (std::strcmp(fmt, "AY") == 0 || std::strcmp(fmt, "SAP") == 0 ||
                   std::strcmp(fmt, "NSF") == 0 ||
                   std::strcmp(fmt, "NSFE") == 0);
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
                  "GME (ZX AY / Atari POKEY)");
    // .emul — вариант именования .ay на ModLand; Probe распознаёт по
    // содержимому (ZXAYEMUL), расширение влияет только на фильтр диалога.
    std::snprintf(info->extensions, sizeof(info->extensions), "%s",
                  "ay;emul;sap");
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
