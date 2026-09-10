/*
 * wuxinstaller - FST (file system table) parser
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
#ifndef _WUX_FST_H
#define _WUX_FST_H

#include "wux/wux_common.h"
#include <vector>
#include <string>

namespace wux {

// One entry of the FST's content table (the 0x20-byte section at 0x20).
struct FstContentInfo {
    U32 offsetSector = 0;
    U32 sizeSector   = 0;
    U64 ownerTitleID = 0;

    // Content byte offset within the partition (0 for offsetSector == 0).
    U64 offset() const {
        return offsetSector == 0 ? 0 : (U64)(offsetSector - 1) * fmt::kSectorSize;
    }
    // Content size in bytes.
    U32 size() const { return (U32)((U64)sizeSector * fmt::kSectorSize); }
};

// One parsed FST file-system entry (file or directory).
struct FstEntry {
    std::string name;       // bare name, e.g. "title.tmd"
    std::string path;       // full path, e.g. "0005000B.../title.tmd"
    U32 fileOffset     = 0;  // bytes, relative to the partition origin (files only)
    U32 fileSize       = 0;  // bytes (files only)
    U16 contentIndex   = 0;
    bool     isDir         = false;
};

// Parses a decrypted FST blob.
class Fst {
public:
    U32 sectorSize   = fmt::kSectorSize;
    int      contentCount = 0;

    std::vector<FstContentInfo> contentInfos;  // indexed by content index
    std::vector<FstEntry>      entries;        // flat, in depth-first order

    // data is the DECRYPTED FST blob. On success, contentInfos and entries are
    // populated; sectorSize and contentCount reflect the header.
    Error parse(const U8* data, size_t len);

    const FstEntry* findEntry(const std::string& path) const;
    const FstContentInfo* contentInfo(int contentIndex) const;

    // Top-level directories (path has no '/'), for the SI partition's per-title
    // folders.
    std::vector<const FstEntry*> rootDirChildren() const;
};

} // namespace wux

#endif // _WUX_FST_H
