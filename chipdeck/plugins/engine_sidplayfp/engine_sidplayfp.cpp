// Движок ChipDeck на libsidplayfp 3.1.2 + libresidfp 1.3.0: играет .sid
// (PSID/RSID, C64 SID 6581/8580). Выход int16 mono -> дублируем в стерео.
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#include "chipdeck_api.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/builders/residfp.h>

namespace {

constexpr uint32_t kErrSid = 16u;         // внутренняя ошибка libsidplayfp
constexpr size_t kMaxFileSize = 4u * 1024 * 1024; // .sid больше 4 МиБ не бывает
constexpr uint32_t kDefaultRate = 44100;
constexpr uint32_t kPalClock = 985248;    // циклов CPU в секунду (PAL)
constexpr uint32_t kNtscClock = 1022727;  // (NTSC)

struct Session {
    std::vector<uint8_t> data; // копия файла для SidTune
    SidTune* tune = nullptr;
    sidplayfp* player = nullptr;
    ReSIDfpBuilder* builder = nullptr;
    uint32_t rate = 0;
    uint32_t track = 0;
    uint32_t clock = kPalClock;
    std::vector<short> mono;   // промежуточный буфер mix()
};

void set_error(ChipError* err, uint32_t code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->reserved = 0;
    if (msg) std::snprintf(err->message, sizeof(err->message), "%s", msg);
    else err->message[0] = '\0';
}

// Пересоздаёт player с данной частотой; tune уже выбрана.
// В libresidfp 1.3 чипы создаёт sidplayfp::config() через builder->lock().
bool rebuild_locked_session(Session* s, ChipError* err) {
    delete s->player;
    s->player = nullptr;
    if (s->builder) s->builder->remove(); // освободить старые чипы

    sidplayfp* p = new sidplayfp();

    if (!s->builder) s->builder = new ReSIDfpBuilder("ChipDeck reSIDfp");

    SidConfig cfg;
    cfg.frequency = s->rate;
    cfg.sidEmulation = s->builder;
    cfg.defaultSidModel =
        s->tune->getInfo()->sidModel(0) == SidTuneInfo::SIDMODEL_8580
            ? SidConfig::MOS8580
            : SidConfig::MOS6581;
    cfg.forceSidModel = true;

    if (!p->config(cfg)) {
        set_error(err, kErrSid, p->error());
        delete p;
        return false;
    }
    if (!p->load(s->tune)) {
        set_error(err, kErrSid, p->error());
        delete p;
        return false;
    }
    p->initMixer(false); // mono
    s->player = p;
    return true;
}

void teardown_session(Session* s) {
    if (!s) return;
    delete s->player;
    s->player = nullptr;
    if (s->builder) {
        s->builder->remove();
        delete s->builder;
        s->builder = nullptr;
    }
    delete s->tune;
    s->tune = nullptr;
}

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetInfo(ChipEngineInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    std::snprintf(info->id, sizeof(info->id), "%s", "sidplayfp");
    std::snprintf(info->name, sizeof(info->name), "%s",
                  "SID (C64, libsidplayfp/reSIDfp)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "sid");
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineProbe(const uint8_t* data, size_t size) {
    if (!data || size < 4) return 0;
    return std::memcmp(data, "PSID", 4) == 0 ||
                   std::memcmp(data, "RSID", 4) == 0
               ? 1u
               : 0u;
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
        set_error(err, CHP_ERR_NOT_RECOGNIZED, "not a PSID/RSID file");
        return 0;
    }

    Session* s = new (std::nothrow) Session();
    if (!s) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    s->data.assign(data, data + size);
    s->tune = new SidTune(s->data.data(), uint_least32_t(s->data.size()));
    if (!s->tune->getStatus()) {
        set_error(err, CHP_ERR_NOT_RECOGNIZED, s->tune->statusString());
        delete s->tune;
        delete s;
        return 0;
    }
    // Частота по умолчанию: настоящий Select пересоберёт под нужную.
    s->rate = kDefaultRate;
    s->clock = s->tune->getInfo()->clockSpeed() == SidTuneInfo::CLOCK_NTSC
                   ? kNtscClock
                   : kPalClock;

    if (!rebuild_locked_session(s, err)) {
        teardown_session(s);
        delete s;
        return 0;
    }

    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipEngineClose(ChipHandle handle) {
    if (!handle) return;
    Session* s = static_cast<Session*>(handle);
    teardown_session(s);
    delete s;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackCount(ChipHandle handle,
                                                     uint32_t* count) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !count) return 0;
    *count = s->tune ? s->tune->getInfo()->songs() : 0u;
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

    const uint32_t songs = s->tune->getInfo()->songs();
    if (songs == 0 || track_index >= songs) {
        set_error(err, CHP_ERR_NOT_FOUND, "track index out of range");
        return 0;
    }

    s->rate = sample_rate;
    s->track = track_index;
    s->tune->selectSong(track_index + 1); // в .sid подтюны с 1

    if (!rebuild_locked_session(s, err)) return 0;
    return 1;
}

// play(cycles) -> getBufSize() -> mix() в mono-скретч -> интерлив в стерео.
CHP_EXPORT uint32_t CHP_CALL ChipEngineRenderInt16(ChipHandle handle,
                                                   int16_t* buffer,
                                                   uint32_t frames) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->player || !buffer) return 0;

