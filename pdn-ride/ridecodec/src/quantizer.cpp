#include "quantizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_map>

namespace rideq {

namespace {

// ---- sRGB (0..255) <-> Oklab (Björn Ottosson) ----

struct Oklab {
    float L, a, b;
};

Oklab srgb_to_oklab(int r8, int g8, int b8) {
    auto lin = [](int c) -> float {
        float v = c / 255.0f;
        return v <= 0.04045f ? v / 12.92f
                             : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    float r = lin(r8), g = lin(g8), bl = lin(b8);

    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * bl;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * bl;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * bl;

    l = std::cbrt(l); m = std::cbrt(m); s = std::cbrt(s);
    return {
        0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
        1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
        0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
    };
}

void oklab_to_srgb(const Oklab& lab, uint8_t out[3]) {
    float l_ = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
    float m_ = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
    float s_ = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    float r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    auto gam = [](float v) -> uint8_t {
        v = v <= 0.0031308f ? v * 12.92f
                            : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        int i = (int)std::lround(v * 255.0f);
        return (uint8_t)std::clamp(i, 0, 255);
    };
    out[0] = gam(r); out[1] = gam(g); out[2] = gam(b);
}

// ---- уникальные цвета ----

struct UniqueColor {
    uint8_t rgb[3];
    uint64_t count;
    Oklab lab;
};

std::vector<UniqueColor> collect_unique(const uint8_t* bgra, uint32_t w,
                                        uint32_t h) {
    std::unordered_map<uint32_t, size_t> map;
    map.reserve(1 << 16);
    std::vector<UniqueColor> out;
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = bgra[i * 4 + 2], g = bgra[i * 4 + 1],
                       b = bgra[i * 4];
        const uint32_t key = r << 16 | g << 8 | b;
        auto it = map.find(key);
        if (it == map.end()) {
            map[key] = out.size();
            UniqueColor uc;
            uc.rgb[0] = (uint8_t)r; uc.rgb[1] = (uint8_t)g;
            uc.rgb[2] = (uint8_t)b;
            uc.count = 1;
            uc.lab = srgb_to_oklab((int)r, (int)g, (int)b);
            out.push_back(uc);
        } else {
            ++out[it->second].count;
        }
    }
    return out;
}

// ---- median cut (взвешенный, в Oklab) ----

struct Box {
    size_t begin, end; // диапазон [begin, end) в отсортированном массиве
    uint64_t weight = 0;
    float split_range = 0; // приоритет: размах по самой длинной оси
    int axis = 0;
};

// Сортировка диапазона по оси и вычисление метрик бокса.
void prepare_box(const std::vector<UniqueColor>& colors,
                 std::vector<size_t>& order, Box& box) {
    auto axis_of = [&](const UniqueColor& c, int axis) -> float {
        return axis == 0 ? c.lab.L : axis == 1 ? c.lab.a : c.lab.b;
    };
    float max_range = -1;
    int best_axis = 0;
    for (int axis = 0; axis < 3; ++axis) {
        std::sort(order.begin() + (long)box.begin,
                  order.begin() + (long)box.end,
                  [&](size_t a, size_t b) {
                      return axis_of(colors[a], axis) <
                             axis_of(colors[b], axis);
                  });
        float minv = axis_of(colors[order[box.begin]], axis);
        float maxv = axis_of(colors[order[box.end - 1]], axis);
        if (maxv - minv > max_range) {
            max_range = maxv - minv;
            best_axis = axis;
        }
    }
    box.axis = best_axis;
    box.split_range = max_range;
    box.weight = 0;
    for (size_t i = box.begin; i < box.end; ++i)
        box.weight += colors[order[i]].count;
}

uint8_t clamp255(int v) {
    return (uint8_t)std::clamp(v, 0, 255);
}

} // namespace

size_t count_unique_colors(const uint8_t* bgra, uint32_t w, uint32_t h) {
    std::unordered_map<uint32_t, size_t> map;
    const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) {
        uint32_t key = (uint32_t)bgra[i * 4 + 2] << 16 |
                       (uint32_t)bgra[i * 4 + 1] << 8 | bgra[i * 4];
        map[key] = 1;
    }
    return map.size();
}

