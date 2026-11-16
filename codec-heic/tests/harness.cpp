// Тестовый стенд кодека HEIC: загружает готовую DLL и проверяет
// контракт Ride Codec ABI (см. docs/CODEC_API.ru.md).
//
// Использование: codec_test <путь к ride_codec_heic.dll> <путь к .heic> [выход.bmp]
// Код возврата — число неудачных проверок.

#include "ride_viewer_codec_api.h"

#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Codec {
    HMODULE mod = nullptr;
    RideCodecGetInfoFn get_info = nullptr;
    RideCodecProbeFn probe = nullptr;
    RideCodecDecodeFn decode = nullptr;
    RideCodecDecodeExFn decode_ex = nullptr;
    RideCodecFreeImageFn free_image = nullptr;
    RideCodecGetIdFn get_id = nullptr;
    RideCodecGetCapabilitiesFn get_capabilities = nullptr;
};

void resolve(Codec& c) {
    c.get_info = reinterpret_cast<RideCodecGetInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecGetInfo")));
    c.probe = reinterpret_cast<RideCodecProbeFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecProbe")));
    c.decode = reinterpret_cast<RideCodecDecodeFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecDecode")));
    c.decode_ex = reinterpret_cast<RideCodecDecodeExFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecDecodeEx")));
    c.free_image = reinterpret_cast<RideCodecFreeImageFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecFreeImage")));
    c.get_id = reinterpret_cast<RideCodecGetIdFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecGetId")));
    c.get_capabilities = reinterpret_cast<RideCodecGetCapabilitiesFn>(
        reinterpret_cast<void*>(GetProcAddress(c.mod, "RideCodecGetCapabilities")));
}

bool export_absent(const Codec& c, const char* name) {
    return GetProcAddress(c.mod, name) == nullptr;
}

bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(n));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

