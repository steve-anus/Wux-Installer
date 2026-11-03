#include "check.h"
#include "wux/wux_container.h"

#include <cstdio>
#include <unistd.h>

namespace {

void putLE32(std::vector<unsigned char>& b, size_t at, unsigned v) {
    for (int i = 0; i < 4; ++i) b[at + (size_t)i] = (unsigned char)(v >> (8 * i));
}
void putLE64(std::vector<unsigned char>& b, size_t at, unsigned long long v) {
    for (int i = 0; i < 8; ++i) b[at + (size_t)i] = (unsigned char)(v >> (8 * i));
}

// Header + index table + sector-aligned sector array, written to a file.
std::string writeWux(const char* name, unsigned sectorSize, unsigned long long uncompressed,
                     const std::vector<unsigned>& index,
                     const std::vector<std::vector<unsigned char>>& sectors,
                     size_t alignTo) {
    size_t arrayOff = 0x20 + index.size() * 4;
    arrayOff = (arrayOff + alignTo - 1) / alignTo * alignTo;
    std::vector<unsigned char> f(arrayOff, 0);
    putLE32(f, 0x00, 0x30585557);
    putLE32(f, 0x04, 0x1099D02E);
    putLE32(f, 0x08, sectorSize);
    putLE64(f, 0x10, uncompressed);
    for (size_t i = 0; i < index.size(); ++i) putLE32(f, 0x20 + i * 4, index[i]);
    for (const auto& s : sectors) f.insert(f.end(), s.begin(), s.end());
    std::string path = tempPath(name);
    FILE* fp = std::fopen(path.c_str(), "wb");
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);
    return path;
}

}  // namespace

void testContainer() {
    const unsigned S = 0x8000;
    std::vector<unsigned char> a(S, 'A'), b(S, 'B');

    {   // two virtual sectors mapped crosswise onto two stored sectors
        std::string p = writeWux("ok.wux", S, 2ULL * S, {1, 0}, {a, b}, S);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Ok);
        CHECK(c.sectorCount() == 2);
        unsigned char out[32];
        CHECK(c.read(S - 16, 32, out) == wux::Error::Ok);
        CHECK(out[0] == 'B' && out[15] == 'B' && out[16] == 'A' && out[31] == 'A');
        CHECK(c.read(2ULL * S - 8, 16, out) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // a 1-byte sector size makes sectorCount * 4 wrap to a tiny table
        std::string p = writeWux("wrap.wux", 1, 0x4000000000000001ULL, {0}, {a}, 1);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) != wux::Error::Ok);
        unlink(p.c_str());
    }
    {   // sector size that is not a power of two
        std::string p = writeWux("odd.wux", 0x6000, 0x6000, {0}, {a}, 0x6000);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) != wux::Error::Ok);
        unlink(p.c_str());
    }
    {   // a full-disc size claimed by a file far too small for its index table
        std::string p = writeWux("short.wux", S, 0x5D3A00000ULL, {0}, {}, 1);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // a legal sector size with a huge declared size: refused before any allocation
        std::string p = writeWux("huge.wux", 0x200, 1ULL << 62, {0}, {}, 1);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // an index entry pointing past the end of the file
        std::string p = writeWux("past.wux", S, S, {7}, {a}, S);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Ok);
        unsigned char out[16];
        CHECK(c.read(0, 16, out) == wux::Error::BadEntryOffset);
        unlink(p.c_str());
    }
    {   // reads stop at the declared size, not at the end of the last sector
        std::string p = writeWux("partial.wux", S, 1, {0}, {a}, S);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Ok);
        unsigned char out[16];
        CHECK(c.read(0, 1, out) == wux::Error::Ok);
        CHECK(c.read(1, 16, out) == wux::Error::Truncated);
        CHECK(c.read(0, 2, out) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // an image larger than any disc, with a file big enough for its table
        const unsigned long long size = 1ULL << 36;           // 64 GiB, 2M sectors
        std::vector<unsigned> index((size_t)(size / S), 0);
        std::string p = writeWux("big.wux", S, size, index, {a}, S);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // small sectors: the table limit counts sectors, not bytes of image
        const unsigned small = 0x200;
        const unsigned long long size = 1ULL << 30;          // 2M sectors, 8 MiB table
        std::vector<unsigned> index((size_t)(size / small), 0);
        std::vector<unsigned char> s(small, 'S');
        std::string p = writeWux("small.wux", small, size, index, {s}, small);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::Truncated);
        unlink(p.c_str());
    }
    {   // wrong magic
        std::string p = writeWux("magic.wux", S, S, {0}, {a}, S);
        FILE* fp = std::fopen(p.c_str(), "r+b");
        std::fputc(0, fp);
        std::fclose(fp);
        wux::WuxContainer c;
        CHECK(c.open(p.c_str()) == wux::Error::BadMagic);
        unlink(p.c_str());
    }
}
