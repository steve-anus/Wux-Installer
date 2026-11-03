/*
 * wuxinstaller - AES-128 CBC decryption, through tiny-AES-c.
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

#include <cstring>

// Must match tiny_aes.c: these settings decide the layout of struct AES_ctx.
#define CBC 1
#define ECB 0
#define CTR 0
extern "C" {
#include "wux/third_party/tiny-aes/aes.h"
}
#undef CBC
#undef ECB
#undef CTR

namespace wux {

// AES-128 CBC decryption through tiny-AES-c. The key and IV are taken into
// the context first; then the input is copied to the output and decrypted
// there, so out may be the same buffer as in, key or iv.
Error aesCbcDecrypt(const U8* key, const U8* iv,
                    const U8* in, size_t inLen, U8* out) {
    if (!key || !iv || !in || !out) return Error::DecryptError;
    if (inLen % AesBlock != 0) return Error::DecryptError;
    if (inLen == 0) return Error::Ok;
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    if (in != out) std::memmove(out, in, inLen);
    AES_CBC_decrypt_buffer(&ctx, out, inLen);
    return Error::Ok;
}

} // namespace wux
