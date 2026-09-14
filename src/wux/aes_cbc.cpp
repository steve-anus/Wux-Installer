/*
 * wuxinstaller - AES-128 CBC decryption implementation.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "wux/aes_cbc.h"

#include <array>
#include <cstring>

namespace wux {
namespace {

// Multiply two elements of GF(2^8) under the AES polynomial x^8+x^4+x^3+x+1.
U8 gfMul(U8 a, U8 b) {
    U8 p = 0;
    while (b) {
        if (b & 1) p ^= a;
        U8 hi = a & 0x80;      // original bit 7: the overflow bit after the shift
        a <<= 1;
        if (hi) a ^= 0x1b;     // fold in the reduction polynomial when it overflowed
        b >>= 1;
    }
    return p;
}

// Multiplicative inverse in GF(2^8); 0 maps to 0 (the AES convention)
U8 gfInv(U8 x) {
    if (x == 0) return 0;
    U8 result = 1;
    U8 base = x;
    int exp = 254;
    while (exp > 0) {
        if (exp & 1) result = gfMul(result, base);
        base = gfMul(base, base);
        exp >>= 1;
    }
    return result;
}

// Forward S-box: the affine transform of the multiplicative inverse, derived
// from the FIPS-197 definition rather than transcribed
const U8* sbox() {
    static const std::array<U8, 256> box = [] {
        std::array<U8, 256> b{};
        for (int i = 0; i < 256; ++i) {
            U8 v = gfInv((U8)i);
            // FIPS-197 affine (bit form): out_k = c_k ^ a_k ^ a_{k+4} ^ a_{k+5}
            // ^ a_{k+6} ^ a_{k+7}; a_0 is the least-significant bit, c = 0x63.
            U8 out = 0;
            for (int k = 0; k < 8; ++k) {
                U8 bit = (U8)((v >> k) & 1);
                bit ^= (U8)((v >> ((k + 4) % 8)) & 1);
                bit ^= (U8)((v >> ((k + 5) % 8)) & 1);
                bit ^= (U8)((v >> ((k + 6) % 8)) & 1);
                bit ^= (U8)((v >> ((k + 7) % 8)) & 1);
                bit ^= (U8)((0x63 >> k) & 1);
                if (bit) out |= (U8)(1 << k);
            }
            b[(size_t)i] = out;
        }
        return b;
    }();
    return box.data();
}

const U8* invSbox() {
    static const std::array<U8, 256> box = [] {
        std::array<U8, 256> b{};
        const U8* const s = sbox();
        for (int i = 0; i < 256; ++i) b[(size_t)s[i]] = (U8)i;
        return b;
    }();
    return box.data();
}

// Forward key schedule, packed as a flat byte array: one 16-byte round key per
// round, byte order matching the column-major state layout.
void keyExpand(const U8* key, size_t keyLen, U8 rk[16 * 15], int& nr) {
    const int nk = (int)(keyLen / 4);         // 4 / 6 / 8
    nr = (int)(keyLen / 4) + 6;               // 10 / 12 / 14
    static const U8 rcon[11] = {
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };
    const U8* const S = sbox();
    std::array<U32, 60> w{};
    for (int i = 0; i < nk; ++i)
        w[(size_t)i] = readU32BE(key + (size_t)i * 4);
    const int total = 4 * (nr + 1);
    for (int i = nk; i < total; ++i) {
        U32 t = w[(size_t)i - 1];
        if (i % nk == 0) {
            t = (t << 8) | (t >> 24);         // RotWord
            t = ((U32)S[(t >> 24) & 0xff] << 24)  // SubWord
              | ((U32)S[(t >> 16) & 0xff] << 16)
              | ((U32)S[(t >> 8) & 0xff] << 8)
              |  (U32)S[(t >> 0) & 0xff];
            t ^= ((U32)rcon[i / nk] << 24);
        } else if (nk > 6 && i % nk == 4) {
            t = ((U32)S[(t >> 24) & 0xff] << 24)
              | ((U32)S[(t >> 16) & 0xff] << 16)
              | ((U32)S[(t >> 8) & 0xff] << 8)
              |  (U32)S[(t >> 0) & 0xff];
        }
        w[(size_t)i] = w[(size_t)i - nk] ^ t;
    }
    for (int r = 0; r <= nr; ++r) {
        for (int c = 0; c < 4; ++c) {
            U32 wv = w[(size_t)(r * 4 + c)];
            rk[(size_t)(r * 16 + c * 4 + 0)] = (U8)(wv >> 24);
            rk[(size_t)(r * 16 + c * 4 + 1)] = (U8)(wv >> 16);
            rk[(size_t)(r * 16 + c * 4 + 2)] = (U8)(wv >> 8);
            rk[(size_t)(r * 16 + c * 4 + 3)] = (U8)(wv);
        }
    }
}

// Inverse cipher on one 16-byte block. State is column-major: s[row][col].
void blockDecrypt(const U8* in, U8* out, const U8 rk[16 * 15], int nr) {
    U8 s[4][4];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            s[r][c] = in[c * 4 + r];

    const U8* const isb = invSbox();

    auto addKey = [&](int round) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                s[r][c] ^= rk[(size_t)(round * 16 + c * 4 + r)];
    };
    auto invShiftRows = [&]() {
        U8 t[4][4];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                t[r][c] = s[r][(c - r + 4) % 4];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                s[r][c] = t[r][c];
    };
    auto invSubBytes = [&]() {
        for (int i = 0; i < 16; ++i)
            s[i / 4][i % 4] = isb[s[i / 4][i % 4]];
    };
    auto invMixColumns = [&]() {
        for (int c = 0; c < 4; ++c) {
            U8 a0 = s[0][c], a1 = s[1][c], a2 = s[2][c], a3 = s[3][c];
            s[0][c] = (U8)(gfMul(a0, 14) ^ gfMul(a1, 11) ^ gfMul(a2, 13) ^ gfMul(a3, 9));
            s[1][c] = (U8)(gfMul(a0, 9)  ^ gfMul(a1, 14) ^ gfMul(a2, 11) ^ gfMul(a3, 13));
            s[2][c] = (U8)(gfMul(a0, 13) ^ gfMul(a1, 9)  ^ gfMul(a2, 14) ^ gfMul(a3, 11));
            s[3][c] = (U8)(gfMul(a0, 11) ^ gfMul(a1, 13) ^ gfMul(a2, 9)  ^ gfMul(a3, 14));
        }
    };

    addKey(nr);
    for (int r = nr - 1; r >= 1; --r) {
        invShiftRows();
        invSubBytes();
        addKey(r);
        invMixColumns();
    }
    invShiftRows();
    invSubBytes();
    addKey(0);

    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = s[r][c];
}

} // namespace

Error aesCbcDecrypt(const U8* key, const U8* iv,
                    const U8* in, size_t inLen, U8* out) {
    if (!key || !iv || !in || !out) return Error::DecryptError;
    if (inLen % AesBlock != 0) return Error::DecryptError;

    U8 rk[16 * 15];
    int nr = 0;
    keyExpand(key, AesKeySize128, rk, nr);

    U8 prev[16];
    std::memcpy(prev, iv, 16);         // first plaintext block XORs the IV
    for (size_t b = 0; b < inLen / 16; ++b) {
        const U8* ct = in + b * 16;
        U8 pt[16];
        blockDecrypt(ct, pt, rk, nr);
        for (int i = 0; i < 16; ++i)
            out[b * 16 + i] = (U8)(pt[i] ^ prev[i]);
        std::memcpy(prev, ct, 16);     // next previous-ciphertext is this block
    }
    return Error::Ok;
}

} // namespace wux
