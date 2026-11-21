// Движок ChipDeck на dr_libs (single-header, public domain):
// MP3 (dr_mp3), FLAC (dr_flac), WAV (dr_wav). Pull-модель рендера в
// int16. dr-декодеры отдают файловую частоту — для частоты, запрошенной
// host'ом, применяется простой линейный ресемплинг; mono дублируется в
// stereo. См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr/dr_flac.h"
#include "dr/dr_mp3.h"
#include "dr/dr_wav.h"

namespace {

enum Format { FMT_MP3, FMT_FLAC, FMT_WAV };

constexpr uint32_t DR_ERR_ENGINE = 16u; // внутренняя ошибка dr_libs
constexpr size_t kMaxFileSize = 256u * 1024 * 1024;

// Один фрейм файла (до 2 каналов) для интерполяции.
struct Frame {
    int16_t ch[2];
};

struct Session {
    std::vector<uint8_t> data; // владеем памятью: dr_* держат указатели
    Format fmt = FMT_MP3;

    drmp3 mp3 = {};
    drflac* flac = nullptr;
    drwav wav = {};

    uint64_t total_frames = 0; // фреймов в файле (по каналам); ~0 = неизвестно
    uint64_t known_frames = 0; // то же, но 0 если длительность неизвестна
    uint32_t file_rate = 0;
    uint32_t channels = 0;

    // ресемплинг file_rate -> out_rate
    uint32_t out_rate = 0;
    double out_pos = 0.0; // позиция вывода в фреймах файла (вещественная)
    Frame cur = {{0, 0}};
    Frame next = {{0, 0}};
    uint64_t have_upto = 0; // фреймов файла прочитано (cur=i0, next=i0+1)
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

// Распознавание по байтам: FLAC и WAV — по магии, MP3 — по ID3 или
// MPEG sync-word (0xFFEx) в первом килобайте.
Format sniff(const uint8_t* d, size_t n) {
    if (n >= 4 && std::memcmp(d, "fLaC", 4) == 0) return FMT_FLAC;
    if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 &&
        std::memcmp(d + 8, "WAVE", 4) == 0) return FMT_WAV;
    if (n >= 3 && std::memcmp(d, "ID3", 3) == 0) return FMT_MP3;
    const size_t lim = n < 1024 ? n : 1024;
    for (size_t i = 0; i + 1 < lim; ++i) {
        if (d[i] == 0xFF && (d[i + 1] & 0xE0) == 0xE0) return FMT_MP3;
    }
    return FMT_MP3;
}

void close_session(Session* s) {
    if (!s) return;
    if (s->fmt == FMT_MP3) drmp3_uninit(&s->mp3);
    if (s->fmt == FMT_FLAC && s->flac) drflac_close(s->flac);
    if (s->fmt == FMT_WAV) drwav_uninit(&s->wav);
    s->flac = nullptr;
}

// Читает ровно один фрейм файла в frame (ch[0], ch[1] при stereo).
bool read_frame(Session* s, Frame* frame) {
    if (s->fmt == FMT_MP3) {
        return drmp3_read_pcm_frames_s16(&s->mp3, 1, frame->ch) == 1;
    }
    if (s->fmt == FMT_FLAC) {
        return drflac_read_pcm_frames_s16(s->flac, 1, frame->ch) == 1;
    }
    return drwav_read_pcm_frames_s16(&s->wav, 1, frame->ch) == 1;
}

bool seek_file(Session* s, uint64_t file_frame) {
    bool ok = false;
    if (s->fmt == FMT_MP3) {
        ok = drmp3_seek_to_pcm_frame(&s->mp3, drmp3_uint64(file_frame));
    } else if (s->fmt == FMT_FLAC) {
        ok = drflac_seek_to_pcm_frame(s->flac, drflac_uint64(file_frame));
    } else {
        ok = drwav_seek_to_pcm_frame(&s->wav, drwav_uint64(file_frame));
    }
    return ok;
}

// Сброс декодера на позицию file_frame и пересчёт кеша интерполяции.
bool reset_decoder(Session* s, uint64_t file_frame) {
    if (file_frame > s->total_frames) file_frame = s->total_frames;
    if (!seek_file(s, file_frame)) return false;
    s->have_upto = file_frame;
    s->eof = false;
    if (file_frame >= s->total_frames) {
        s->cur = Frame{{0, 0}};
        s->next = Frame{{0, 0}};
        return true;
    }
    if (!read_frame(s, &s->cur)) {
        s->eof = true;
        s->cur = Frame{{0, 0}};
        return false;
    }
    s->have_upto = file_frame + 1;
    if (s->have_upto >= s->total_frames || !read_frame(s, &s->next)) {
        s->eof = true;
        s->next = s->cur;
    }
    return true;
}

// Продвигает кеш на один фрейм файла (cur <- next, next <- read).
void advance(Session* s) {
    if (s->eof) {
        s->cur = s->next; // дотянутый до конца "следующий"
    } else {
        s->cur = s->next;
        ++s->have_upto;
        if (s->have_upto >= s->total_frames || !read_frame(s, &s->next)) {
            s->eof = true;
            s->next = s->cur;
        }
    }
}

// Конвертация + ресемплинг: заполняет out (interleaved stereo) count'ом
// кадров. Возвращает число записанных кадров (меньше count = конец).
uint32_t render(Session* s, int16_t* out, uint32_t count) {
    uint32_t written = 0;
    while (written < count) {
        // Конец файла: точная длительность известна, или же декодер
        // дошёл до EOF и позиция вышла за последний прочитанный фрейм
        // (MP3 умеет не знать totalPCMFrameCount).
        if (s->out_pos >= double(s->total_frames)) break;
        if (s->eof && s->out_pos >= double(s->have_upto)) break;
        const uint64_t i0 = uint64_t(s->out_pos);
        const double t = s->out_pos - double(i0);
        while (i0 >= s->have_upto) advance(s);

        // линейная интерполяция по каналам
        for (uint32_t c = 0; c < 2; ++c) {
            const double v = double(s->cur.ch[c]) +
                             (double(s->next.ch[c]) - double(s->cur.ch[c])) * t;
            double cl = v < -32768.0 ? -32768.0
                        : v > 32767.0 ? 32767.0
                                      : v;
            out[(size_t)written * 2 + c] = int16_t(cl + (cl < 0 ? -0.5 : 0.5));
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
    std::snprintf(info->id, sizeof(info->id), "%s", "dr");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "dr_libs (MP3 / FLAC / WAV)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s",
                  "mp3;flac;wav");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size < 12) return 0;
    // FLAC и WAV — по магии; MP3 — по ID3 или MPEG sync-word в первом
    // килобайте (стандартная эвристика, точность подтверждает Open).
    if (std::memcmp(data, "fLaC", 4) == 0) return 1;
    if (std::memcmp(data, "RIFF", 4) == 0 &&
        std::memcmp(data + 8, "WAVE", 4) == 0) return 1;
    if (std::memcmp(data, "ID3", 3) == 0) return 1;
    const size_t lim = size < 1024 ? size : 1024;
    for (size_t i = 0; i + 1 < lim; ++i) {
        if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0) return 1;
    }
    return 0;
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not an MP3/FLAC/WAV file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size);
    s->fmt = sniff(s->data.data(), s->data.size());

    // ВАЖНО: dr_* хранят указатели в наш буфер — копируем данные в
    // вектор ДО инициализации декодера и не трогаем его после.
    const uint8_t* p = s->data.data();
    size_t n = s->data.size();

    if (s->fmt == FMT_MP3) {
        if (!drmp3_init_memory(&s->mp3, p, n, nullptr)) {
            set_error(err, DR_ERR_ENGINE, "invalid MP3 stream");
            delete s;
            return 0;
        }
        s->total_frames = s->mp3.totalPCMFrameCount;
        s->file_rate = s->mp3.sampleRate;
        s->channels = s->mp3.channels;
    } else if (s->fmt == FMT_FLAC) {
        s->flac = drflac_open_memory(p, n, nullptr);
        if (!s->flac) {
            set_error(err, DR_ERR_ENGINE, "invalid FLAC stream");
            delete s;
            return 0;
        }
        s->total_frames = s->flac->totalPCMFrameCount;
        s->file_rate = s->flac->sampleRate;
        s->channels = s->flac->channels;
    } else {
        if (!drwav_init_memory(&s->wav, p, n, nullptr)) {
            set_error(err, DR_ERR_ENGINE, "invalid WAV stream");
            delete s;
            return 0;
        }
        s->total_frames = s->wav.totalPCMFrameCount;
        s->file_rate = s->wav.sampleRate;
        s->channels = s->wav.channels;
    }

    if (s->channels == 0 || s->channels > 2 || s->file_rate == 0) {
        set_error(err, DR_ERR_ENGINE, "unsupported channel count / rate");
        close_session(s);
        delete s;
        return 0;
    }
    s->known_frames = s->total_frames;
    if (s->total_frames == 0) {
        // MP3 без точной длительности: размер не ограничиваем, конец
        // потока определяет EOF декодера.
        s->total_frames = ~0ull;
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
    *count = 1; // аудиофайл = один трек
    return 1;
}

