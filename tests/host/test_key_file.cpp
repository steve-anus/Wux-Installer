#include "check.h"
#include "wux/key_file.h"

#include <cstdio>
#include <unistd.h>

void testKeyFile() {
    std::vector<unsigned char> key;
    {   // a raw key whose bytes include tab, line feed, carriage return and space
        std::vector<unsigned char> raw = fromHex("01020304050607080909200a0d0e0f10");
        CHECK(wux::parseKeyBytes(raw, key) == wux::Error::Ok);
        CHECK(key == raw);
    }
    {   // 32 hex digits with a trailing line break
        std::string text = "000102030405060708090a0b0c0d0e0f\n";
        std::vector<unsigned char> raw(text.begin(), text.end());
        CHECK(wux::parseKeyBytes(raw, key) == wux::Error::Ok);
        CHECK(key == fromHex("000102030405060708090a0b0c0d0e0f"));
    }
    {   // 16 hex digits: most likely a truncated hex key
        std::string text = "0001020304050607";
        std::vector<unsigned char> raw(text.begin(), text.end());
        std::string why;
        CHECK(wux::parseKeyBytes(raw, key, &why) == wux::Error::MissingKey);
        CHECK(why.find("truncated") != std::string::npos);
        CHECK(key.empty());
    }
    {   // 32 characters that are not hex
        std::string text = "zz0102030405060708090a0b0c0d0e0f";
        std::vector<unsigned char> raw(text.begin(), text.end());
        CHECK(wux::parseKeyBytes(raw, key) == wux::Error::MissingKey);
    }
    {   // the caller appends the accepted formats, so the reason gives only the count
        std::vector<unsigned char> raw(15, 0x41);
        std::string why;
        CHECK(wux::parseKeyBytes(raw, key, &why) == wux::Error::MissingKey);
        CHECK(why == "got 15 byte(s)");
    }
    {   // a key file over 4096 bytes is refused before parsing
        std::string p = tempPath("big.key");
        std::vector<unsigned char> raw(5000, 'a');
        FILE* fp = std::fopen(p.c_str(), "wb");
        std::fwrite(raw.data(), 1, raw.size(), fp);
        std::fclose(fp);
        std::string why;
        CHECK(wux::parseKeyFile(p.c_str(), key, &why) == wux::Error::Truncated);
        CHECK(why == "file too large");
        unlink(p.c_str());
    }
    {   // through a real file, then a missing one
        std::string p = tempPath("game.key");
        std::vector<unsigned char> raw = fromHex("0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a");
        FILE* fp = std::fopen(p.c_str(), "wb");
        std::fwrite(raw.data(), 1, raw.size(), fp);
        std::fclose(fp);
        CHECK(wux::parseKeyFile(p.c_str(), key) == wux::Error::Ok);
        CHECK(key == raw);
        unlink(p.c_str());
        std::string why;
        CHECK(wux::parseKeyFile(p.c_str(), key, &why) == wux::Error::NotFound);
        CHECK(why == "file not found");
    }
}

void testChecked() {
    U64 r = 0;
    CHECK(wux::addU64(1, 2, r) && r == 3);
    CHECK(!wux::addU64(~(U64)0, 1, r));
    CHECK(wux::mulU64(0xFFFFFFFFULL, 0xFFFFFFFFULL, r) && r == 0xFFFFFFFE00000001ULL);
    CHECK(!wux::mulU64(1ULL << 32, 1ULL << 32, r));
    CHECK(wux::mulU64(0, ~(U64)0, r) && r == 0);
}
