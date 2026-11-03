#include "check.h"
#include "wux/aes_cbc.h"

void testAes() {
    std::vector<unsigned char> zero(16, 0);
    {   // FIPS-197 appendix C.1 (AES-128), one block with a zero IV
        std::vector<unsigned char> key = fromHex("000102030405060708090a0b0c0d0e0f");
        std::vector<unsigned char> ct = fromHex("69c4e0d86a7b0430d8cdb78070b4c55a");
        std::vector<unsigned char> out(16);
        CHECK(wux::aesCbcDecrypt(key.data(), zero.data(), ct.data(), 16, out.data()) == wux::Error::Ok);
        CHECK(out == fromHex("00112233445566778899aabbccddeeff"));
    }
    // NIST SP 800-38A F.2.2, CBC-AES128.Decrypt
    std::vector<unsigned char> key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
    std::vector<unsigned char> iv = fromHex("000102030405060708090a0b0c0d0e0f");
    std::vector<unsigned char> ct = fromHex(
        "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
        "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7");
    std::vector<unsigned char> pt = fromHex(
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    {
        std::vector<unsigned char> out(64);
        CHECK(wux::aesCbcDecrypt(key.data(), iv.data(), ct.data(), 64, out.data()) == wux::Error::Ok);
        CHECK(out == pt);
    }
    {   // decrypting in place gives the same result
        std::vector<unsigned char> buf = ct;
        CHECK(wux::aesCbcDecrypt(key.data(), iv.data(), buf.data(), 64, buf.data()) == wux::Error::Ok);
        CHECK(buf == pt);
    }
    {   // the output may overlap the key or the IV (FIPS-197 C.1, zero IV)
        std::vector<unsigned char> k1 = fromHex("000102030405060708090a0b0c0d0e0f");
        std::vector<unsigned char> c1 = fromHex("69c4e0d86a7b0430d8cdb78070b4c55a");
        std::vector<unsigned char> p1 = fromHex("00112233445566778899aabbccddeeff");
        std::vector<unsigned char> buf = k1;
        CHECK(wux::aesCbcDecrypt(buf.data(), zero.data(), c1.data(), 16, buf.data()) == wux::Error::Ok);
        CHECK(buf == p1);
        std::vector<unsigned char> ivbuf(16, 0);
        CHECK(wux::aesCbcDecrypt(k1.data(), ivbuf.data(), c1.data(), 16, ivbuf.data()) == wux::Error::Ok);
        CHECK(ivbuf == p1);
    }
    {   // the IV is not modified
        std::vector<unsigned char> ivCopy = iv, out(64);
        wux::aesCbcDecrypt(key.data(), ivCopy.data(), ct.data(), 64, out.data());
        CHECK(ivCopy == iv);
    }
    {
        std::vector<unsigned char> out(64);
        CHECK(wux::aesCbcDecrypt(key.data(), iv.data(), ct.data(), 15, out.data()) == wux::Error::DecryptError);
        CHECK(wux::aesCbcDecrypt(key.data(), iv.data(), ct.data(), 0, out.data()) == wux::Error::Ok);
    }
}
