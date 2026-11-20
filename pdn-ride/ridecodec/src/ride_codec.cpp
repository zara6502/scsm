// Реализация C ABI-контракта RideCodec поверх rideent (ride:: API).
// См. ../include/ride_codec/ride_codec.h — правила памяти, потоков, ошибок.

#include "ride_codec/ride_codec.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "quantizer.hpp"
#include "ride/ride.hpp"

namespace {

constexpr uint32_t kRideCodecMajor = 1;
constexpr uint32_t kRideCodecMinor = 0;

constexpr uint32_t kErrOk = 0;
constexpr uint32_t kErrInvalidArg = 1;
constexpr uint32_t kErrMalformed = 2;
constexpr uint32_t kErrTooLarge = 3;
constexpr uint32_t kErrOutOfMemory = 4;
constexpr uint32_t kErrInternal = 5;

void set_error(RideError* err, uint32_t code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->reserved = 0;
    if (msg) std::snprintf(err->message, sizeof(err->message), "%s", msg);
    else err->message[0] = '\0';
}

uint32_t map_ride_err(const std::string& ride_err) {
    // грубая классификация текстов rideent
    if (ride_err.find("larger") != std::string::npos ||
        ride_err.find("too big") != std::string::npos)
        return kErrTooLarge;
    return kErrMalformed;
}

ride::EncodeOptions to_ride_options(const RideEncodeOptions* o) {
    ride::EncodeOptions opt;
    if (!o) return opt;
    switch (o->profile_choice) {
    case RIDE_PROFILE_RGB: opt.profile = ride::ProfileChoice::RGB; break;
    case RIDE_PROFILE_INDEXED:
        opt.profile = ride::ProfileChoice::Indexed;
        break;
    case RIDE_PROFILE_QOI: opt.profile = ride::ProfileChoice::Qoi; break;
    default: opt.profile = ride::ProfileChoice::Auto; break;
    }
    switch (o->entropy) {
    case RIDE_ENTROPY_AUTO: opt.entropy = ride::EntropyMode::Auto; break;
    case RIDE_ENTROPY_ON: opt.entropy = ride::EntropyMode::On; break;
    default: opt.entropy = ride::EntropyMode::Off; break;
    }
    if (o->linear != 0) opt.flags |= ride::kFlagLinear;
    if (o->block_pixels != 0) opt.block_pixels = o->block_pixels;
    if (o->block_rows != 0) opt.block_rows = o->block_rows;
    opt.try_both_parses = o->try_both_parses != 0;
    opt.threads = o->threads;
    return opt;
}

// Есть ли уникальных цветов больше quantize_colors (нужно квантование).
bool quantization_needed(const uint8_t* bgra, uint32_t w, uint32_t h,
                         const RideEncodeOptions* o) {
    if (!o || o->struct_size < sizeof(RideEncodeOptions)) return false;
    if (o->quantize_colors == 0 || o->quantize_colors > 256) return false;
    // QOI-профиль не квантуется
    if (o->profile_choice == RIDE_PROFILE_QOI) return false;
    return rideq::count_unique_colors(bgra, w, h) > o->quantize_colors;
}

} // namespace

