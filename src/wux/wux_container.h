/*
 * wuxinstaller - .wux (compressed .wud) container reader.
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
#ifndef _WUX_CONTAINER_H
#define _WUX_CONTAINER_H

#include "wux/wux_common.h"

namespace wux {

// Streams a .wux disc image (compressed .wud) without materializing the
// full image. The index table (up to ~3 MB) is held in memory; each read maps a
// virtual offset to the on-disk sector array and reads straight to the caller.
class WuxContainer {
public:
    WuxContainer();
    ~WuxContainer();

    // Open a .wux file, validate the header, and load the index table.
    Error open(const char* path);
    void close();

    // Read len bytes of the virtual (uncompressed) disc image starting at
    // offset. May cross 32 KB sector boundaries; offset+len must stay within
    // the image.
    Error read(U64 offset, size_t len, U8* out) const;

    U64 uncompressedSize() const { return uncompressedSize_; }
    U64 sectorCount() const { return sectorCount_; }
    bool isOpen() const { return fd_ >= 0; }

private:
    int fd_;
    U64 fileSize_;
    U64 uncompressedSize_;
    U32 sectorSize_;
    U64 sectorCount_;
    U64 sectorArrayOffset_;  // byte offset of the sector array
    U32* indexTable_;        // [sectorCount_] sector indices (malloc'd)
};

} // namespace wux

#endif // _WUX_CONTAINER_H