    s->mono.resize(size_t(frames));
    const unsigned int cycles = static_cast<unsigned int>(
        (uint64_t(frames) * s->clock + s->rate - 1) / s->rate);
    s->player->play(cycles);
    const unsigned int produced = s->player->getBufSize(cycles);
    if (produced == 0) return 0;

    const unsigned int mixed =
        s->player->mix(s->mono.data(),
                       produced > frames ? frames : produced);
    if (mixed == 0) return 0;

    for (unsigned int i = 0; i < mixed; ++i) {
        buffer[i * 2] = s->mono[i];
        buffer[i * 2 + 1] = s->mono[i];
    }
    return mixed;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetCapabilities(
    ChipEngineCapabilities* caps) {
    if (!caps || caps->struct_size < sizeof(*caps)) return 0;
    const uint32_t caller_size = caps->struct_size;
    std::memset(caps, 0, sizeof(*caps));
    caps->struct_size = caller_size;
    caps->version = CHP_API_VERSION;
    caps->flags = CHP_ENGINE_INT16; // seek в sidplayfp API нет
    caps->voice_count = 0;
    caps->spectrum_bands = 0;
    caps->max_tracks = 0;
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipEngineGetTrackInfo(ChipHandle handle,
                                                    uint32_t track_index,
                                                    ChipTrackInfo* info) {
    Session* s = static_cast<Session*>(handle);
    if (!s || !s->tune || !info || info->struct_size < sizeof(*info)) return 0;

    const SidTuneInfo* ti = s->tune->getInfo();
    if (!ti || track_index >= ti->songs()) return 0;

    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->track_index = track_index;

    if (ti->numberOfInfoStrings() > 0 && ti->infoString(0) &&
        ti->infoString(0)[0]) {
        std::snprintf(info->title, sizeof(info->title), "%s",
                      ti->infoString(0));
        info->flags |= CHP_TRACK_HAS_TITLE;
    }
    if (ti->numberOfInfoStrings() > 1 && ti->infoString(1) &&
        ti->infoString(1)[0]) {
        std::snprintf(info->author, sizeof(info->author), "%s",
                      ti->infoString(1));
        info->flags |= CHP_TRACK_HAS_AUTHOR;
    }
    // formatString бывает длинным ("Real C64 one-file format (RSID)") —
    // сворачиваем до PSID/RSID.
    const char* fs = ti->formatString() ? ti->formatString() : "";
    const char* kind = std::strstr(fs, "RSID") ? "RSID"
                       : std::strstr(fs, "PSID") ? "PSID" : "SID";
    std::snprintf(info->format, sizeof(info->format), "SID (%s, %s)", kind,
                  ti->sidModel(0) == SidTuneInfo::SIDMODEL_8580 ? "8580"
                                                                 : "6581");
    // Длительности в .sid нет (нужна внешняя БД Songlengths) — не ставим
    // CHP_TRACK_HAS_DURATION; host запомнит фактическую.
    return 1;
}

} // extern "C"