extern "C" {

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecVersion(void) {
    return (kRideCodecMajor << 16) | kRideCodecMinor;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecEncode(
    const uint8_t* bgra, uint32_t w, uint32_t h,
    const RideEncodeOptions* opts,
    uint8_t** out, size_t* out_size,
    RideError* err) {
    if (out) *out = nullptr;
    if (out_size) *out_size = 0;

    if (!bgra || w == 0 || h == 0 || !out || !out_size ||
        (opts && opts->struct_size < sizeof(RideEncodeOptions))) {
        set_error(err, kErrInvalidArg, "invalid arguments");
        return kErrInvalidArg;
    }
    if ((uint64_t)w * h > 1ull << 30) {
        set_error(err, kErrTooLarge, "image too large");
        return kErrTooLarge;
    }

    ride::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        // BGRA -> RGB888 (альфа отбрасывается: формат RIDE v1 без альфы)
        img.pixels[i * 3] = bgra[i * 4 + 2];
        img.pixels[i * 3 + 1] = bgra[i * 4 + 1];
        img.pixels[i * 3 + 2] = bgra[i * 4];
    }

    const ride::EncodeOptions ropt = to_ride_options(opts);
    ride::EncodeStats stats;
    std::string ride_err;
    std::vector<uint8_t> encoded;

    const bool need_quantize =
        quantization_needed(bgra, w, h, opts);
    if (need_quantize) {
        // квантованный INDEXED-кандидат (палитра <= quantize_colors)
        ride::IndexedImage idx;
        const bool dither =
            opts && opts->struct_size >= sizeof(RideEncodeOptions) &&
            opts->dither != 0;
        if (!rideq::quantize_indexed(bgra, w, h, opts->quantize_colors,
                                     dither, idx)) {
            set_error(err, kErrOutOfMemory, "quantization failed");
            return kErrOutOfMemory;
        }
        encoded = ride::encode_indexed(idx, ropt, &stats, &ride_err);
    } else if (ropt.profile == ride::ProfileChoice::Indexed) {
        // форсированный INDEXED без квантования: ошибка >256 цветов —
        // легаси-поведение (см. документацию контракта)
        encoded = ride::encode(img, ropt, &stats, &ride_err);
    } else {
        encoded = ride::encode(img, ropt, &stats, &ride_err);
    }
    if (encoded.empty()) {
        set_error(err, kErrInternal,
                  ride_err.empty() ? "encode failed" : ride_err.c_str());
        return kErrInternal;
    }

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(encoded.size()));
    if (!buf) {
        set_error(err, kErrOutOfMemory, "out of memory");
        return kErrOutOfMemory;
    }
    std::memcpy(buf, encoded.data(), encoded.size());
    *out = buf;
    *out_size = encoded.size();
    set_error(err, kErrOk, nullptr);
    return kErrOk;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size,
    uint32_t* w, uint32_t* h,
    const RideDecodeOptions* opts,
    uint8_t** out,
    RideError* err) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (out) *out = nullptr;

    if (!data || size < 14 || !w || !h || !out ||
        (opts && opts->struct_size < sizeof(RideDecodeOptions))) {
        set_error(err, kErrInvalidArg, "invalid arguments");
        return kErrInvalidArg;
    }

    ride::DecodeOptions dopt;
    if (opts) {
        dopt.threads = opts->threads;
        dopt.max_pixels =
            opts->max_pixels ? opts->max_pixels : (1ull << 30);
    }

    ride::Image img;
    std::string ride_err;
    if (!ride::decode(data, size, img, dopt, &ride_err)) {
        set_error(err, map_ride_err(ride_err),
                  ride_err.empty() ? "decode failed" : ride_err.c_str());
        return map_ride_err(ride_err);
    }

    uint8_t* buf = static_cast<uint8_t*>(std::malloc((size_t)img.width *
                                                     img.height * 4));
    if (!buf) {
        set_error(err, kErrOutOfMemory, "out of memory");
        return kErrOutOfMemory;
    }
    for (size_t i = 0; i < (size_t)img.width * img.height; ++i) {
        buf[i * 4] = img.pixels[i * 3 + 2];     // B
        buf[i * 4 + 1] = img.pixels[i * 3 + 1]; // G
        buf[i * 4 + 2] = img.pixels[i * 3];     // R
        buf[i * 4 + 3] = 255;                   // RIDE v1 без альфы
    }
    *w = img.width;
    *h = img.height;
    *out = buf;
    set_error(err, kErrOk, nullptr);
    return kErrOk;
}

RIDE_CODEC_EXPORT void RIDE_CODEC_CALL RideCodecFree(void* ptr) {
    std::free(ptr);
}

} // extern "C"
