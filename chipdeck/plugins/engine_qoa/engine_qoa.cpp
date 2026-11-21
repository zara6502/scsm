// Движок ChipDeck на QOA (phoboslab, MIT): lossy-кодек "Quite OK Audio"
// (.qoa). Файл небольшой (коэффициент сжатия ~4:1) — декодируем целиком
// в память при Open; Seek и ресемплинг становятся тривиальными.
// Pull-модель рендера в int16. См. ../../include/chipdeck_api.h.

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#define QOA_IMPLEMENTATION
#define QOA_NO_STDIO
#include "qoa/qoa.h"

namespace {

constexpr uint32_t QOA_ERR_ENGINE = 16u; // внутренняя ошибка qoa
constexpr size_t kMaxFileSize = 64u * 1024 * 1024; // входной .qoa

struct Session {
    std::vector<int16_t> pcm; // декодированный PCM файла (по каналам)
    uint64_t total_frames = 0; // кадров (по каналам)
    uint32_t file_rate = 0;
    uint32_t channels = 0;

    // ресемплинг file_rate -> out_rate, mono -> stereo
    uint32_t out_rate = 0;
    double out_pos = 0.0;
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

// Конвертация + ресемплинг: interleaved stereo int16, число записанных
// кадров может быть меньше запрошенного = конец трека.
uint32_t render(Session* s, int16_t* out, uint32_t count) {
    uint32_t written = 0;
    while (written < count) {
        if (s->out_pos >= double(s->total_frames)) break;
        const uint64_t i0 = uint64_t(s->out_pos);
        const double t = s->out_pos - double(i0);
        const uint64_t i1 = i0 + 1 < s->total_frames ? i0 + 1 : i0;

        for (uint32_t c = 0; c < 2; ++c) {
            const uint32_t src_c = c < s->channels ? c : s->channels - 1;
            const double a = double(s->pcm[i0 * s->channels + src_c]);
            const double b = double(s->pcm[i1 * s->channels + src_c]);
            const double v = a + (b - a) * t;
            const double cl = v < -32768.0 ? -32768.0
                              : v > 32767.0 ? 32767.0
                                            : v;
            out[(size_t)written * 2 + c] =
                int16_t(cl + (cl < 0 ? -0.5 : 0.5));
        }
        ++written;
        s->out_pos += s->out_rate ? double(s->file_rate) / double(s->out_rate)
                                  : 1.0;
    }
    return written;
}

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "qoa");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "QOA (Quite OK Audio)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "qoa");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size < QOA_MIN_FILESIZE) return 0;
    if (std::memcmp(data, "qoaf", 4) != 0) return 0;
    // Разумная проверка заголовка: каналы 1..8, частота в пределах.
    qoa_desc q{};
    if (qoa_decode_header(data, int(size), &q) == 0) return 0;
    if (q.channels < 1 || q.channels > QOA_MAX_CHANNELS ||
        q.samplerate < 8000 || q.samplerate > 192000) return 0;
    return 1;
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not a QOA file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }

    qoa_desc q{};
    qoa_uint64_t pos = 0; // идём по фреймам от начала файла
    // Заголовок файла.
    pos += qoa_decode_header(data, int(size), &q);

    // Пул буферов декодирования.
    const unsigned int max_frame_samples =
        QOA_SLICES_PER_FRAME * QOA_SLICE_LEN;
    std::vector<short> frame_buf(size_t(max_frame_samples) * q.channels);
    const size_t first_reserve = 1024 * 1024;
    s->pcm.reserve(first_reserve);

    bool ok = true;
    while (pos + 8 <= size) {
        // Заголовок фрейма (u64 BE): каналы(8) частота(24) сэмплы(16)
        // размер фрейма(16).
        const qoa_uint64_t fh =
            (qoa_uint64_t)data[pos] << 56 |
            (qoa_uint64_t)data[pos + 1] << 48 |
            (qoa_uint64_t)data[pos + 2] << 40 |
            (qoa_uint64_t)data[pos + 3] << 32 |
            (qoa_uint64_t)data[pos + 4] << 24 |
            (qoa_uint64_t)data[pos + 5] << 16 |
            (qoa_uint64_t)data[pos + 6] << 8 | (qoa_uint64_t)data[pos + 7];
        const unsigned int chans = (fh >> 56) & 0xFF;
        const unsigned int frame_size = fh & 0xFFFF;
        if (chans == 0 || frame_size < 16 ||
            chans != q.channels) { // повреждённый фрейм — останавливаемся
            ok = false;
            break;
        }
        if (pos + frame_size > size) {
            ok = false;
            break;
        }
        unsigned int frame_len = 0;
        if (qoa_decode_frame(data + pos, frame_size, &q, frame_buf.data(),
                             &frame_len) == 0 ||
            frame_len == 0) {
            ok = false;
            break;
        }
        s->pcm.insert(s->pcm.end(), frame_buf.begin(),
                      frame_buf.begin() + size_t(frame_len) * chans);
        pos += frame_size;
        if (q.samples && s->pcm.size() / chans >= q.samples) break;
    }

    s->total_frames = s->pcm.size() / q.channels;
    s->file_rate = q.samplerate;
    s->channels = q.channels;
    s->out_rate = s->file_rate;
    s->out_pos = 0.0;

    if (!ok && s->total_frames == 0) {
        set_error(err, QOA_ERR_ENGINE, "invalid QOA stream");
        delete s;
        return 0;
    }

    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle) {
    if (!handle) return;
    delete static_cast<Session*>(handle);
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !count) return 0;
    *count = 1;
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
    if (track_index != 0) {
        set_error(err, CHP_ERR_NOT_FOUND, "track index out of range");
        return 0;
    }
    s->out_rate = sample_rate;
    s->out_pos = 0.0;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderInt16(ChipHandle handle,
                                                   int16_t* buffer,
                                                   uint32_t frames) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !buffer) return 0;
    return render(s, buffer, frames);
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
    caps->max_tracks = 1;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackInfo(ChipHandle handle,
                                                    uint32_t track_index,
                                                    ChipTrackInfo* info) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !info || info->struct_size < sizeof(*info) || track_index != 0)
        return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->track_index = 0;
    std::snprintf(info->format, sizeof(info->format), "%s", "QOA");
    if (s->total_frames > 0 && s->file_rate > 0) {
        info->duration_ms = uint32_t(s->total_frames * 1000 / s->file_rate);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineSeek(ChipHandle handle,
                                            uint64_t sample_position,
                                            ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->out_rate) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad handle or no active track");
        return 0;
    }
    const double file_frame =
        double(sample_position) * double(s->file_rate) / double(s->out_rate);
    if (file_frame > 4.0e9) {
        set_error(err, CHP_ERR_INVALID_ARG, "position too large");
        return 0;
    }
    s->out_pos = file_frame;
    if (s->out_pos > double(s->total_frames)) {
        s->out_pos = double(s->total_frames); // к концу, рендер вернёт EOF
    }
    return 1;
}

} // extern "C"
