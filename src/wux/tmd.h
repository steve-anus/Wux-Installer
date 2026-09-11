/*
 * wuxinstaller - TMD (title metadata) parser and h3 hash extraction.
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
#ifndef _WUX_TMD_H
#define _WUX_TMD_H

#include "wux/wux_common.h"
#include <vector>
#include <string>

namespace wux {

// One content entry from the TMD.
struct TmdContent {
    U32 id                 = 0;  // content ID (8 hex digits -> .app name)
    U16 index              = 0;
    U16 type               = 0;
    U64 encryptedFileSize  = 0;
    bool     hashed            = false;   // type & 0x0002
    bool     encrypted         = false;   // type & 0x0001
};

// Parsed TMD.
struct Tmd {
    U64 titleID      = 0;
    U16 contentCount = 0;
    std::vector<TmdContent> contents;
};

// Parse a DECRYPTED TMD. Validates the Wii U title-ID prefix (0x00050000).
Error parseTmd(const U8* data, size_t len, Tmd& out);

// Extract the .h3 hash block for the content with `index` from the raw GM
// volume header. header is the 0x40-byte volume
// header (h3HashArrayListSize at +0x0C, numberOfH3HashArray at +0x10); h3Region
// is the raw h3 data at (gmOffset + 0x40). On success out holds the .h3.
Error extractH3(const U8* header, const U8* h3Region, size_t h3RegionLen,
                int index, std::vector<U8>& out);

// Format a 64-bit value as `digits` uppercase hex digits (no 0x prefix).
std::string hexUpper(U64 v, int digits);

// Format a 64-bit title ID as 16 uppercase hex digits (the install folder name).
std::string titleIdHex(U64 titleID);

} // namespace wux

#endif // _WUX_TMD_H
