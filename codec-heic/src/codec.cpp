// DLL-кодек HEIC/HEIF для Far Ride Viewer 2.
//
// Декодирует изображения HEIC (HEIF-контейнер, внутри HEVC) в
// нормализованный top-down RGBA8. Использует статически слинкованные
// libheif + libde265 (см. build-deps.sh), посторонних DLL не требует.

#include "ride_viewer_codec_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "heic_decoder.h"

namespace {

// Коды ошибок кодека (общего перечисления в ABI нет, документируем здесь).
enum HeicErrorCode : uint32_t {
    HEIC_ERR_INVALID_ARG = 1, // нулевые/несовместимые аргументы вызова
    HEIC_ERR_NOT_HEIC = 2,    // ввод не распознан как HEIF/HEIC
    HEIC_ERR_DECODE = 3,      // libheif не смог декодировать
    HEIC_ERR_TOO_LARGE = 4,   // изображение превышает лимиты кодека
    HEIC_ERR_OUT_OF_MEMORY = 5,
};

void set_error(RideCodecError* error, uint32_t code, const char* message) {
    if (!error) return;
    error->code = code;
    error->byte_offset = 0;
    if (message) {
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    } else {
        error->message[0] = '\0';
    }
}

uint32_t decode_common(const uint8_t* data, size_t size, RideImage* image,
                       RideImageMetadata* metadata, RideCodecError* error) {
    if (!image) {
        set_error(error, HEIC_ERR_INVALID_ARG, "image is null");
        return 0;
    }

    // Приводим выход к состоянию, безопасному для FreeImage, ещё до работы.
    // struct_size вызывающей стороны сохраняем.
    image->width = 0;
    image->height = 0;
    image->stride = 0;
    image->pixel_format = 0;
    image->pixels = nullptr;
    image->data_size = 0;

    if (!data || size == 0) {
        set_error(error, HEIC_ERR_NOT_HEIC, "empty input");
        return 0;
    }
    if (!heic::probe(data, size)) {
        set_error(error, HEIC_ERR_NOT_HEIC, "not a HEIF/HEIC file");
        return 0;
    }

    heic::DecodedImage decoded;
    heic::SourceInfo info;
    std::string err;
    bool ok = false;
    try {
        ok = heic::decode(data, size, decoded, info, err);
    } catch (const std::bad_alloc&) {
        err = "out of memory";
        ok = false;
    } catch (...) {
        // Исключения не пересекают границу C ABI.
        err = "unknown decoder error";
        ok = false;
    }

    if (!ok) {
        set_error(error, HEIC_ERR_DECODE, err.c_str());
        return 0;
    }

    image->width = decoded.width;
    image->height = decoded.height;
    image->stride = decoded.stride;
    image->pixel_format = 1; // top-down RGBA8
    image->pixels = decoded.pixels;
    image->data_size = decoded.data_size;

    if (metadata && metadata->struct_size >= sizeof(*metadata)) {
        const uint32_t caller_size = metadata->struct_size;
        std::memset(metadata, 0, sizeof(*metadata));
        metadata->struct_size = caller_size;
        metadata->version = RIDE_CODEC_IMAGE_METADATA_VERSION;
        std::snprintf(metadata->format_name, sizeof(metadata->format_name), "%s",
                      "HEIC");

        char src[64];
        if (info.grayscale) {
            std::snprintf(src, sizeof(src), "Grayscale %d-bit", info.luma_bits);
        } else {
            std::snprintf(src, sizeof(src), "YCbCr %d-bit", info.luma_bits);
        }
        std::snprintf(metadata->pixel_format, sizeof(metadata->pixel_format), "%s",
                      src);

        // bits_per_pixel: точное значение требует знания субдискретизации
        // хромы исходника, которой в API handle нет; честно оставляем 0
        // для цветных, для градаций серого известна точно.
        metadata->bits_per_pixel =
            info.grayscale ? uint32_t(info.luma_bits) : 0u;
        metadata->palette_entries = 0;
        metadata->flags =
            info.has_alpha ? RIDE_CODEC_IMAGE_META_ALPHA : 0u;
    }

    return 1;
}

} // namespace

extern "C" {

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetInfo(RideCodecInfo* info) {
    if (!info || info->struct_size < sizeof(RideCodecInfo)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = RIDE_CODEC_API_VERSION;
    std::snprintf(info->name, sizeof(info->name), "%s", "HEIC (HEIF/HEVC)");
    std::snprintf(info->extensions, sizeof(info->extensions), "%s", "heic;heif;hif");
    return 1;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetId(char* output,
                                                          uint32_t capacity) {
    if (!output || capacity < 5) return 0; // "heic" + нулевой байт
    std::memcpy(output, "heic", 5);
    return 1;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecProbe(const uint8_t* data,
                                                          size_t size) {
    return heic::probe(data, size) ? 1u : 0u;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecode(
    const uint8_t* data, size_t size, RideImage* image, RideCodecError* error) {
    return decode_common(data, size, image, nullptr, error);
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecDecodeEx(
    const uint8_t* data, size_t size, RideImage* image,
    RideImageMetadata* metadata, RideCodecError* error) {
    return decode_common(data, size, image, metadata, error);
}

RIDE_CODEC_EXPORT void RIDE_CODEC_CALL RideCodecFreeImage(RideImage* image) {
    if (!image) return;
    // Память выделена этой DLL (std::malloc в heic::decode).
    std::free(image->pixels);
    image->pixels = nullptr;
    image->data_size = 0;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
    image->pixel_format = 0;
}

RIDE_CODEC_EXPORT uint32_t RIDE_CODEC_CALL RideCodecGetCapabilities(
    RideCodecCapabilities* caps) {
    if (!caps || caps->struct_size < sizeof(*caps)) return 0;
    const uint32_t caller_size = caps->struct_size;
    std::memset(caps, 0, sizeof(*caps));
    caps->struct_size = caller_size;
    caps->version = RIDE_CODEC_CAPABILITIES_VERSION;
    caps->flags = RIDE_CODEC_CAP_DECODE | RIDE_CODEC_CAP_RGB |
                  RIDE_CODEC_CAP_RGBA | RIDE_CODEC_CAP_ALPHA;
    caps->input_pixel_formats = 0;   // кодирование не поддерживается
    caps->output_pixel_formats = RIDE_CODEC_PIXEL_RGBA8;
    caps->max_width = 32768;
    caps->max_height = 32768;
    return 1;
}

} // extern "C"
