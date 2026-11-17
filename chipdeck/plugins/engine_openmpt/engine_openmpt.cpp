// Движок ChipDeck на libopenmpt 0.8.10: трекерная музыка MOD/S3M/XM/IT
// и ещё два десятка форматов. libopenmpt копирует входной буфер, поэтому
// сессия хранит только указатель на модуль.
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <libopenmpt/libopenmpt.h>

namespace {

constexpr uint32_t kErrOpenmpt = 16u; // внутренняя ошибка libopenmpt
constexpr size_t kMaxFileSize = 256u * 1024 * 1024;
constexpr uint32_t kDefaultRate = 44100;

void set_error(ChipError* err, uint32_t code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->reserved = 0;
    if (msg) std::snprintf(err->message, sizeof(err->message), "%s", msg);
    else err->message[0] = '\0';
}

std::string module_meta(openmpt_module* mod, const char* key) {
    const char* v = openmpt_module_get_metadata(mod, key);
    return v ? v : "";
}

struct Session {
    openmpt_module* mod = nullptr;
    uint32_t rate = kDefaultRate;
};

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "openmpt");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "OpenMPT (MOD/S3M/XM/IT)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s",
                  "mod;s3m;xm;it;mtm;mo3;okt;ult;669;far;mdz;ptm;sfx");
    return 1;
}

// libopenmpt сам распознаёт все поддерживаемые форматы по заголовку —
// используем его штатный probe (рекомендованный размер байт).
CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size == 0) return 0;
    const size_t need = openmpt_probe_file_header_get_recommended_size();
    if (size < need) return 0;
    const int r = openmpt_probe_file_header(
        OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT, data, size, size, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    return r == OPENMPT_PROBE_FILE_HEADER_RESULT_SUCCESS ? 1u : 0u;
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not a tracker module");
        return 0;
    }

    openmpt_module* mod = openmpt_module_create_from_memory2(
        data, size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr);
    if (!mod) {
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "libopenmpt failed to load");
        return 0;
    }
    Session* s = new Session();
    s->mod = mod;
    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle) {
    if (!handle) return;
    Session* s = static_cast<Session*>(handle);
    openmpt_module_destroy(s->mod);
    delete s;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !count) return 0;
    openmpt_module* mod = s->mod;
    const int32_t n = openmpt_module_get_num_subsongs(mod);
    *count = n > 0 ? uint32_t(n) : 1u;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineSelect(ChipHandle handle,
                                              uint32_t track_index,
                                              uint32_t sample_rate,
                                              ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s || sample_rate < 8000 || sample_rate > 192000) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad handle or sample rate");
        return 0;
    }
    openmpt_module* mod = s->mod;
    s->rate = sample_rate;

    const int32_t n = openmpt_module_get_num_subsongs(mod);
    if (n > 0 && int32_t(track_index) >= n) {
        set_error(err, CHP_ERR_NOT_FOUND, "track index out of range");
        return 0;
    }
    if (n > 1 && !openmpt_module_select_subsong(mod, int32_t(track_index))) {
        set_error(err, CHP_ERR_NOT_FOUND, "select_subsong failed");
        return 0;
    }
    openmpt_module_set_position_seconds(mod, 0.0);
    return 1;
}

// Конец трека = меньше кадров, чем запрошено (контракт ABI).
CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderInt16(ChipHandle handle,
                                                   int16_t* buffer,
                                                   uint32_t frames) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !buffer) return 0;
    const size_t got = openmpt_module_read_interleaved_stereo(
        s->mod, int32_t(s->rate), frames, buffer);
    return uint32_t(got);
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetCapabilities(
    ChipEngineCapabilities* caps) {
    if (!caps || caps->struct_size < sizeof(*caps)) return 0;
    const uint32_t caller_size = caps->struct_size;
    std::memset(caps, 0, sizeof(*caps));
    caps->struct_size = caller_size;
    caps->version = CHP_API_VERSION;
    caps->flags = CHP_ENGINE_INT16 | CHP_ENGINE_SEEK;
    caps->voice_count = 0;
    caps->spectrum_bands = 0;
    caps->max_tracks = 0;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackInfo(ChipHandle handle,
                                                    uint32_t track_index,
                                                    ChipTrackInfo* info) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !info || info->struct_size < sizeof(*info)) return 0;
    openmpt_module* mod = s->mod;

    const int32_t n = openmpt_module_get_num_subsongs(mod);
    if (n > 0 && int32_t(track_index) >= n) return 0;

    // Метаданные относятся к текущему подсонгу: переключаемся без
    // побочных эффектов для вызывающей стороны (позиция сбрасывается,
    // но host всегда делает Select перед Render).
    if (n > 1) openmpt_module_select_subsong(mod, int32_t(track_index));

    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->track_index = track_index;

    const std::string title = module_meta(mod, "title");
    if (!title.empty()) {
        std::snprintf(info->title, sizeof(info->title), "%s", title.c_str());
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    const std::string artist = module_meta(mod, "artist");
    if (!artist.empty()) {
        std::snprintf(info->author, sizeof(info->author), "%s",
                      artist.c_str());
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
    std::string type = module_meta(mod, "type_long");
    if (type.empty()) type = module_meta(mod, "type");
    std::snprintf(info->format, sizeof(info->format), "%s",
                  type.empty() ? "Tracker" : type.c_str());

    const double dur = openmpt_module_get_duration_seconds(mod);
    if (dur > 0.0) {
        info->duration_ms = uint32_t(dur * 1000.0);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }

    if (n > 1) openmpt_module_select_subsong(mod, 0);
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineSeek(ChipHandle handle,
                                            uint64_t sample_position,
                                            ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s) {
        set_error(err, CHP_ERR_INVALID_ARG, "no session");
        return 0;
    }
    const double seconds = double(sample_position) / double(s->rate);
    openmpt_module_set_position_seconds(s->mod, seconds);
    return 1;
}

} // extern "C"
