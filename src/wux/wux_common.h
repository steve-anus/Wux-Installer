/*
 * wuxinstaller - native .wux disc image installer for the Wii U.
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
#ifndef _WUX_COMMON_H
#define _WUX_COMMON_H

#include <cstdint>
#include <cstddef>
#include <cstring>

#define U8 uint8_t
#define U16 uint16_t
#define U32 uint32_t
#define U64 uint64_t

namespace wux {

enum class Error {
    Ok = 0,
    Unknown,
    BadMagic,        // .wux header magic mismatch
    BadSignature,    // TOC / partition / FST signature mismatch
    Truncated,       // file shorter than a declared length
    BadEntryOffset,  // index-table entry points outside the sector array
    IoError,         // open / read / write / seek failed
    AllocError,      // out of memory
    DecryptError,    // AES failure (bad key or length not a multiple of 16)
    NotFound,        // file or partition not present
    MissingKey,      // game.key absent or not 16 bytes
    NoSpace,         // not enough free space for the extracted files
    NotSupported,
    Cancelled        // streaming stopped on request (ProgressFn returned false)
};

inline const char* errorName(Error e) {
    switch (e) {
        case Error::Ok:             return "ok";
        case Error::BadMagic:      return "bad .wux magic";
        case Error::BadSignature:  return "bad signature";
        case Error::Truncated:     return "truncated";
        case Error::BadEntryOffset:return "bad index entry";
        case Error::IoError:       return "i/o error";
        case Error::AllocError:    return "alloc error";
        case Error::DecryptError:  return "decrypt error";
        case Error::NotFound:      return "not found";
        case Error::MissingKey:    return "key not usable";
        case Error::NoSpace:       return "not enough free space";
        case Error::NotSupported:  return "a same-named file exists on the card";
        case Error::Cancelled:     return "cancelled";
        default:                   return "unknown";
    }
}

// ---- byte readers ----------------------------------------------------------
// The .wux header and index table are little-endian. The disc structures
// (TOC, partition header, FST, TMD) are big-endian. These assemble the value
// from the raw bytes, so they are correct on any host endianness.

inline U32 readU32LE(const U8* p) {
    return (U32)p[0] | ((U32)p[1] << 8) |
           ((U32)p[2] << 16) | ((U32)p[3] << 24);
}
inline U64 readU64LE(const U8* p) {
    return (U64)readU32LE(p) | ((U64)readU32LE(p + 4) << 32);
}
inline U32 readU32BE(const U8* p) {
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) |
           ((U32)p[2] << 8) | (U32)p[3];
}
inline U16 readU16BE(const U8* p) {
    return (U16)(((U16)p[0] << 8) | (U16)p[1]);
}
inline U64 readU64BE(const U8* p) {
    return ((U64)readU32BE(p) << 32) | (U64)readU32BE(p + 4);
}

// Build the offset-derived CBC IV used for TMD/TIK/CERT (and hashed content):
// bytes 0-7 are zero, bytes 8-15 hold (fileOffsetBytes >> 16) big-endian.
// fileOffsetBytes is the FST entry's file offset in bytes.
inline void makeOffsetIv(U64 fileOffsetBytes, U8 iv[16]) {
    std::memset(iv, 0, 16);
    U64 v = fileOffsetBytes >> 16;
    for (int i = 0; i < 8; ++i)
        iv[8 + i] = (U8)(v >> (8 * (7 - i)));
}

// ---- fixed format constants -----
namespace fmt {
    const U32 kSectorSize     = 0x8000;  // 32 KiB
    const U32 kTocOffset      = 0x18000; // decrypted-area sector
    const U32 kTocEntryOffset = 0x800;   // partition table start (in TOC block)
    const U32 kTocEntrySize   = 0x80;    // bytes per partition entry

    // Signatures, compared as raw byte sequences
    const U8 kTocSignature[4]       = { 0xCC, 0xA6, 0xE6, 0x7B };
    const U8 kPartitionSignature[4] = { 0xCC, 0x93, 0xA4, 0xF5 };
    const U8 kFstSignature[4]       = { 0x46, 0x53, 0x54, 0x00 }; // "FST\0"
}

} // namespace wux

#endif // _WUX_COMMON_H
