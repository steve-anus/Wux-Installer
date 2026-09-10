/*
 * wuxinstaller - FST parser implementation
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
#include "wux/fst.h"

namespace wux {
namespace {

// Read a null-terminated name at nameOff + nameOffset, bounds-checked.
std::string readName(const U8* data, size_t len, size_t nameOff, U32 nameOffset) {
    size_t start = nameOff + nameOffset;
    if (start >= len) return std::string();
    size_t i = start;
    while (i < len && data[i] != 0) ++i;
    return std::string((const char*)(data + start), i - start);
}

// Parse entries in slot range [i, end) as children of parentPath. The entry
// table is laid out in depth-first order; a directory's 0x08 field holds the
// absolute slot index where its subtree ends.
void parseRange(const U8* data, size_t len, size_t fstOff, size_t nameOff,
                U32 sectorSize, int i, int end, const std::string& parentPath,
                std::vector<FstEntry>& out) {
    while (i < end) {
        const U8* e = data + fstOff + (size_t)i * 0x10;
        if (e + 0x10 > data + len) return;   // bounds guard

        bool     isDir        = (e[0] & 0x01) != 0;
        U32 raw04        = readU32BE(e + 0x04);
        U32 raw08        = readU32BE(e + 0x08);
        U16 contentIndex = readU16BE(e + 0x0E);
        U32 nameOffset   = ((U32)e[1] << 16) | ((U32)e[2] << 8) | (U32)e[3];

        std::string name = readName(data, len, nameOff, nameOffset);
        std::string path = parentPath + name;

        FstEntry en;
        en.name = name;
        en.path = path;
        en.isDir = isDir;
        en.contentIndex = contentIndex;

        if (isDir) {
            en.fileOffset = 0;
            en.fileSize = 0;
            out.push_back(en);
            // Subtree occupies slots [i+1, raw08); raw08 is the absolute end.
            parseRange(data, len, fstOff, nameOff, sectorSize, i + 1, (int)raw08,
                       path + "/", out);
            i = (int)raw08;
        } else {
            en.fileOffset = (U32)((U64)raw04 * sectorSize);
            en.fileSize = raw08;
            out.push_back(en);
            ++i;
        }
    }
}

} // namespace

Error Fst::parse(const U8* data, size_t len) {
    contentInfos.clear();
    entries.clear();

    if (len < 0x20) return Error::Truncated;
    if (std::memcmp(data, fmt::kFstSignature, 4) != 0) return Error::BadSignature;

    sectorSize = readU32BE(data + 0x04);
    if (sectorSize == 0) sectorSize = fmt::kSectorSize;
    contentCount = (int)readU32BE(data + 0x08);

    // Content table at 0x20: contentCount entries of 0x20 bytes.
    size_t contentfstOff = 0x20;
    size_t contentfstSize = (size_t)contentCount * 0x20;
    if (contentfstOff + contentfstSize > len) return Error::Truncated;
    for (int i = 0; i < contentCount; ++i) {
        const U8* e = data + contentfstOff + (size_t)i * 0x20;
        FstContentInfo ci;
        ci.offsetSector = readU32BE(e + 0x00);
        ci.sizeSector   = readU32BE(e + 0x04);
        ci.ownerTitleID = readU64BE(e + 0x08);
        contentInfos.push_back(ci);
    }

    // File table follows the content table.
    size_t fstOff = contentfstOff + contentfstSize;
    if (fstOff + 0x10 > len) return Error::Truncated;
    U32 totalEntries = readU32BE(data + fstOff + 0x08);
    size_t nameOff = fstOff + (size_t)totalEntries * 0x10;
    if (nameOff > len) return Error::Truncated;

    parseRange(data, len, fstOff, nameOff, sectorSize, 1, (int)totalEntries,
               std::string(), entries);
    return Error::Ok;
}

const FstEntry* Fst::findEntry(const std::string& path) const {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].path == path)
            return &entries[i];
    return nullptr;
}

const FstContentInfo* Fst::contentInfo(int contentIndex) const {
    if (contentIndex < 0 || contentIndex >= (int)contentInfos.size())
        return nullptr;
    return &contentInfos[(size_t)contentIndex];
}

std::vector<const FstEntry*> Fst::rootDirChildren() const {
    std::vector<const FstEntry*> res;
    for (size_t i = 0; i < entries.size(); ++i) {
        const FstEntry& e = entries[i];
        if (e.isDir && e.path.find('/') == std::string::npos)
            res.push_back(&e);
    }
    return res;
}

} // namespace wux