bool quantize_indexed(const uint8_t* bgra, uint32_t w, uint32_t h,
                      uint32_t max_colors, bool dither,
                      ride::IndexedImage& out) {
    out = ride::IndexedImage{};
    out.width = w;
    out.height = h;
    if (!bgra || w == 0 || h == 0 || max_colors == 0) return false;
    max_colors = std::min<uint32_t>(max_colors, 256);

    auto unique = collect_unique(bgra, w, h);
    const uint32_t unique_count = (uint32_t)unique.size();

    // Точная палитра — квантование не нужно.
    std::vector<size_t> order(unique_count);
    for (size_t i = 0; i < unique_count; ++i) order[i] = i;

    if (unique_count <= max_colors) {
        // точная палитра по частоте (как ride::to_indexed по частоте)
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return unique[a].count > unique[b].count;
        });
        for (size_t i = 0; i < unique_count; ++i) {
            out.palette.push_back(unique[order[i]].rgb[0]);
            out.palette.push_back(unique[order[i]].rgb[1]);
            out.palette.push_back(unique[order[i]].rgb[2]);
        }
    } else {
        // --- median cut ---
        // сортируем по L для стартового разбиения
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return unique[a].lab.L < unique[b].lab.L;
        });
        Box root{0, unique_count};
        prepare_box(unique, order, root);
        auto cmp = [&](const Box& a, const Box& b) {
            return a.split_range * (double)a.weight <
                   b.split_range * (double)b.weight;
        };
        std::priority_queue<Box, std::vector<Box>, decltype(cmp)> boxes(cmp);
        boxes.push(root);
        while (boxes.size() < max_colors) {
            Box b = boxes.top();
            boxes.pop();
            if (b.end - b.begin < 2 || b.split_range <= 0) {
                // неразделимый бокс — оставляем; таких боксов не больше
                // числа уникальных цветов
                boxes.push(b);
                if (boxes.size() >= unique_count) break;
                // если верх приоритетной очереди неразделим — выходим
                if (boxes.top().split_range <= 0 ||
                    boxes.top().end - boxes.top().begin < 2)
                    break;
                continue;
            }
            // взвешенная медиана по оси
            const int axis = b.axis;
            auto axis_val = [&](size_t idx) -> float {
                const Oklab& c = unique[order[idx]].lab;
                return axis == 0 ? c.L : axis == 1 ? c.a : c.b;
            };
            uint64_t total = 0;
            for (size_t i = b.begin; i < b.end; ++i)
                total += unique[order[i]].count;
            uint64_t acc = 0;
            size_t split = b.begin;
            for (size_t i = b.begin; i < b.end; ++i) {
                acc += unique[order[i]].count;
                if (acc * 2 >= total) { split = i + 1; break; }
            }
            if (split <= b.begin || split >= b.end) split = b.begin + 1;

            Box a1{b.begin, split};
            Box a2{split, b.end};
            prepare_box(unique, order, a1);
            prepare_box(unique, order, a2);
            boxes.push(a1);
            boxes.push(a2);
        }

        // центроиды боксов -> палитра (sRGB)
        uint32_t pal_count = 0;
        std::vector<Oklab> pal_lab;
        while (!boxes.empty()) {
            const Box& b = boxes.top();
            float L = 0, A = 0, B = 0;
            uint64_t wsum = 0;
            for (size_t i = b.begin; i < b.end; ++i) {
                const auto& uc = unique[order[i]];
                L += uc.lab.L * (float)uc.count;
                A += uc.lab.a * (float)uc.count;
                B += uc.lab.b * (float)uc.count;
                wsum += uc.count;
            }
            Oklab lab = wsum ? Oklab{L / (float)wsum, A / (float)wsum,
                                     B / (float)wsum}
                             : Oklab{};
            uint8_t rgb[3];
            oklab_to_srgb(lab, rgb);
            out.palette.push_back(rgb[0]);
            out.palette.push_back(rgb[1]);
            out.palette.push_back(rgb[2]);
            pal_lab.push_back(lab);
            ++pal_count;
            boxes.pop();
        }

        // --- k-means (3 итерации, ΔE = квадрат расстояния в Oklab) ---
        // палитра хранится как sRGB-байты (то, что попадёт в файл);
        // пересчёт центроид в Oklab по назначенным уникальным цветам
        auto dist2 = [&](const Oklab& x, const Oklab& y) {
            float dl = x.L - y.L, da = x.a - y.a, db = x.b - y.b;
            return dl * dl + da * da + db * db;
        };
        for (int iter = 0; iter < 3; ++iter) {
            // пересчитать pal_lab из текущих sRGB-байтов палитры
            pal_lab.clear();
            for (uint32_t i = 0; i < pal_count; ++i) {
                pal_lab.push_back(srgb_to_oklab(
                    out.palette[i * 3], out.palette[i * 3 + 1],
                    out.palette[i * 3 + 2]));
            }
            std::vector<double> accL(pal_count, 0), accA(pal_count, 0),
                accB(pal_count, 0);
            std::vector<uint64_t> accW(pal_count, 0);
            bool changed = false;
            for (const auto& uc : unique) {
                size_t best = 0;
                float best_d = dist2(uc.lab, pal_lab[0]);
                for (uint32_t i = 1; i < pal_count; ++i) {
                    float d = dist2(uc.lab, pal_lab[i]);
                    if (d < best_d) { best_d = d; best = i; }
                }
                accL[best] += uc.lab.L * (double)uc.count;
                accA[best] += uc.lab.a * (double)uc.count;
                accB[best] += uc.lab.b * (double)uc.count;
                accW[best] += uc.count;
            }
            for (uint32_t i = 0; i < pal_count; ++i) {
                if (accW[i] == 0) continue;
                Oklab centroid{(float)(accL[i] / accW[i]),
                               (float)(accA[i] / accW[i]),
                               (float)(accB[i] / accW[i])};
                uint8_t rgb[3];
                oklab_to_srgb(centroid, rgb);
                if (rgb[0] != out.palette[i * 3] ||
                    rgb[1] != out.palette[i * 3 + 1] ||
                    rgb[2] != out.palette[i * 3 + 2]) changed = true;
                out.palette[i * 3] = rgb[0];
                out.palette[i * 3 + 1] = rgb[1];
                out.palette[i * 3 + 2] = rgb[2];
            }
            if (!changed) break;
        }
    }

    // глубина: минимальные биты, вмещающие палитру
    uint32_t pal_colors = (uint32_t)(out.palette.size() / 3);
    uint8_t depth = 1;
    while ((1u << depth) < pal_colors && depth < 8) ++depth;
    out.depth = depth;

    // ---- маппинг пикселей (с опциональным Floyd–Steinberg) ----
    // ближайший цвет палитры в Oklab; кеш по точному RGB
    uint32_t pal_colors_final = (uint32_t)(out.palette.size() / 3);
    std::vector<Oklab> pal_lab(pal_colors_final);
    for (uint32_t i = 0; i < pal_colors_final; ++i)
        pal_lab[i] = srgb_to_oklab(out.palette[i * 3],
                                   out.palette[i * 3 + 1],
                                   out.palette[i * 3 + 2]);

    out.indices.assign((size_t)w * h, 0);

    if (!dither) {
        // прямое сопоставление по уникальным цветам (быстро)
        std::unordered_map<uint32_t, uint8_t> cache;
        const size_t n = (size_t)w * h;
        for (size_t i = 0; i < n; ++i) {
            uint32_t key = (uint32_t)bgra[i * 4 + 2] << 16 |
                           (uint32_t)bgra[i * 4 + 1] << 8 | bgra[i * 4];
            auto it = cache.find(key);
            if (it != cache.end()) {
                out.indices[i] = it->second;
                continue;
            }
            Oklab lab = srgb_to_oklab(bgra[i * 4 + 2], bgra[i * 4 + 1],
                                      bgra[i * 4]);
            size_t best = 0;
            float best_d = 1e30f;
            for (uint32_t pi = 0; pi < pal_colors_final; ++pi) {
                float dl = lab.L - pal_lab[pi].L;
                float da = lab.a - pal_lab[pi].a;
                float db = lab.b - pal_lab[pi].b;
                float d = dl * dl + da * da + db * db;
                if (d < best_d) { best_d = d; best = pi; }
            }
            out.indices[i] = (uint8_t)best;
            cache[key] = (uint8_t)best;
        }
        return true;
    }

    // Floyd–Steinberg: последовательно по строкам, ошибки в float
    const float matrix[4] = {7.0f / 16, 3.0f / 16, 5.0f / 16, 1.0f / 16};
    std::vector<float> err_cur((size_t)w * 3 + 3, 0);
    std::vector<float> err_next((size_t)w * 3 + 3, 0);

    for (uint32_t y = 0; y < h; ++y) {
        std::fill(err_next.begin(), err_next.end(), 0.0f);
        for (uint32_t x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const int o = (int)(x * 3);
            float adj[3];
            for (int c = 0; c < 3; ++c)
                adj[c] = std::clamp(
                    bgra[i * 4 + (c == 0 ? 2 : c == 1 ? 1 : 0)] +
                        err_cur[o + c],
                    0.0f, 255.0f);

            // ближайший цвет палитры к adj (sRGB->Oklab)
            Oklab lab = srgb_to_oklab((int)adj[0], (int)adj[1],
                                      (int)adj[2]);
            uint8_t chosen[3] = {0, 0, 0};
            size_t best = 0;
            float best_d = 1e30f;
            for (uint32_t pi = 0; pi < pal_colors_final; ++pi) {
                float dl = lab.L - pal_lab[pi].L;
                float da = lab.a - pal_lab[pi].a;
                float db = lab.b - pal_lab[pi].b;
                float d = dl * dl + da * da + db * db;
                if (d < best_d) {
                    best_d = d;
                    best = pi;
                    chosen[0] = out.palette[pi * 3];
                    chosen[1] = out.palette[pi * 3 + 1];
                    chosen[2] = out.palette[pi * 3 + 2];
                }
            }
            out.indices[i] = (uint8_t)best;

            // распространение ошибки (значения ПАЛИТРЫ минус adj)
            for (int c = 0; c < 3; ++c) {
                float e = (float)chosen[c] - adj[c];
                if (x + 1 < w) err_cur[o + 3 + c] += e * matrix[0];
                if (y + 1 < h) {
                    if (x > 0) err_next[o - 3 + c] += e * matrix[1];
                    err_next[o + c] += e * matrix[2];
                    if (x + 1 < w) err_next[o + 3 + c] += e * matrix[3];
                }
            }
        }
        err_cur.swap(err_next);
    }

    return true;
}

} // namespace rideq
