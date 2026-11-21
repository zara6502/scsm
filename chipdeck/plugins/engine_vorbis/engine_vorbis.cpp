// Движок ChipDeck на stb_vorbis (nothings/stb, public domain):
// Ogg Vorbis (.ogg). Pull-модель рендера в int16; линейный ресемплинг
// файловой частоты под частоту host'а, mono -> stereo.
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_PUSHDATA
#define STB_VORBIS_IMPLEMENTATION
#include "stb/stb_vorbis.c"

namespace {

constexpr uint32_t VB_ERR_ENGINE = 16u; // внутренняя ошибка stb_vorbis
constexpr size_t kMaxFileSize = 128u * 1024 * 1024;

// Один фрейм файла (до 2 каналов) для интерполяции.
struct Frame {
    int16_t ch[2];
};

struct Session {
    std::vector<uint8_t> data; // владеем памятью: stb держит указатель
    stb_vorbis* vb = nullptr;

    uint64_t total_frames = 0; // кадров файла (по каналам)
    uint32_t file_rate = 0;
    uint32_t channels = 0;

    // ресемплинг file_rate -> out_rate
    uint32_t out_rate = 0;
    double out_pos = 0.0;
    // Буфер декодированных кадров (interleaved, file channels):
    // кадр buf_start соответствует buf[0]. stb_vorbis отдаёт блоками
    // (~1024 кадра за вызов), поэтому читаем блоки и раздаём по кадру.
    std::vector<int16_t> buf;
    uint64_t buf_start = 0;
    bool eof = false;
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

// OggS-магия + identification-пакет "\x01vorbis" в первом килобайте.
bool sniff(const uint8_t* d, size_t n) {
    if (n < 32 || std::memcmp(d, "OggS", 4) != 0) return false;
    const size_t lim = n < 1024 ? n : 1024;
    for (size_t i = 0; i + 7 < lim; ++i) {
        if (d[i] == 0x01 && std::memcmp(d + i + 1, "vorbis", 6) == 0)
            return true;
    }
    return false;
}

void close_session(Session* s) {
    if (s && s->vb) {
        stb_vorbis_close(s->vb);
        s->vb = nullptr;
    }
}

// Декодирует очередной vorbis-блок в буфер сессии. Буфер обязан
// вмещать целый блок (до 8192 сэмплов на канал), иначе
// stb_vorbis_get_frame_short_interleaved вернёт 0.
bool decode_block(Session* s) {
    if (s->eof) return false;
    static thread_local std::vector<int16_t> tmp; // не пере-аллоцируем
    tmp.resize(8192 * s->channels);
    const int got = stb_vorbis_get_frame_short_interleaved(
        s->vb, int(s->channels), tmp.data(), int(tmp.size()));
    if (got <= 0) {
        s->eof = true;
        return false;
    }
    s->buf.insert(s->buf.end(), tmp.begin(),
                  tmp.begin() + size_t(got) * s->channels);
    return true;
}

// Гарантирует наличие в буфере кадров [from..to]. Буфер движется
// только вперёд; при выходе за конец ставит eof.
bool ensure_frames(Session* s, uint64_t from, uint64_t to) {
    (void)from; // буфер всегда начинается не позже уже выданных кадров
    while (!s->eof && to >= s->buf_start + s->buf.size() / s->channels) {
        if (!decode_block(s)) break;
    }
    return to < s->buf_start + s->buf.size() / s->channels;
}

// Кадр файла по индексу (ч[0], ч[1] — mono дублируется).
bool get_frame_at(Session* s, uint64_t i, Frame* frame) {
    if (!ensure_frames(s, i, i + 1)) return false;
    const size_t off = size_t(i - s->buf_start) * s->channels;
    (*frame) = Frame{{s->buf[off],
                      s->channels > 1 ? s->buf[off + 1] : s->buf[off]}};
    return true;
}

// Сброс декодера на позицию file_frame.
bool reset_decoder(Session* s, uint64_t file_frame) {
    s->buf.clear();
    s->buf_start = file_frame;
    s->eof = false;
    if (file_frame >= s->total_frames) {
        s->eof = true;
        return true;
    }
    if (!stb_vorbis_seek(s->vb, unsigned(file_frame))) {
        s->eof = true;
        return false;
    }
    decode_block(s); // eof при неудаче выставится сам
    return true;
}

// Ресемплинг + mono->stereo: заполняет out, возвращает число кадров.
uint32_t render(Session* s, int16_t* out, uint32_t count) {
    uint32_t written = 0;
    while (written < count) {
        if (s->out_pos >= double(s->total_frames)) break;
        const uint64_t i0 = uint64_t(s->out_pos);
        const double t = s->out_pos - double(i0);
        const uint64_t i1 = i0 + 1;

        Frame f0{{0, 0}};
        Frame f1{{0, 0}};
        const bool have0 = get_frame_at(s, i0, &f0);
        if (!have0) break; // конец потока
        // i1 может быть за концом — тогда интерполируем на сам в себя.
        if (!get_frame_at(s, i1, &f1)) f1 = f0;

        for (uint32_t c = 0; c < 2; ++c) {
            const double v = double(f0.ch[c]) +
                             (double(f1.ch[c]) - double(f0.ch[c])) * t;
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
    // Отбрасываем уже проигранное начало буфера, чтобы он не рос.
    const uint64_t keep_from =
        s->out_pos < double(s->total_frames) ? uint64_t(s->out_pos)
                                             : s->total_frames;
    if (keep_from > s->buf_start) {
        const size_t drop =
            size_t(keep_from - s->buf_start) * s->channels;
        if (drop <= s->buf.size()) {
            s->buf.erase(s->buf.begin(),
                         s->buf.begin() + int(drop));
            s->buf_start = keep_from;
        }
    }
    return written;
}

// Достаёт "KEY=значение" из Vorbis-комментариев (регистр KEY не важен).
const char* find_comment(const stb_vorbis_comment& c, const char* key) {
    const size_t klen = std::strlen(key);
    for (int i = 0; i < c.comment_list_length; ++i) {
        const char* cm = c.comment_list[i];
        if (!cm) continue;
        if (std::strlen(cm) > klen && cm[klen] == '=' &&
            _strnicmp(cm, key, klen) == 0) {
            return cm + klen + 1;
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "vorbis");
    std::snprintf(info->name, sizeof(info->name), "%s", "stb_vorbis (Ogg Vorbis)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "ogg");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data) return 0;
    return sniff(data, size) ? 1u : 0u;
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not an Ogg Vorbis file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size);

    s->vb = stb_vorbis_open_memory(s->data.data(), int(s->data.size()),
                                   nullptr, nullptr);
    if (!s->vb) {
        set_error(err, VB_ERR_ENGINE, "invalid Ogg Vorbis stream");
        delete s;
        return 0;
    }

    const stb_vorbis_info vi = stb_vorbis_get_info(s->vb);
    s->file_rate = vi.sample_rate;
    s->channels = vi.channels;
    s->total_frames = stb_vorbis_stream_length_in_samples(s->vb);

    if (s->channels == 0 || s->channels > 2 || s->file_rate == 0) {
        set_error(err, VB_ERR_ENGINE, "unsupported channel count / rate");
        close_session(s);
        delete s;
        return 0;
    }

    s->out_rate = s->file_rate;
    s->out_pos = 0.0;
    reset_decoder(s, 0);

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
    reset_decoder(s, 0);
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

    std::snprintf(info->format, sizeof(info->format), "%s", "Ogg Vorbis");

    // Метаданные из Vorbis-комментариев.
    const stb_vorbis_comment vc = stb_vorbis_get_comment(s->vb);
    const char* title = find_comment(vc, "TITLE");
    if (title && title[0]) {
        std::snprintf(info->title, sizeof(info->title), "%s", title);
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    const char* artist = find_comment(vc, "ARTIST");
    if (artist && artist[0]) {
        std::snprintf(info->author, sizeof(info->author), "%s", artist);
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
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
    const uint64_t i0 = uint64_t(s->out_pos);
    if (i0 > s->total_frames) s->out_pos = double(s->total_frames);
    if (!reset_decoder(s, i0)) {
        // Seek к самому концу может не сойтись — не критично: рендер
        // тогда сразу вернёт конец потока.
        s->out_pos = double(s->total_frames);
    }
    return 1;
}

} // extern "C"
