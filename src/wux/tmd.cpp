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

Error extractH3(const U8* header, const U8* h3Region, size_t h3RegionLen,
                int index, std::vector<U8>& out) {
    if (header == nullptr || h3Region == nullptr || index < 0)
        return Error::Truncated;
        
    const U32 h3ListSize = readU32BE(header + 0x0C);
    const U32 numArrays  = readU32BE(header + 0x10);
    if (h3RegionLen < h3ListSize)
        return Error::Truncated;

    // H3HashArrayList: list position k holds the hash for content index k, built
    // from table entry i = k + 1 (table entries i = 1 .. numArrays-1, each a u32
    // offset into the region; the last entry ends at the region size).
    const U32 i = (U32)index + 1;
    if (i < 1 || i >= numArrays)
        return Error::NotFound;
    const U64 offPos = (U64)i * 0x04;
    if (offPos + 4 > h3RegionLen)
        return Error::Truncated;
    const U32 curOffset = readU32BE(h3Region + offPos);
    const U32 curEnd = (i < numArrays - 1)
        ? readU32BE(h3Region + (U64)(i + 1) * 0x04)
        : h3ListSize;
    if ((U64)curOffset >= h3ListSize || (U64)curEnd > h3ListSize || curOffset > curEnd)
        return Error::Truncated;
    out.assign(h3Region + curOffset, h3Region + curEnd);
    return Error::Ok;
}

} // namespace wux
