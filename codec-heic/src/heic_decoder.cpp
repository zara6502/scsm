#include "heic_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "libheif/heif.h"

namespace heic {

namespace {

// Ограничения на размер декодируемого изображения: 128 Мпикс = до 512 МиБ
// под выходной RGBA-буфер. Защита от исчерпания памяти на crafted-файлах.
constexpr uint64_t kMaxPixels = 128ull * 1024 * 1024;
constexpr uint64_t kMaxTotalMemory = 1024ull * 1024 * 1024; // 1 GiB внутри libheif

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool brand_is(const uint8_t* p, const char* b) {
    return p[0] == uint8_t(b[0]) && p[1] == uint8_t(b[1]) &&
           p[2] == uint8_t(b[2]) && p[3] == uint8_t(b[3]);
}

// Бренды семейства HEVC/HEIF: сильное совпадение.
bool brand_hevc_family(const uint8_t* p) {
    return brand_is(p, "heic") || brand_is(p, "heix") || brand_is(p, "heim") ||
           brand_is(p, "heis") || brand_is(p, "hevc") || brand_is(p, "hevm") ||
           brand_is(p, "hevs");
}

// Универсальные контейнерные бренды: слабое совпадение.
bool brand_heif_container(const uint8_t* p) {
    return brand_is(p, "mif1") || brand_is(p, "mif2") || brand_is(p, "msf1") ||
           brand_is(p, "miaf");
}

// Бренды AV1/AVIF: файлы с ними должен забирать AVIF-кодек, не мы.
bool brand_av1_family(const uint8_t* p) {
    return brand_is(p, "avif") || brand_is(p, "avis") || brand_is(p, "avio") ||
           brand_is(p, "av01");
}

struct ContextGuard {
    heif_context* ctx;
    ~ContextGuard() { heif_context_free(ctx); }
};

struct HandleGuard {
    heif_image_handle* handle;
    ~HandleGuard() { heif_image_handle_release(handle); }
};

struct ImageGuard {
    heif_image* img;
    ~ImageGuard() { heif_image_release(img); }
};

} // namespace

bool probe(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (!brand_is(data + 4, "ftyp")) return false;

    // Граница бокса ftyp: размер в big-endian; 0 = до конца файла,
    // 1 = 64-битный largesize (для ftyp экзотика) — в обоих случаях
    // ограничиваемся размером входного буфера.
    const uint64_t box_size = read_be32(data);
    const size_t brands_begin = 16; // major(4) + minor(4) после size+type
    size_t brands_end;
    if (box_size == 0 || box_size == 1) {
        brands_end = size;
    } else {
        const uint64_t end = 8 + box_size; // бокс начинается с offset 0
        if (end > size) brands_end = size;
        else brands_end = static_cast<size_t>(end);
    }
    if (brands_end < brands_begin) brands_end = brands_begin;

    // major brand
    const uint8_t* major = data + 8;
    bool strong = brand_hevc_family(major);
    bool container = brand_heif_container(major);
    bool av1 = brand_av1_family(major);

    // compatible brands (не более 64, чтобы не крутиться на мусоре)
    size_t count = 0;
    for (size_t off = brands_begin; off + 4 <= brands_end && count < 64;
         off += 4, ++count) {
        const uint8_t* b = data + off;
        strong = strong || brand_hevc_family(b);
        container = container || brand_heif_container(b);
        av1 = av1 || brand_av1_family(b);
    }

    if (av1) return false;              // AVIF — не наш формат
    return strong || container;
}

bool decode(const uint8_t* data, size_t size,
            DecodedImage& out, SourceInfo& info, std::string& err) {
    out = DecodedImage{};
    info = SourceInfo{};

    heif_context* raw_ctx = heif_context_alloc();
    if (!raw_ctx) {
        err = "heif_context_alloc failed";
        return false;
    }
    ContextGuard ctx_guard{raw_ctx};

    // Лимиты памяти до чтения файла.
    if (heif_security_limits* lim = heif_context_get_security_limits(raw_ctx)) {
        if (lim->version >= 1) {
            lim->max_image_size_pixels = kMaxPixels;
        }
        if (lim->version >= 2) {
            lim->max_total_memory = kMaxTotalMemory;
        }
    }

    heif_error e = heif_context_read_from_memory_without_copy(raw_ctx, data, size, nullptr);
    if (e.code != heif_error_Ok) {
        err = e.message ? e.message : "failed to read HEIF context";
        return false;
    }

    heif_image_handle* raw_handle = nullptr;
    e = heif_context_get_primary_image_handle(raw_ctx, &raw_handle);
    if (e.code != heif_error_Ok) {
        err = e.message ? e.message : "no primary image";
        return false;
    }
    HandleGuard handle_guard{raw_handle};

    info.has_alpha = heif_image_handle_has_alpha_channel(raw_handle) != 0;
    info.luma_bits = heif_image_handle_get_luma_bits_per_pixel(raw_handle);
    info.chroma_bits = heif_image_handle_get_chroma_bits_per_pixel(raw_handle);
    info.grayscale = info.chroma_bits == 0;

    heif_decoding_options* opt = heif_decoding_options_alloc();
    if (!opt) {
        err = "heif_decoding_options_alloc failed";
        return false;
    }
    opt->version = 2;
    opt->convert_hdr_to_8bit = 1; // 10-бит -> 8-бит при выводе
    // ignore_transformations == 0 (по умолчанию): libheif сам применяет
    // поворот и зеркалирование из irot/imir.

    heif_image* raw_img = nullptr;
    e = heif_decode_image(raw_handle, &raw_img, heif_colorspace_RGB,
                          heif_chroma_interleaved_RGBA, opt);
    heif_decoding_options_free(opt);
    if (e.code != heif_error_Ok) {
        err = e.message ? e.message : "heif_decode_image failed";
        return false;
    }
    ImageGuard img_guard{raw_img};

    // Размеры берём у декодированного изображения: для повёрнутых
    // файлов они отличаются от ispe в handle.
    const int w = heif_image_get_width(raw_img, heif_channel_interleaved);
    const int h = heif_image_get_height(raw_img, heif_channel_interleaved);
    if (w <= 0 || h <= 0) {
        err = "decoded image has zero size";
        return false;
    }
    if (uint64_t(w) * uint64_t(h) > kMaxPixels) {
        err = "image too large";
        return false;
    }

    const uint64_t stride64 = uint64_t(w) * 4u;
    if (stride64 > 0xFFFFFFFFull) { // RideImage::stride — uint32_t
        err = "image row too wide";
        return false;
    }
    const size_t stride = static_cast<size_t>(stride64);
    const size_t data_size = stride * size_t(h);

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(data_size));
    if (!buf) {
        err = "out of memory";
        return false;
    }

    int src_stride = 0;
    const uint8_t* src =
        heif_image_get_plane_readonly(raw_img, heif_channel_interleaved, &src_stride);
    if (!src || src_stride < w * 4) {
        std::free(buf);
        err = "invalid interleaved plane";
        return false;
    }

    const size_t row_bytes = size_t(w) * 4u;
    for (int y = 0; y < h; ++y) {
        const uint8_t* srow = src + size_t(y) * size_t(src_stride);
        uint8_t* drow = buf + size_t(y) * stride;
        std::memcpy(drow, srow, row_bytes);
        if (!info.has_alpha) {
            for (size_t x = 3; x < row_bytes; x += 4) {
                drow[x] = 255; // контракт: непрозрачные пиксели — alpha 255
            }
        }
    }

    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.stride = uint32_t(stride);
    out.pixels = buf;
    out.data_size = data_size;
    return true;
}

void free_image(DecodedImage& img) {
    std::free(img.pixels);
    img.pixels = nullptr;
    img.data_size = 0;
    img.width = img.height = img.stride = 0;
}

} // namespace heic
