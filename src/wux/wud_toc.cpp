/*
 * wuxinstaller - partition TOC reader implementation
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
#include "wux/wud_toc.h"
#include "wux/aes_cbc.h"

namespace wux {

Error WudToc::load(const WuxContainer& container, const U8* key,
                   std::vector<TocPartition>& partitions) {
    partitions.clear();

    // One 32 KB sector. Static so we do not put 64 KB on the stack.
    static U8 enc[fmt::kSectorSize];
    static U8 dec[fmt::kSectorSize];

    Error e = container.read(fmt::kTocOffset, sizeof(enc), enc);
    if (e != Error::Ok) return e;

    // The TOC is AES-CBC decrypted with a 16-zero-byte IV.
    U8 iv[16];
    std::memset(iv, 0, sizeof(iv));
    e = aesCbcDecrypt(key, iv, enc, sizeof(enc), dec);
    if (e != Error::Ok) return e;

    if (std::memcmp(dec, fmt::kTocSignature, 4) != 0)
        return Error::BadSignature;

    U32 partitionCount = readU32BE(dec + 0x1C);
    if (partitionCount == 0)
        return Error::NotFound;

    // The 32 KiB TOC sector holds exactly 240 entries; an untrusted
    // partitionCount beyond that would read past the end of dec[].
    const U32 kMaxPartitions =
        (U32)((sizeof(dec) - fmt::kTocEntryOffset) / fmt::kTocEntrySize);
    if (partitionCount > kMaxPartitions)
        return Error::Truncated;

    for (U32 i = 0; i < partitionCount; ++i) {
        const U8* entry =
            dec + fmt::kTocEntryOffset + (size_t)i * fmt::kTocEntrySize;

        TocPartition p;
        std::memset(&p, 0, sizeof(p));

        // Partition name: up to 0x19 bytes, null-terminated.
        int j = 0;
        while (j < 0x19 && entry[j] != 0) ++j;
        std::memcpy(p.name, entry, (size_t)j);
        p.name[j] = 0;

        // Sector index -> absolute byte offset in the disc image.
        p.offset = (U64)readU32BE(entry + 0x20) * fmt::kSectorSize;
        partitions.push_back(p);
    }
    return Error::Ok;
}

} // namespace wux
