/*
 * wuxinstaller - FST (file system table) parser implementation.
 *
 * See fst.h for the byte layout.
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
// table is laid out depth-first; a directory's 0x08 field holds the absolute
// slot index where its subtree ends.
void parseRange(const U8* data, size_t len, size_t entOff, size_t strOff,
                int i, int end, const std::string& parentPath,
                std::vector<FstEntry>& out) {
    while (i < end) {
        const U8* e = data + entOff + (size_t)i * 0x10;
        if (e + 0x10 > data + len) return;   // bounds guard

        U32 raw04        = readU32BE(e + 0x04);
        U32 raw08        = readU32BE(e + 0x08);
        U16 sectionNumber = readU16BE(e + 0x0E);
        U32 nameOffset   = ((U32)e[1] << 16) | ((U32)e[2] << 8) | (U32)e[3];

        std::string name = readName(data, len, strOff, nameOffset);
        std::string path = parentPath + name;

        FstEntry en;
        en.name = name;
        en.path = path;
        en.isDir  = (e[0] & 0x01) != 0;
        en.isLink = (e[0] & 0x80) != 0;
        en.sectionNumber = sectionNumber;

        if (en.isDir) {
            en.lastEntry = raw08;   // raw04 is the parent entry number (unused)
            out.push_back(en);
            // Subtree occupies slots [i+1, raw08); raw08 is the absolute end.
            parseRange(data, len, entOff, strOff, i + 1, (int)raw08,
                       path + "/", out);
            // A corrupt lastEntry could point backwards; never loop forever.
            i = (raw08 > (U32)(i + 1)) ? (int)raw08 : i + 1;
        } else {
            en.addrBlocks = raw04;  // section-block units (files)
            en.fileSize = raw08;    // bytes (files)
            out.push_back(en);
            ++i;
        }
    }
}

} // namespace

Error Fst::parse(const U8* data, size_t len) {
    sections.clear();
    entries.clear();
    sectionCount = 0;
    sectionBlockSize = fmt::kSectorSize;

    if (len < 0x20) return Error::Truncated;
    if (std::memcmp(data, fmt::kFstSignature, 4) != 0) return Error::BadSignature;

    sectionBlockSize = readU32BE(data + 0x04);
    if (sectionBlockSize == 0) sectionBlockSize = fmt::kSectorSize;
    sectionCount = (int)readU32BE(data + 0x08);
    if (sectionCount > 1024) return Error::Truncated;   // sanity cap

    // Section table at 0x20: sectionCount entries of 32 bytes.
    const size_t secOff = 0x20;
    const size_t secSize = (size_t)sectionCount * 0x20;
    if (secOff + secSize > len) return Error::Truncated;
    sections.resize((size_t)sectionCount);
    for (int i = 0; i < sectionCount; ++i) {
        const U8* e = data + secOff + (size_t)i * 0x20;
        FstSection& s = sections[(size_t)i];
        s.address      = readU32BE(e + 0x00);
        s.size         = readU32BE(e + 0x04);
        s.ownerTitleID = readU64BE(e + 0x08);
        s.groupID      = readU32BE(e + 0x10);
        s.hashMode     = e[0x14];
    }

    // Node table follows the section table; entry 0 is the root.
    const size_t entOff = secOff + secSize;
    if (entOff + 0x10 > len) return Error::Truncated;
    const U32 lastEntryNumber = readU32BE(data + entOff + 0x08);
    if (lastEntryNumber < 1) return Error::Truncated;

    // String table follows the node entries.
    const size_t strOff = entOff + (size_t)lastEntryNumber * 0x10;
    if (strOff > len) return Error::Truncated;

    parseRange(data, len, entOff, strOff, 1, (int)lastEntryNumber,
               std::string(), entries);
    return Error::Ok;
}

const FstEntry* Fst::findEntry(const std::string& path) const {
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].path == path)
            return &entries[i];
    return nullptr;
}

const FstSection* Fst::section(int number) const {
    if (number < 0 || number >= (int)sections.size())
        return nullptr;
    return &sections[(size_t)number];
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
