#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace heic {

// Сведения об исходном изображении (для RideImageMetadata).
struct SourceInfo {
    bool has_alpha = false;
    bool grayscale = false;
    int luma_bits = 0;
    int chroma_bits = 0;
};

// Нормализованный результат декодирования: top-down RGBA8, прямая альфа.
struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;        // width * 4
    uint8_t* pixels = nullptr;  // буфер из std::malloc
    size_t data_size = 0;       // stride * height
};

// Быстрая проверка HEIC/HEIF по брендам ftyp, без libheif.
bool probe(const uint8_t* data, size_t size);

// Полное декодирование первичного изображения в RGBA8.
// При ошибке возвращает false и заполняет err; out остаётся пустым.
bool decode(const uint8_t* data, size_t size,
            DecodedImage& out, SourceInfo& info, std::string& err);

// Освобождает буфер DecodedImage (безопасна и для пустой структуры).
void free_image(DecodedImage& img);

} // namespace heic