void put_be32(uint8_t* p, uint32_t v) { // ISO BMFF (ftyp) — big-endian
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

void put_le16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

void put_le32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

// 24-битный BMP (снизу вверх, строки выровнены на 4 байта).
bool write_bmp(const char* path, const RideImage& img) {
    const size_t row = (size_t(img.width) * 3u + 3u) & ~size_t(3);
    const size_t pixels = row * img.height;
    const size_t total = 14 + 40 + pixels;
    std::vector<uint8_t> bmp(total, 0);

    bmp[0] = 'B';
    bmp[1] = 'M';
    put_le32(bmp.data() + 2, uint32_t(total));
    put_le32(bmp.data() + 10, uint32_t(14 + 40));
    uint8_t* h = bmp.data() + 14;
    put_le32(h + 0, 40);                 // BITMAPINFOHEADER
    put_le32(h + 4, img.width);
    put_le32(h + 8, img.height);
    put_le16(h + 12, 1);                 // planes
    put_le16(h + 14, 24);                // bpp
    put_le32(h + 16, 0);                 // BI_RGB
    put_le32(h + 20, uint32_t(pixels));

    for (uint32_t y = 0; y < img.height; ++y) {
        const uint8_t* src = img.pixels + size_t(img.stride) * size_t(y);
        uint8_t* dst = bmp.data() + 14 + 40 + row * size_t(img.height - 1 - y);
        for (uint32_t x = 0; x < img.width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2]; // B
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 0]; // R
        }
    }

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: codec_test <codec.dll> <sample.heic> [out.bmp]\n");
        return 1;
    }
    const char* dll_path = argv[1];
    const char* sample_path = argv[2];
    const char* bmp_path = argc > 3 ? argv[3] : "decoded.bmp";

    Codec c;
    c.mod = LoadLibraryA(dll_path);
    if (!c.mod) {
        std::printf("[FAIL] LoadLibrary: %lu\n", GetLastError());
        return 1;
    }
    resolve(c);

    // --- 1. Таблица экспортов ---------------------------------------------
    check(c.get_info && c.probe && c.decode && c.decode_ex && c.free_image &&
              c.get_id && c.get_capabilities,
          "все 7 экспортов кодека присутствуют");
    check(export_absent(c, "RideCodecEncode") &&
              export_absent(c, "RideCodecEncodeEx") &&
              export_absent(c, "RideCodecGetEncodeOptions") &&
              export_absent(c, "RideCodecFreeEncodedData"),
          "экспорты кодирования отсутствуют (декодер)");

    // --- 2. GetInfo / GetId / GetCapabilities ------------------------------
    RideCodecInfo info{};
    info.struct_size = sizeof(info);
    check(c.get_info(&info) != 0, "GetInfo: успех");
    check(info.api_version == RIDE_CODEC_API_VERSION, "GetInfo: api_version == 1");
    check(info.name[0] != 0 && info.extensions[0] != 0,
          "GetInfo: строки непустые и с нулём в конце");
    std::printf("       name='%s' extensions='%s'\n", info.name, info.extensions);

    char id[RIDE_CODEC_ID_CAPACITY] = {};
    check(c.get_id(id, sizeof(id)) != 0 && std::strcmp(id, "heic") == 0,
          "GetId: 'heic'");

    RideCodecCapabilities caps{};
    caps.struct_size = sizeof(caps);
    check(c.get_capabilities(&caps) != 0, "GetCapabilities: успех");
    check(caps.version == RIDE_CODEC_CAPABILITIES_VERSION, "GetCapabilities: версия 2");
    check((caps.flags & RIDE_CODEC_CAP_DECODE) != 0 &&
              (caps.flags & RIDE_CODEC_CAP_RGBA) != 0 &&
              (caps.flags & RIDE_CODEC_CAP_ENCODE) == 0 &&
              caps.output_pixel_formats == RIDE_CODEC_PIXEL_RGBA8,
          "GetCapabilities: DECODE+RGBA8, без ENCODE");
    check(c.get_info(nullptr) == 0 && c.get_capabilities(nullptr) == 0,
          "GetInfo/GetCapabilities: null отклоняются");

    // --- 3. Probe -----------------------------------------------------------
    std::vector<uint8_t> sample;
    check(read_file(sample_path, sample), "чтение тестового .heic");
    if (sample.empty()) return 1;

    check(c.probe(sample.data(), sample.size()) != 0, "Probe: сэмпл распознан");
    check(c.probe(nullptr, 0) == 0, "Probe: null/пусто -> 0");
    std::vector<uint8_t> garbage(1024, 0xAB);
    check(c.probe(garbage.data(), garbage.size()) == 0, "Probe: мусор -> 0");
    check(c.probe(sample.data(), 8) == 0, "Probe: усечённый заголовок -> 0");

    // ftyp с AVIF-брендом — не наш файл.
    std::vector<uint8_t> avif_hdr(sample.begin(), sample.begin() + 32);
    std::memcpy(avif_hdr.data() + 8, "avif", 4);
    check(c.probe(avif_hdr.data(), avif_hdr.size()) == 0,
          "Probe: бренд avif отклоняется");

    // Минимальный ftyp major=mif1 без av1 — распознаём.
    std::vector<uint8_t> mif1(32, 0);
    put_be32(mif1.data(), 32);
    std::memcpy(mif1.data() + 4, "ftyp", 4);
    std::memcpy(mif1.data() + 8, "mif1", 4);
    check(c.probe(mif1.data(), mif1.size()) != 0, "Probe: контейнер mif1 распознан");

    // --- 4. Decode сэмпла ---------------------------------------------------
    RideImage img{};
    img.struct_size = sizeof(img);
    RideCodecError err{};
    check(c.decode(sample.data(), sample.size(), &img, &err) != 0,
          "Decode: сэмпл декодирован");
    if (img.pixels) {
        std::printf("       %ux%u stride=%u fmt=%u data=%zu\n", img.width,
                    img.height, img.stride, img.pixel_format, img.data_size);
        check(img.pixel_format == 1, "Decode: pixel_format == RGBA8");
        check(img.stride >= img.width * 4, "Decode: stride >= width*4");
        check(img.data_size >= size_t(img.stride) * (img.height - 1) +
                                   size_t(img.width) * 4,
              "Decode: data_size покрывает буфер");

        // Выборочная печать пикселей: углы и центр.
        auto px = [&](uint32_t x, uint32_t y) -> const uint8_t* {
            return img.pixels + size_t(img.stride) * y + size_t(x) * 4;
        };
        std::printf("       px(0,0)=#%02X%02X%02X%02X px(c,c)=#%02X%02X%02X%02X\n",
                    px(0, 0)[0], px(0, 0)[1], px(0, 0)[2], px(0, 0)[3],
                    px(img.width / 2, img.height / 2)[0],
                    px(img.width / 2, img.height / 2)[1],
                    px(img.width / 2, img.height / 2)[2],
                    px(img.width / 2, img.height / 2)[3]);

        check(write_bmp(bmp_path, img), "запись BMP для визуальной проверки");

        // --- 5. DecodeEx с метаданными -------------------------------------
        RideImage img2{};
        img2.struct_size = sizeof(img2);
        RideImageMetadata meta{};
        meta.struct_size = sizeof(meta);
        meta.version = RIDE_CODEC_IMAGE_METADATA_VERSION;
        check(c.decode_ex(sample.data(), sample.size(), &img2, &meta, &err) != 0,
              "DecodeEx: успех");
        if (img2.pixels) {
            std::printf("       meta: format='%s' src='%s' bpp=%u flags=0x%X\n",
                        meta.format_name, meta.pixel_format, meta.bits_per_pixel,
                        meta.flags);
            check(meta.version == RIDE_CODEC_IMAGE_METADATA_VERSION,
                  "DecodeEx: версия метаданных");
        }
        c.free_image(&img2);

        // --- 6. Несколько циклов Decode/Free --------------------------------
        bool cycles_ok = true;
        for (int i = 0; i < 3; ++i) {
            RideImage ci{};
            ci.struct_size = sizeof(ci);
            if (c.decode(sample.data(), sample.size(), &ci, nullptr) == 0 ||
                !ci.pixels) {
                cycles_ok = false;
                break;
            }
            c.free_image(&ci);
            c.free_image(&ci); // повторный вызов безопасен
        }
        check(cycles_ok, "Decode/FreeImage: 3 цикла, повторный free безопасен");
    }

    // --- 7. Пути с ошибками -------------------------------------------------
    RideImage bad{};
    bad.struct_size = sizeof(bad);
    RideCodecError e2{};
    check(c.decode(garbage.data(), garbage.size(), &bad, &e2) == 0 &&
              e2.code != 0 && e2.message[0] != 0 && bad.pixels == nullptr,
          "Decode: мусор -> код ошибки, image пуст");
    c.free_image(&bad); // должен выдержать
    check(c.decode(nullptr, 100, &bad, &e2) == 0, "Decode: null-ввод отклонён");

    // ftyp валидный, но содержимое мусорное -> аккуратная ошибка HEIC_ERR_DECODE.
    std::vector<uint8_t> fake(4096, 0x55);
    put_be32(fake.data(), 4096);
    std::memcpy(fake.data() + 4, "ftyp", 4);
    std::memcpy(fake.data() + 8, "heic", 4);
    RideImage bad2{};
    bad2.struct_size = sizeof(bad2);
    check(c.decode(fake.data(), fake.size(), &bad2, &e2) == 0 && e2.code != 0,
          "Decode: фальшивый HEIC -> ошибка, без падения");
    c.free_image(&bad2);

    c.free_image(nullptr); // null безопасен
    c.free_image(&img);

    std::printf("\n%s: %d неудачных проверок\n", g_failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ПРОШЛО",
                g_failures);
    FreeLibrary(c.mod);
    return g_failures;
}
