// Движок ChipDeck на opusfile (xiph, BSD) + libopus: Ogg Opus (.opus).
// Opus всегда декодируется в 48 кГц — для частоты host'а применяется
// линейный ресемплинг, mono -> stereo. Потоковое блочное декодирование
// с буфером сессии (см. engine_vorbis). ../../include/chipdeck_api.h.

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include <opus/opusfile.h>

namespace {

constexpr uint32_t OP_ERR_ENGINE = 16u; // внутренняя ошибка opusfile
constexpr size_t kMaxFileSize = 128u * 1024 * 1024;
constexpr uint32_t kOpusRate = 48000; // Opus декодируется только в 48k

// Один кадр файла (48 кГц, до 2 каналов) для интерполяции.
struct Frame {
    int16_t ch[2];
};

struct Session {
    std::vector<uint8_t> data; // владеем памятью: opusfile держит указатель
    OggOpusFile* of = nullptr;

    uint32_t channels = 0;      // каналов в файле (1..8)
    uint32_t file_rate = kOpusRate;
    uint64_t total_frames = 0;  // кадров 48 кГц в файле

    // ресемплинг file_rate -> out_rate и буфер декодированных кадров
    uint32_t out_rate = kOpusRate;
    double out_pos = 0.0;
    std::vector<int16_t> buf; // interleaved, кадр buf_start = buf[0]
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

// OggS-магия + "OpusHead" в первом килобайте.
bool sniff(const uint8_t* d, size_t n) {
    if (n < 32 || std::memcmp(d, "OggS", 4) != 0) return false;
    const size_t lim = n < 1024 ? n : 1024;
    for (size_t i = 0; i + 8 < lim; ++i) {
        if (std::memcmp(d + i, "OpusHead", 8) == 0) return true;
    }
    return false;
}

void close_session(Session* s) {
    if (s && s->of) {
        op_free(s->of);
        s->of = nullptr;
    }
}

// Декодирует очередной блок Opus (до 5760 кадров) в буфер сессии.
bool decode_block(Session* s) {
    if (s->eof) return false;
    static thread_local std::vector<short> tmp; // не пере-аллоцируем
    tmp.resize(5760 * s->channels);
    for (;;) {
        const int got = op_read(s->of, tmp.data(), int(tmp.size()), nullptr);
        if (got > 0) {
            // op_read может выдать меньше каналов, чем в файле (bitrate
            // collapse) — не встречается на практике; обрезаем честно.
            s->buf.insert(s->buf.end(), tmp.begin(),
                          tmp.begin() + size_t(got) * s->channels);
            return true;
        }
        if (got == 0) { // конец потока
            s->eof = true;
            return false;
        }
        if (got == OP_HOLE) continue; // разрыв страницы — читаем дальше
        s->eof = true; // ошибка декодирования: останавливаемся
        return false;
    }
}

// Гарантирует наличие в буфере кадров [from..to].
bool ensure_frames(Session* s, uint64_t to) {
    while (!s->eof && to >= s->buf_start + s->buf.size() / s->channels) {
        if (!decode_block(s)) break;
    }
    return to < s->buf_start + s->buf.size() / s->channels;
}

// Кадр 48 кГц по индексу (mono дублируется в ch[0]/ch[1]).
bool get_frame_at(Session* s, uint64_t i, Frame* frame) {
    if (!ensure_frames(s, i + 1)) return false;
    const size_t off = size_t(i - s->buf_start) * s->channels;
    (*frame) = Frame{{s->buf[off],
                      s->channels > 1 ? s->buf[off + 1] : s->buf[off]}};
    return true;
}

// Сброс декодера на позицию file_frame (кадры 48 кГц).
bool reset_decoder(Session* s, uint64_t file_frame) {
    s->buf.clear();
    s->buf_start = file_frame;
    s->eof = false;
    if (file_frame >= s->total_frames) {
        s->eof = true;
        return true;
    }
    if (op_pcm_seek(s->of, opus_int64(file_frame)) < 0) {
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
        if (!get_frame_at(s, i0, &f0)) break; // конец потока
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
        const size_t drop = size_t(keep_from - s->buf_start) * s->channels;
        if (drop <= s->buf.size()) {
            s->buf.erase(s->buf.begin(), s->buf.begin() + int(drop));
            s->buf_start = keep_from;
        }
    }
    return written;
}

// Достаёт "KEY=значение" из OpusTags (регистр KEY не важен).
const char* find_comment(const OpusTags* tags, const char* key) {
    if (!tags) return nullptr;
    const size_t klen = std::strlen(key);
    for (int i = 0; i < tags->comments; ++i) {
        const char* cm = tags->user_comments[i];
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
    std::snprintf(info->id, sizeof(info->id), "%s", "opus");
    std::snprintf(info->name, sizeof(info->name), "%s", "opusfile (Ogg Opus)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s",
                  "opus;ogg;oga");
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not an Ogg Opus file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size);

    int oerr = 0;
    s->of = op_open_memory(s->data.data(), s->data.size(), &oerr);
    if (!s->of) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "opusfile error %d", oerr);
        set_error(err, OP_ERR_ENGINE, msg);
        delete s;
        return 0;
    }

    const OpusHead* head = op_head(s->of, -1);
    s->channels = head ? uint32_t(head->channel_count) : 2;
    const ogg_int64_t pcm_total = op_pcm_total(s->of, -1);
    s->total_frames = pcm_total > 0 ? uint64_t(pcm_total) : 0;
    s->out_rate = s->file_rate = kOpusRate;
    s->out_pos = 0.0;

    if (s->channels == 0 || s->channels > 2) {
        set_error(err, OP_ERR_ENGINE, "unsupported channel count");
        close_session(s);
        delete s;
        return 0;
    }

    // Pre-roll по умолчанию подходит для прослушивания; буфер не нужен.
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

    std::snprintf(info->format, sizeof(info->format), "%s", "Ogg Opus");

    // Метаданные из OpusTags.
    const OpusTags* tags = op_tags(s->of, -1);
    const char* title = find_comment(tags, "TITLE");
    if (title && title[0]) {
        std::snprintf(info->title, sizeof(info->title), "%s", title);
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    const char* artist = find_comment(tags, "ARTIST");
    if (artist && artist[0]) {
        std::snprintf(info->author, sizeof(info->author), "%s", artist);
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
    if (s->total_frames > 0) {
        info->duration_ms =
            uint32_t(s->total_frames * 1000 / kOpusRate);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }
    return 1;
}

// Позиция — в выходных сэмплах от начала трека (48 кГц -> out_rate).
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
        s->out_pos = double(s->total_frames);
    }
    return 1;
}

} // extern "C"
