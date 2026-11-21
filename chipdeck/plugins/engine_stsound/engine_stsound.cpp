// Движок ChipDeck на ST-Sound (Arnaud Carre, BSD): YM-файлы Atari ST
// (YM2149/AY-3-8910, форматы YM2!/YM3!/YM3b/YM4!/YM5!/YM6!/YMT1/YMT2,
// включая LZH-упакованные). Рендер моно 16 бит @44100; для частоты
// host'а — линейный ресемплинг, mono -> stereo.
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "StSoundLibrary.h"

namespace {

constexpr uint32_t YM_ERR_ENGINE = 16u; // внутренняя ошибка ST-Sound
constexpr size_t kMaxFileSize = 16u * 1024 * 1024;
constexpr uint32_t kYmRate = 44100; // частота рендера ST-Sound

struct Session {
    std::vector<uint8_t> data; // владеем памятью: ST-Sound держит указатель
    YMMUSIC* mus = nullptr;

    uint32_t file_rate = kYmRate;
    uint32_t channels = 1;     // ST-Sound рендерит моно
    uint64_t total_frames = 0; // кадров файла (44100), 0 = не известно

    // ресемплинг file_rate -> out_rate; чтение сэмплов у ST-Sound
    // строго последовательное (ymMusicCompute по 1), поэтому кеш
    // держит "последний прочитанный кадр" с его файловым индексом.
    uint32_t out_rate = kYmRate;
    double out_pos = 0.0;
    uint64_t cur_index = 0; // файловый индекс кадра cur
    int16_t cur = 0;
    bool have_cur = false;
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

// YM-магия или LZH-упаковка (ST-Sound распакует сам): в LHA-заголовке
// метод "-lhX-" находится по смещению 2 (байт размера + контрольная
// сумма перед ним).
bool sniff(const uint8_t* d, size_t n) {
    if (n < 12) return false;
    if (std::memcmp(d + 2, "-lh", 3) == 0) return true;
    if (d[0] != 'Y' || d[1] != 'M') return false;
    static const char* magics[] = {"2!", "3!", "3b", "4!", "5!", "6!", "T1",
                                   "T2"};
    for (const char* m : magics) {
        const size_t len = std::strlen(m);
        if (std::memcmp(d + 2, m, len) == 0) return true;
    }
    return false;
}

// Читает следующий кадр (моно) через ymMusicCompute.
bool read_frame(Session* s, int16_t* frame) {
    if (s->eof) return false;
    if (ymMusicIsOver(s->mus)) {
        s->eof = true;
        return false;
    }
    ymsample tmp = 0;
    if (ymMusicCompute(s->mus, &tmp, 1) <= 0) {
        s->eof = true;
        return false;
    }
    *frame = tmp;
    return true;
}

// Ресемплинг + mono->stereo: заполняет out, возвращает число кадров.
// Инвариант: cur = сэмпл файла с индексом cur_index; декодер стоит
// сразу после него. out_pos движется вперёд со ставкой file_rate.
uint32_t render(Session* s, int16_t* out, uint32_t count) {
    uint32_t written = 0;
    while (written < count) {
        if (s->eof) break;
        const uint64_t i0 = uint64_t(s->out_pos);
        const double t = s->out_pos - double(i0);

        // Дочитываем до кадра i0 (после Seek/шага ресемплинга).
        if (!s->have_cur) {
            if (!read_frame(s, &s->cur)) break;
            s->have_cur = true;
        } else {
            while (s->cur_index < i0) {
                if (!read_frame(s, &s->cur)) break;
                ++s->cur_index;
            }
            if (s->eof) break;
        }

        // Следующий кадр (i0+1) для линейной интерполяции.
        int16_t f1 = s->cur;
        if (s->cur_index < i0 + 1) {
            if (!read_frame(s, &f1)) {
                f1 = s->cur; // конец потока — держим уровень
            } else {
                s->cur = f1;
                ++s->cur_index;
            }
        }

        for (uint32_t c = 0; c < 2; ++c) {
            const double v = double(s->cur) +
                             (double(f1) - double(s->cur)) * t;
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
    std::snprintf(info->id, sizeof(info->id), "%s", "stsound");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "ST-Sound (Atari YM)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "ym");
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not a YM file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size); // ymMusicLoadMemory читает блок сам

    s->mus = ymMusicCreate();
    if (!s->mus) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "ymMusicCreate failed");
        delete s;
        return 0;
    }
    ymMusicSetLoopMode(s->mus, YMFALSE);
    if (!ymMusicLoadMemory(s->mus, s->data.data(), ymu32(s->data.size()))) {
        set_error(err, YM_ERR_ENGINE, ymMusicGetLastError(s->mus));
        ymMusicDestroy(s->mus);
        delete s;
        return 0;
    }
    ymMusicPlay(s->mus);

    ymMusicInfo_t mi{};
    ymMusicGetInfo(s->mus, &mi);
    if (mi.musicTimeInMs > 0) {
        s->total_frames = uint64_t(mi.musicTimeInMs) * kYmRate / 1000;
    }
    s->out_rate = s->file_rate;
    s->out_pos = 0.0;
    s->cur_index = 0;

    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle) {
    if (!handle) return;
    Session* s = static_cast<Session*>(handle);
    if (s->mus) {
        ymMusicDestroy(s->mus);
        s->mus = nullptr;
    }
    delete s;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !count) return 0;
    *count = 1;
    return 1;
}

// Выбор трека/частоты. Частота меняется ресемплингом без переоткрытия.
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
    s->cur_index = 0;
    s->have_cur = false;
    s->cur = 0;
    s->eof = false;
    ymMusicRestart(s->mus);
    ymMusicPlay(s->mus);
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

    std::snprintf(info->format, sizeof(info->format), "%s", "YM (Atari ST)");

    ymMusicInfo_t mi{};
    ymMusicGetInfo(s->mus, &mi);
    if (mi.pSongName && mi.pSongName[0]) {
        std::snprintf(info->title, sizeof(info->title), "%s", mi.pSongName);
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    if (mi.pSongAuthor && mi.pSongAuthor[0]) {
        std::snprintf(info->author, sizeof(info->author), "%s",
                      mi.pSongAuthor);
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
    if (mi.musicTimeInMs > 0) {
        info->duration_ms = uint32_t(mi.musicTimeInMs);
        info->flags |= CHP_TRACK_HAS_DURATION;
    }
    return 1;
}

// Позиция — в выходных сэмплах от начала трека.
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
    s->eof = false;
    s->have_cur = false;
    s->cur = 0;
    if (ymMusicIsSeekable(s->mus)) {
        // ST-Sound умеет Seek с точностью 1 мс.
        const ymu32 ms = ymu32(file_frame * 1000 / s->file_rate);
        ymMusicSeek(s->mus, ms);
        s->cur_index = uint64_t(ms) * kYmRate / 1000;
        s->out_pos = double(s->cur_index);
    } else {
        // Не seekable: сброс в начало (позиция не гарантируется).
        ymMusicRestart(s->mus);
        ymMusicPlay(s->mus);
        s->cur_index = 0;
        s->out_pos = 0.0;
        set_error(err, YM_ERR_ENGINE, "tune not seekable; restarted");
    }
    return 1;
}

} // extern "C"
