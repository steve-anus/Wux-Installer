/*
 * wuxinstaller - .wux container reader implementation.
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
#include "wux/wux_container.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>

// POSIX off_t carries every disc offset (lseek/SEEK_END at :46 and the U64 cast at :102).
static_assert(sizeof(off_t) >= 8, "off_t must be 64-bit: .wux offsets exceed 2 GiB");

namespace wux {

WuxContainer::WuxContainer()
    : fd_(-1), fileSize_(0), uncompressedSize_(0), sectorSize_(0),
      sectorCount_(0), sectorArrayOffset_(0), indexTable_(nullptr) {}

WuxContainer::~WuxContainer() { close(); }

void WuxContainer::close() {
    if (indexTable_) { std::free(indexTable_); indexTable_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

Error WuxContainer::open(const char* path) {
    close();
    fd_ = ::open(path, O_RDONLY);
    if (fd_ < 0) return Error::IoError;

    // File size (used later to reject out-of-range index entries).
    off_t end = ::lseek(fd_, 0, SEEK_END);
    if (end < 0) { close(); return Error::IoError; }
    fileSize_ = (U64)end;
    ::lseek(fd_, 0, SEEK_SET);

    U8 header[0x20];
    ssize_t n = ::read(fd_, header, sizeof(header));
    if (n != (ssize_t)sizeof(header)) { close(); return Error::Truncated; }

    if (readU32LE(header + 0x00) != 0x30585557 ||
        readU32LE(header + 0x04) != 0x1099D02E) {
        close();
        return Error::BadMagic;
    }
    sectorSize_ = readU32LE(header + 0x08);
    uncompressedSize_ = readU64LE(header + 0x10);
    if (sectorSize_ == 0) { close(); return Error::BadMagic; }

    sectorCount_ = (uncompressedSize_ + sectorSize_ - 1) / sectorSize_;

    // Index table lives at 0x20 (one 4-byte LE entry per sector); the sector
    // array follows, aligned up to a full sector.
    U64 indexTableSize = sectorCount_ * 4;
    sectorArrayOffset_ = 0x20 + indexTableSize;
    sectorArrayOffset_ = (sectorArrayOffset_ + (sectorSize_ - 1)) &
                         ~(U64)(sectorSize_ - 1);

    indexTable_ = (U32*)std::malloc(indexTableSize ? indexTableSize : 1);
    if (!indexTable_) { close(); return Error::AllocError; }

    U64 got = 0;
    while (got < indexTableSize) {
        ssize_t r = ::read(fd_, (U8*)indexTable_ + got, (size_t)(indexTableSize - got));
        if (r <= 0) { close(); return Error::Truncated; }
        got += (U64)r;
    }
    return Error::Ok;
}

Error WuxContainer::read(U64 offset, size_t len, U8* out) const {
    if (fd_ < 0 || !out) return Error::IoError;

    U64 left = len;
    U64 off = offset;
    while (left > 0) {
        U64 secIdx = off / sectorSize_;
        if (secIdx >= sectorCount_) return Error::Truncated;
        U64 secOff = off % sectorSize_;
        U64 chunk = sectorSize_ - secOff;   // bytes remaining in this sector
        if (chunk > left) chunk = left;

        // Map to the real (deduplicated) sector location.
        U32 realSector = readU32LE((const U8*)indexTable_ + secIdx * 4);
        U64 realOff = sectorArrayOffset_ + (U64)realSector * sectorSize_ + secOff;
        if (realOff + chunk > fileSize_) return Error::BadEntryOffset;

        if (::lseek(fd_, (off_t)realOff, SEEK_SET) < 0) return Error::IoError;
        ssize_t r = ::read(fd_, out + (len - left), (size_t)chunk);
        if (r != (ssize_t)chunk) return Error::IoError;

        left -= chunk;
        off += chunk;
    }
    return Error::Ok;
}

} // namespace wux
