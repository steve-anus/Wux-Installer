#include "check.h"
#include "wux/fst.h"

namespace {

void putBE32(std::vector<unsigned char>& b, size_t at, unsigned v) {
    for (int i = 0; i < 4; ++i) b[at + (size_t)i] = (unsigned char)(v >> (8 * (3 - i)));
}

// "FST\0", block size, section count, then sections, entries, strings.
std::vector<unsigned char> fstHeader(unsigned sectionCount) {
    std::vector<unsigned char> b(0x20, 0);
    b[0] = 'F'; b[1] = 'S'; b[2] = 'T'; b[3] = 0;
    putBE32(b, 0x04, 0x20);
    putBE32(b, 0x08, sectionCount);
    return b;
}

}  // namespace

void testFst() {
    {   // one section, root + one directory holding one file
        std::vector<unsigned char> b = fstHeader(1);
        b.resize(0x20 + 0x20 + 3 * 0x10, 0);
        const size_t ent = 0x40;
        b[ent] = 1; putBE32(b, ent + 0x08, 3);                       // root, 3 entries
        b[ent + 0x10] = 1; b[ent + 0x13] = 1; putBE32(b, ent + 0x18, 3);  // dir "d" at name 1
        b[ent + 0x20 + 3] = 3; putBE32(b, ent + 0x28, 9);            // file "f" at name 3, 9 bytes
        const char names[] = "\0d\0f\0";
        b.insert(b.end(), names, names + sizeof(names));
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Ok);
        const wux::FstEntry* e = f.findEntry("d/f");
        CHECK(e != nullptr && !e->isDir && e->fileSize == 9);
        CHECK(f.rootDirChildren().size() == 1);
    }
    {   // section count with the top bit set must not pass the sanity cap
        std::vector<unsigned char> b = fstHeader(0xFFFFFFFFu);
        b.resize(0x400, 0);
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Truncated);
    }
    {
        std::vector<unsigned char> b = fstHeader(0x80000000u);
        b.resize(0x400, 0);
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Truncated);
    }
    {   // one above the cap
        std::vector<unsigned char> b = fstHeader(1025);
        b.resize(0x20 + 1025 * 0x20 + 0x10, 0);
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Truncated);
    }
    {   // entry count claiming more entries than the buffer holds
        std::vector<unsigned char> b = fstHeader(0);
        b.resize(0x30, 0);
        b[0x20] = 1;
        putBE32(b, 0x28, 0xFFFFFFFFu);
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Truncated);
    }
    {   // many entries sharing one long name must not expand without bound
        const unsigned entries = 8000;
        std::vector<unsigned char> b = fstHeader(1);
        const size_t ent = 0x40;
        b.resize(ent + entries * 0x10, 0);
        b[ent] = 1; putBE32(b, ent + 0x08, entries);
        for (unsigned i = 1; i < entries; ++i) {
            size_t e = ent + i * 0x10;
            b[e + 3] = 1;                       // every file named by the same string
            putBE32(b, e + 0x08, 1);
        }
        b.push_back(0);
        b.insert(b.end(), 60000, 'n');          // one 60000-byte name
        b.push_back(0);
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::Truncated);
    }
    {   // bad signature
        std::vector<unsigned char> b = fstHeader(0);
        b[0] = 'X';
        wux::Fst f;
        CHECK(f.parse(b.data(), b.size()) == wux::Error::BadSignature);
    }
}
