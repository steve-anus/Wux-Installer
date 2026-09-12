/*
 * wuxinstaller - ticket / NUS content key.
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
#include "wux/ticket.h"
#include "wux/aes_cbc.h"

#include <cstring>

namespace wux {

Error deriveContentKey(const U8* commonKey, const U8* tik, size_t tikLen,
                       U8 contentKey[16]) {
    if (commonKey == nullptr || tik == nullptr || contentKey == nullptr)
        return Error::DecryptError;
    // The ticket must extend past the title ID field.
    if (tikLen < (size_t)kTicketTitleIdOffset + 8)
        return Error::Truncated;

    const U8* encKey = tik + kTicketKeyOffset;
    U64 titleID = readU64BE(tik + kTicketTitleIdOffset);

    // IV: the title ID as a big-endian u64 in the first 8 bytes, zero in the
    // last 8 (matches ByteBuffer.allocate(0x10).putLong(titleID)).
    U8 iv[16];
    std::memset(iv, 0, 16);
    for (int i = 0; i < 8; ++i)
        iv[i] = (U8)(titleID >> (8 * (7 - i)));

    return aesCbcDecrypt(commonKey, iv, encKey, 16, contentKey);
}

} // namespace wux
