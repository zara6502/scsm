// Smoke-тест RideCodec.dll: encode -> decode -> сравнение BGRA,
// на синтетических картинках (solid / gradient / шум / 1x1).
// Запуск: codec_smoke.exe  (использует RideCodec.dll рядом)
#include "ride_codec/ride_codec.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++failures;
}

static uint32_t rng = 12345;
static uint8_t rnd() {
    rng = rng * 1103515245 + 12345;
    return (uint8_t)(rng >> 16);
}

static void run_case(const char* name, std::vector<uint8_t>& bgra,
                     uint32_t w, uint32_t h,
                     const RideEncodeOptions* opts) {
    RideError err{};
    uint8_t* ride_data = nullptr;
    size_t ride_size = 0;

    uint32_t r = RideCodecEncode(bgra.data(), w, h, opts, &ride_data,
                                 &ride_size, &err);
    if (r != 0) {
        std::printf("[FAIL] %s: encode err %u (%s)\n", name, err.code,
                    err.message);
        ++failures;
        return;
    }
    check(ride_size > 14, (std::string(name) + ": encode").c_str());

    uint32_t dw = 0, dh = 0;
    uint8_t* out = nullptr;
    r = RideCodecDecode(ride_data, ride_size, &dw, &dh, nullptr, &out, &err);
    if (r != 0 || dw != w || dh != h || !out) {
        std::printf("[FAIL] %s: decode err %u (%s)\n", name, err.code,
                    err.message);
        ++failures;
        RideCodecFree(ride_data);
        return;
    }

    if (opts && opts->struct_size >= sizeof(RideEncodeOptions) &&
        opts->quantize_colors > 0) {
        // квантованный INDEXED: декод == палитра[индексы]; проверяем,
        // что уникальных цветов декода не больше quantize_colors
        std::vector<uint8_t> seen;
        uint32_t distinct = 0;
        for (size_t i = 0; i < (size_t)w * h * 4; i += 4) {
            bool found = false;
            for (size_t k = 0; k < seen.size(); k += 3)
                if (seen[k] == out[i] && seen[k + 1] == out[i + 1] &&
                    seen[k + 2] == out[i + 2])
                { found = true; break; }
            if (!found) {
                seen.push_back(out[i]);
                seen.push_back(out[i + 1]);
                seen.push_back(out[i + 2]);
                ++distinct;
            }
        }
        check(distinct <= opts->quantize_colors,
              (std::string(name) + ": <= palette colors").c_str());
    } else {
        bool same = memcmp(bgra.data(), out, (size_t)w * h * 4) == 0;
        check(same, (std::string(name) + ": roundtrip BGRA").c_str());
    }

    RideCodecFree(out);
    RideCodecFree(ride_data);
}

int main() {
    check(RideCodecVersion() != 0, "RideCodecVersion");
    std::printf("codec version: %u.%u\n", RideCodecVersion() >> 16,
                RideCodecVersion() & 0xFFFF);

    // solid 64x64
    {
        std::vector<uint8_t> b(64 * 64 * 4, 0);
        for (size_t i = 0; i < b.size(); i += 4) {
            b[i] = 30; b[i + 1] = 144; b[i + 2] = 255; b[i + 3] = 255;
        }
        run_case("solid 64x64", b, 64, 64, nullptr);
    }
    // gradient 128x128
    {
        std::vector<uint8_t> b(128 * 128 * 4, 0);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                size_t o = (size_t)(y * 128 + x) * 4;
                b[o] = (uint8_t)x * 2; b[o + 1] = (uint8_t)y * 2;
                b[o + 2] = 128; b[o + 3] = 255;
            }
        run_case("gradient 128x128", b, 128, 128, nullptr);
    }
    // noise 200x150 (RGB profile: слишком много цветов для INDEXED)
    {
        std::vector<uint8_t> b(200 * 150 * 4, 0);
        for (size_t i = 0; i < b.size(); i += 4) {
            b[i] = rnd(); b[i + 1] = rnd(); b[i + 2] = rnd(); b[i + 3] = 255;
        }
        run_case("noise 200x150", b, 200, 150, nullptr);
    }
    // шум + явный профиль RGB + entropy on
    {
        std::vector<uint8_t> b(100 * 80 * 4, 0);
        for (size_t i = 0; i < b.size(); i += 4) {
            b[i] = rnd(); b[i + 1] = rnd(); b[i + 2] = rnd(); b[i + 3] = 255;
        }
        RideEncodeOptions o{};
        o.struct_size = sizeof(o);
        o.profile_choice = RIDE_PROFILE_RGB;
        o.entropy = RIDE_ENTROPY_ON;
        run_case("noise RGB+entropy", b, 100, 80, &o);
    }
    // 4 цвета -> INDEXED
    {
        std::vector<uint8_t> b(80 * 80 * 4, 0);
        const uint8_t pal[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,0}};
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 80; ++x) {
                size_t o = (size_t)(y * 80 + x) * 4;
                const uint8_t* c = pal[(x / 20 + y / 20) % 4];
                b[o] = c[2]; b[o + 1] = c[1]; b[o + 2] = c[0]; b[o + 3] = 255;
            }
        run_case("4-color INDEXED", b, 80, 80, nullptr);

        // квантование: фото-подобный шум (>256 цветов) -> Indexed, N=32
        std::vector<uint8_t> n2(200 * 150 * 4, 0);
        for (size_t i = 0; i < n2.size(); i += 4) {
            n2[i] = rnd(); n2[i + 1] = rnd(); n2[i + 2] = rnd();
            n2[i + 3] = 255;
        }
        RideEncodeOptions qo{};
        qo.struct_size = sizeof(qo);           // v2: с quantize_colors
        qo.profile_choice = RIDE_PROFILE_INDEXED;
        qo.quantize_colors = 32;
        run_case("quantize 32 colors", n2, 200, 150, &qo);
        // то же с Floyd–Steinberg
        RideEncodeOptions qd = qo;
        qd.dither = 1;
        run_case("quantize 32 + FS", n2, 200, 150, &qd);
    }
    // 1x1
    {
        std::vector<uint8_t> b(4, 0);
        b[0] = 9; b[1] = 8; b[2] = 7; b[3] = 255;
        run_case("1x1", b, 1, 1, nullptr);
    }
    // мусор на декодирование -> аккуратная ошибка
    {
        uint8_t junk[64];
        memset(junk, 0xAB, sizeof(junk));
        uint32_t w = 0, h = 0;
        uint8_t* out = nullptr;
        RideError err{};
        uint32_t r = RideCodecDecode(junk, sizeof(junk), &w, &h, nullptr,
                                     &out, &err);
        check(r != 0 && err.code != 0 && out == nullptr,
              "decode garbage -> error");
        RideCodecFree(out);
    }
    // null-аргументы
    {
        RideError err{};
        uint8_t* out = nullptr;
        size_t sz = 0;
        check(RideCodecEncode(nullptr, 10, 10, nullptr, &out, &sz, &err) != 0,
              "encode null -> error");
        check(RideCodecEncode(nullptr, 10, 10, nullptr, nullptr, nullptr,
                              nullptr) != 0,
              "encode null out -> error");
    }

    RideCodecFree(nullptr); // null безопасен
    std::printf(failures == 0 ? "\nВСЁ ПРОШЛО\n" : "\nОШИБОК: %d\n", failures);
    return failures;
}
