/*
 * wuxinstaller - partition TOC (decrypted area) reader
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
#ifndef _WUD_TOC_H
#define _WUD_TOC_H

#include "wux/wux_common.h"
#include "wux/wux_container.h"
#include <vector>

namespace wux {

// One partition entry from the decrypted TOC.
struct TocPartition {
    char name[0x1A];   // 0x19 chars + NUL
    U64 offset;   // absolute byte offset in the disc image
};

// Decrypts and parses the partition TOC (the decrypted area at 0x18000).
class WudToc {
public:
    // Reads the 0x8000 sector at 0x18000 through the container, decrypts it
    // (AES-CBC, IV = 16 zero bytes) with key, verifies the signature, and
    // fills partitions with the name/offset map.
    Error load(const WuxContainer& container, const U8* key,
               std::vector<TocPartition>& partitions);
};

} // namespace wux

#endif // _WUD_TOC_H
