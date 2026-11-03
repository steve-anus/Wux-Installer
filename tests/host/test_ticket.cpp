#include "check.h"
#include "wux/ticket.h"

// With title ID 0 the IV is all zero, so the content key is the plain AES
// decryption of the encrypted key: the FIPS-197 C.1 vector fits exactly.
void testTicket() {
    std::vector<unsigned char> tik(0x1DC + 8, 0);
    std::vector<unsigned char> ct = fromHex("69c4e0d86a7b0430d8cdb78070b4c55a");
    for (size_t i = 0; i < 16; ++i) tik[wux::kTicketKeyOffset + i] = ct[i];
    std::vector<unsigned char> common = fromHex("000102030405060708090a0b0c0d0e0f");
    unsigned char key[16];
    CHECK(wux::deriveContentKey(common.data(), tik.data(), tik.size(), key) == wux::Error::Ok);
    std::vector<unsigned char> want = fromHex("00112233445566778899aabbccddeeff");
    CHECK(std::vector<unsigned char>(key, key + 16) == want);
    CHECK(wux::deriveContentKey(common.data(), tik.data(), 0x1DC, key) == wux::Error::Truncated);
}