// Выбор трека и частоты. Частота меняется без переоткрытия — ресемплинг
// работает в рендере. Любая смена трека/частоты сбрасывает позицию.
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

    std::snprintf(info->format, sizeof(info->format), "%s",
                  s->fmt == FMT_MP3  ? "MP3"
                  : s->fmt == FMT_FLAC ? "FLAC"
                                       : "WAV");
    if (s->known_frames > 0 && s->file_rate > 0) {
        info->duration_ms =
            uint32_t(s->known_frames * 1000 / s->file_rate);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }
    return 1;
}

// Позиция — в выходных сэмплах от начала трека.
CHP_EXPORT uint32_t CHP_CALL ChipEngineSeek(ChipHandle handle,
                                            uint64_t sample_position,
                                            ChipError* err) {
    Session* s = static_cast<Session*>(handle);
    if (!s) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad handle");
        return 0;
    }
    if (!s->out_rate) {
        set_error(err, CHP_ERR_INVALID_ARG, "no active track");
        return 0;
    }
    const double file_frame =
        double(sample_position) * double(s->file_rate) / double(s->out_rate);
    if (file_frame > 4.0e9) {
        set_error(err, CHP_ERR_INVALID_ARG, "position too large");
        return 0;
    }
    s->out_pos = file_frame;
    // Кеш интерполяции: i0 = floor(out_pos)
    const uint64_t i0 = uint64_t(s->out_pos);
    if (i0 > s->total_frames) s->out_pos = double(s->total_frames);
    if (!reset_decoder(s, i0)) {
        // MP3-seek к самому концу может «провалиться» — не критично:
        // рендер тогда сразу вернёт конец потока.
        s->out_pos = double(s->total_frames);
    }
    return 1;
}

} // extern "C"
