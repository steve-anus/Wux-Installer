/*
 * wuxinstaller - TMD parser and h3 extraction implementation
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
#include "wux/tmd.h"

#include <algorithm>

namespace wux {

std::string hexUpper(U64 v, int digits) {
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    s.reserve((size_t)digits);
    for (int i = digits - 1; i >= 0; --i)
        s.push_back(hex[(int)((v >> (i * 4)) & 0xF)]);
    return s;
}

std::string titleIdHex(U64 titleID) {
    return hexUpper(titleID, 16);
}

Error parseTmd(const U8* data, size_t len, Tmd& out) {
    out = Tmd();
    if (len < 0x204) return Error::Truncated;

    out.titleID = readU64BE(data + 0x18C);
    if ((out.titleID & 0x0005000000000000ULL) != 0x0005000000000000ULL)
        return Error::BadMagic;   // not a Wii U (0005xxxx) title
    out.contentCount = readU16BE(data + 0x1DE);

    const size_t contentOff = 0xB04;
    const size_t contentSize = 0x30;
    if (contentOff + (size_t)out.contentCount * contentSize > len)
        return Error::Truncated;

    for (int i = 0; i < out.contentCount; ++i) {
        const U8* e = data + contentOff + (size_t)i * contentSize;
        TmdContent c;
        c.id = readU32BE(e + 0x00);
        c.index = readU16BE(e + 0x04);
        c.type = readU16BE(e + 0x06);
        c.encryptedFileSize = readU64BE(e + 0x08);
        c.hashed = (c.type & 0x0002) != 0;
        c.encrypted = (c.type & 0x0001) != 0;
        out.contents.push_back(c);
    }
    return Error::Ok;
}

Error extractH3(const U8* header, size_t headerLen,
                const std::vector<TmdContent>& contents, int index,
                std::vector<U8>& out) {
    if (header == nullptr || headerLen < 0x40)
        return Error::Truncated;

    U32 cnt = readU32BE(header + 0x10);
    U64 start = 0x40 + (U64)cnt * 0x04;

    // Hash entries are laid out in ascending content-index order.
    std::vector<int> order;
    order.reserve(contents.size());
    for (size_t i = 0; i < contents.size(); ++i) order.push_back((int)i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return contents[a].index < contents[b].index; });

    U64 acc = 0;
    for (size_t k = 0; k < order.size(); ++k) {
        const TmdContent& c = contents[order[k]];
        if (!c.hashed || !c.encrypted) continue;

        // One 0x14-byte hash per 0x1000 chunk of each 0x10000 of content.
        U32 cntHashes = (U32)((c.encryptedFileSize / 0x10000) / 0x1000) + 1;
        if ((int)c.index == index) {
            U64 h3off = start + acc * 0x14;
            U64 h3len = (U64)cntHashes * 0x14;
            if (h3off + h3len > headerLen) return Error::Truncated;
            out.assign(header + h3off, header + h3off + h3len);
            return Error::Ok;
        }
        acc += cntHashes;
    }
    return Error::NotFound;
}

} // namespace wux
