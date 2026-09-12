/*
 * wuxinstaller - FST (file system table) parser.
 *
 *
 ******************************Layout*********************************************
 *                                                                               *
 *                                                                               *
 *   header:    "FST\0" (4), sectionBlockSize (u32 @4), numberOfSections (u32 @8)*
 *   sections:  32 bytes each @0x20: address (u32, volume blocks), size (u32,    *
 *              volume blocks), ownerID (u64), groupID (u32), hashMode (1)       *
 *   nodes:     16 bytes each, entry 0 = root:                                   *
 *              byte0 = type (0=file, 1=dir, 0x80=link), bytes1-3 = name         *
 *              address (24-bit), +0x04 file: offset in section blocks / dir:    *
 *              parent entry number, +0x08 file: size (bytes) / dir:             *
 *              lastEntryNumber (absolute end slot), +0x0C permission,           *
 *              +0x0E section number (== TMD content index)                      *
 *   strings:   at (nodes + lastEntryNumber * 16), null-terminated,              *
 *              address 0 =  empty (root) name                                   *
 *                                                                               *
 *                                                                               *
 *********************************************************************************
 *
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

// One entry of the FST section table (the 32-byte table at 0x20).
struct FstSection {
    U32 address = 0;     // in volume blocks (relative to the volume start)
    U32 size = 0;        // in volume blocks
    U64 ownerTitleID = 0;
    U32 groupID = 0;
    U8  hashMode = 0;
};

// One parsed FST node entry (file, directory or link).
struct FstEntry {
    std::string name;              // bare name, e.g. "title.tmd"
    std::string path;              // full path, e.g. "0005000B.../title.tmd"
    U32 addrBlocks = 0;            // files: offset within the section, in section blocks
    U32 fileSize = 0;              // files: size in bytes
    U32 lastEntry = 0;             // dirs: absolute slot index where the subtree ends
    U16 sectionNumber = 0;         // index into the section table == TMD content index
    bool isDir  = false;
    bool isLink = false;           // type 0x80: references content not in this package

    // File byte offset within its section (for the IV and the on-disc offset).
    U64 offset(U32 sectionBlockSize) const {
        return (U64)addrBlocks * sectionBlockSize;
    }
};

// Parses a decrypted FST blob.
class Fst {
public:
    U32 sectionBlockSize = fmt::kSectorSize;   // from the FST header (+0x04)
    int  sectionCount = 0;

    std::vector<FstSection> sections;          // indexed by section number
    std::vector<FstEntry>   entries;           // flat, in depth-first order

    // data is the DECRYPTED FST blob. On success, sections and entries are
    // populated; sectionBlockSize and sectionCount reflect the header.
    Error parse(const U8* data, size_t len);

    const FstEntry* findEntry(const std::string& path) const;
    const FstSection* section(int number) const;

    // Top-level directories (path has no '/'), for the SI partition's per-title
    // folders.
    std::vector<const FstEntry*> rootDirChildren() const;
};

} // namespace wux

#endif // _WUX_FST_H
