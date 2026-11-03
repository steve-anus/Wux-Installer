#include "check.h"
#include "wux/tmd.h"

void testTmd() {
    std::vector<unsigned char> t(0xB04 + 0x30, 0);
    t[0x18C] = 0x00; t[0x18D] = 0x05; t[0x18E] = 0x00; t[0x18F] = 0x0E;  // 0005000E...
    t[0x1DF] = 1;                                                       // one content
    t[0xB04 + 3] = 0x2A;                                                // content id 0x2A
    t[0xB04 + 7] = 0x03;                                                // encrypted + hashed
    t[0xB04 + 0x0F] = 0x40;                                             // 0x40 bytes
    wux::Tmd tmd;
    CHECK(wux::parseTmd(t.data(), t.size(), tmd) == wux::Error::Ok);
    CHECK(tmd.contents.size() == 1 && tmd.contents[0].id == 0x2A);
    CHECK(tmd.contents[0].hashed && tmd.contents[0].encrypted);
    CHECK(wux::titleIdHex(tmd.titleID).substr(0, 8) == "0005000E");

    t[0x1DE] = 0xFF; t[0x1DF] = 0xFF;          // 65535 contents in a buffer for one
    CHECK(wux::parseTmd(t.data(), t.size(), tmd) == wux::Error::Truncated);
    t[0x1DE] = 0; t[0x1DF] = 1;
    t[0x18D] = 0x01;                            // not a Wii U title
    CHECK(wux::parseTmd(t.data(), t.size(), tmd) == wux::Error::BadMagic);
    t[0x18C] = 0xFF; t[0x18D] = 0xFF;           // both 0005 bits set, but not 0005
    CHECK(wux::parseTmd(t.data(), t.size(), tmd) == wux::Error::BadMagic);
    t[0x18C] = 0x00; t[0x18D] = 0x05;
    CHECK(wux::parseTmd(t.data(), 0x100, tmd) == wux::Error::Truncated);
}
