// wav2qoa <in.wav> <out.qoa> — минимальный конвертер 16-bit PCM WAV
// (mono/stereo) в QOA на энкодере из qoa.h. Для тестовых сэмплов.

#include <cstdio>
#include <cstring>
#include <vector>

#define QOA_IMPLEMENTATION
#define QOA_NO_STDIO
#include "qoa/qoa.h"

namespace {

bool read_all(const char* path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

unsigned int le32(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 |
           unsigned(p[3]) << 24;
}

unsigned short le16(const unsigned char* p) {
    return (unsigned short)(p[0]) | (unsigned short)(p[1]) << 8;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: wav2qoa <in.wav> <out.qoa>\n");
        return 1;
    }
    std::vector<unsigned char> wav;
    if (!read_all(argv[1], wav) || wav.size() < 44 ||
        std::memcmp(wav.data(), "RIFF", 4) != 0) {
        std::printf("bad wav\n");
        return 1;
    }
    if (std::memcmp(wav.data() + 8, "WAVE", 4) != 0 ||
        std::memcmp(wav.data() + 12, "fmt ", 4) != 0) {
        std::printf("not WAVE\n");
        return 1;
    }
    const unsigned short fmt = le16(wav.data() + 20);
    const unsigned short chans = le16(wav.data() + 22);
    const unsigned int rate = le32(wav.data() + 24);
    const unsigned short bits = le16(wav.data() + 34);
    if (fmt != 1 || (chans != 1 && chans != 2) || bits != 16) {
        std::printf("unsupported: fmt=%u chans=%u bits=%u\n", fmt, chans,
                    bits);
        return 1;
    }
    // Ищем data-чанк (может быть не сразу после fmt).
    size_t p = 12;
    const unsigned char* data = nullptr;
    unsigned int data_size = 0;
    while (p + 8 <= wav.size()) {
        const unsigned int sz = le32(wav.data() + p + 4);
        if (std::memcmp(wav.data() + p, "data", 4) == 0) {
            data = wav.data() + p + 8;
            data_size = sz;
            break;
        }
        p += 8 + sz + (sz & 1);
    }
    if (!data || data_size == 0) {
        std::printf("no data chunk\n");
        return 1;
    }

    qoa_desc q{};
    q.channels = chans;
    q.samplerate = rate;
    q.samples = data_size / 2 / chans;
    unsigned int out_len = 0;
    void* encoded = qoa_encode(reinterpret_cast<const short*>(data), &q,
                               &out_len);
    if (!encoded) {
        std::printf("encode failed\n");
        return 1;
    }
    FILE* f = std::fopen(argv[2], "wb");
    if (!f) {
        QOA_FREE(encoded);
        return 1;
    }
    std::fwrite(encoded, 1, out_len, f);
    std::fclose(f);
    QOA_FREE(encoded);
    std::printf("ok: %u samples, %u channels, %u Hz -> %u bytes\n",
                q.samples, q.channels, q.samplerate, out_len);
    return 0;
}
