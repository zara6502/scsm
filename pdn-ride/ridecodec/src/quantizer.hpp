// Квантование изображений до палитры (для профиля INDEXED).
// Median Cut в Oklab + k-means (ΔE в Oklab) + опциональный Floyd–Steinberg.
// Реализовано в RideCodec (обёртке), rideent не изменяется.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ride/ride.hpp"

namespace rideq {

// Число уникальных цветов BGRA-буфера (альфа игнорируется).
size_t count_unique_colors(const uint8_t* bgra, uint32_t w, uint32_t h);

// Квантование до палитры не более max_colors цветов. Если уникальных
// цветов <= max_colors, палитра точная (квантование не выполняется).
// dither: Floyd–Steinberg при фактическом квантовании.
// Возвращает false при нехватке памяти.
bool quantize_indexed(const uint8_t* bgra, uint32_t w, uint32_t h,
                      uint32_t max_colors, bool dither,
                      ride::IndexedImage& out);

} // namespace rideq
