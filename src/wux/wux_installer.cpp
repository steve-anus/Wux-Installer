/*
 * wuxinstaller - .wux extraction pipeline implementation.
 *
 * Pipeline (per title found in the image):
 *   open .wux + load game.key -> decrypt TOC -> SI partition FST -> per-title
 *   TMD/TIK/CERT -> GM partition header (h3) + FST -> stream raw content to
 *   <outRoot>/<TITLEID>/   ;  A disc holds one SI folder (and one GM partition) per title; every title is extracted.
 *
 * FST blobs are read in fixed 64 KiB chunks and each chunk is AES-CBC
 * decrypted with a zero IV, so an FST size that is not a multiple of 16 works.
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
#include "wux/wux_installer.h"
#include "wux/wux_container.h"
#include "wux/wud_toc.h"
#include "wux/fst.h"
#include "wux/tmd.h"
#include "wux/aes_cbc.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <cstring>

namespace wux {
namespace {

// FST blobs are decrypted one 64 KiB chunk at a time; the
// last chunk is truncated to the FST size.
const U64 kFstChunkSize = 0x10000;

// Sanity caps on on-disc (untrusted) sizes before allocating for them.
const U32 kMaxFstSize     = 0x1000000;  // 16 MiB
const U32 kMaxH3ListSize  = 0x8000;     // one sector
const U32 kMaxMetaFileSize = 0x100000;  // 1 MiB (TIK/TMD/CERT are << this)

// Read a whole file into `out`, in 64 KB chunks. Used only for small files (the 16-byte game.key);
// the multi-GB .wux is streamed separately by WuxContainer, never read whole.
Error readWhole(const char* path, std::vector<U8>& out) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return Error::IoError;
    out.clear();
    U8 buf[0x10000];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0) { ::close(fd); return Error::IoError; }
        if (n == 0) break;
        out.insert(out.end(), buf, buf + n);
    }
    ::close(fd);
    return Error::Ok;
}

// Hex digit value (0-15), or -1 if not a hex digit.
int hexVal(U8 c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

// Read a key file into a 16-byte key. Accepts either 16 raw bytes, or a
// 32-char ASCII hex string. ASCII whitespace is ignored. On success key holds exactly 16 bytes.
Error parseKeyFile(const char* path, std::vector<U8>& key) {
    std::vector<U8> raw;
    Error e = readWhole(path, raw);
    if (e != Error::Ok) return e;
    std::vector<U8> clean;
    for (size_t i = 0; i < raw.size(); ++i) {
        U8 b = raw[i];
        if (b == ' ' || b == '\t' || b == '\r' || b == '\n') continue;
        clean.push_back(b);
    }
    key.clear();
    if (clean.size() == 16) {
        key.assign(clean.begin(), clean.end());
        return Error::Ok;
    }
    if (clean.size() == 32) {
        key.resize(16);
        for (int i = 0; i < 16; ++i) {
            int hi = hexVal(clean[2 * i]);
            int lo = hexVal(clean[2 * i + 1]);
            if (hi < 0 || lo < 0) { key.clear(); return Error::MissingKey; }
            key[i] = (U8)((hi << 4) | lo);
        }
        return Error::Ok;
    }
    key.clear();
    return Error::MissingKey;
}

// Case-insensitive ASCII prefix test: does the C string a start with prefix?
bool nameStartsWith(const char* a, const std::string& prefix) {
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (a[i] == '\0') return false;
        char ca = a[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        char cb = prefix[i];
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return true;
}

// Write bytes to a file (creates or truncates).
Error writeWhole(const char* path, const U8* data, size_t len) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return Error::IoError;
    size_t left = len;
    const U8* p = data;
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n <= 0) { ::close(fd); return Error::IoError; }
        p += (size_t)n;
        left -= (size_t)n;
    }
    ::close(fd);
    return Error::Ok;
}

// Create a directory, treating "already exists" as success. Relies on stat
// rather than errno, since the console's mkdir may not set errno reliably.
Error ensureDir(const char* path) {
    struct stat st;
    if (::stat(path, &st) == 0) return Error::Ok;      // already present
    if (::mkdir(path, 0777) == 0) return Error::Ok;     // created
    return ::stat(path, &st) == 0 ? Error::Ok : Error::IoError;
}

// Stream size bytes from the container into a file, one 32 KB sector at a time.
Error streamToFile(const WuxContainer& c, U64 offset, U64 size,
                   const char* path) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return Error::IoError;

    std::vector<U8> buf(fmt::kSectorSize);
    U64 done = 0;
    while (done < size) {
        U64 chunk = size - done;
        if (chunk > buf.size()) chunk = buf.size();
        Error e = c.read(offset + done, (size_t)chunk, buf.data());
        if (e != Error::Ok) { ::close(fd); return e; }
        const U8* p = buf.data();
        size_t left = (size_t)chunk;
        while (left > 0) {
            ssize_t n = ::write(fd, p, left);
            if (n <= 0) { ::close(fd); return Error::IoError; }
            p += (size_t)n;
            left -= (size_t)n;
        }
        done += chunk;
    }
    ::close(fd);
    return Error::Ok;
}

// Read + decrypt a partition's FST into out. 
// full 64 KiB chunks are read from the disc and each chunk is AES-CBC decrypted with a
// zero IV (the on-disc FST is encrypted per 64 KiB chunk), so any FST size
// works, not just multiples of 16. Fst::parse verifies the "FST" signature.
Error readFst(const WuxContainer& c, U64 fstOffset, U32 fstSize,
              const U8* key, std::vector<U8>& out) {
    if (fstSize == 0) return Error::Truncated;
    if (fstSize > kMaxFstSize) return Error::Truncated;
    out.assign(fstSize, 0);

    static const size_t chunk = (size_t)kFstChunkSize;
    std::vector<U8> raw(chunk);
    std::vector<U8> dec(chunk);
    U8 iv[16];
    std::memset(iv, 0, sizeof(iv));

    U64 done = 0;
    while (done < (U64)fstSize) {
        Error e = c.read(fstOffset + done, chunk, raw.data());
        if (e != Error::Ok) return e;
        e = aesCbcDecrypt(key, iv, raw.data(), chunk, dec.data());
        if (e != Error::Ok) return e;
        size_t n = (size_t)(((U64)chunk < (U64)fstSize - done) ? chunk : ((U64)fstSize - done));
        std::memcpy(out.data() + done, dec.data(), n);
        done += n;
    }
    return Error::Ok;
}

// Read + decrypt a GM FST (NUS content 0) as ONE continuous CBC stream: IV =
// 16 zero bytes for the first block, then each block chains from the previous
// ciphertext block across the whole blob. size must be a multiple of 16 (it is
// align16 of the TMD content 0 size). This differs from the SI FST, which is
// independent 64 KiB blocks (see readFst).
Error readFstChained(const WuxContainer& c, U64 fstOffset, U32 size,
                     const U8* key, std::vector<U8>& out) {
    if (size == 0) return Error::Truncated;
    if (size > kMaxFstSize) return Error::Truncated;
    if (size % 16 != 0) return Error::DecryptError;

    std::vector<U8> raw(size);
    Error e = c.read(fstOffset, size, raw.data());
    if (e != Error::Ok) return e;

    out.resize(size);
    U8 iv[16];
    std::memset(iv, 0, sizeof(iv));
    return aesCbcDecrypt(key, iv, raw.data(), size, out.data());
}

// Read the raw partition header (first 0x20 bytes) and verify the CC93A4F5
// signature. On success the caller uses:
//   +0x04 blockSize (volume block size), +0x0C h3HashArrayListSize,
//   +0x10 numberOfH3HashArray, +0x14 FSTSize, +0x18 FSTAddress (in volume
//   blocks).
Error readPartitionHeader(const WuxContainer& c, U64 offset, U8 header[0x20]) {
    Error e = c.read(offset, 0x20, header);
    if (e != Error::Ok) return e;
    if (std::memcmp(header, fmt::kPartitionSignature, 4) != 0)
        return Error::BadSignature;
    return Error::Ok;
}

// The FST blob sits at volumeStart + FSTAddress * blockSize (header fields
// +0x18 / +0x04).
U64 fstOffset(const U8 header[0x20], U64 volumeStart) {
    U32 blockSize = readU32BE(header + 0x04);
    if (blockSize == 0) blockSize = fmt::kSectorSize;
    U32 fstAddress = readU32BE(header + 0x18);
    return volumeStart + (U64)fstAddress * blockSize;
}

// Read + decrypt a file described by an FST entry (TMD/TIK/CERT). On-disc
// offset = partitionStart + section(sectionNumber).address * volBlockSize +
// entryOffset, where entryOffset = addrBlocks * sectionBlockSize; the IV is
// (entryOffset >> 16) in bytes 8-15.
Error getFstFile(const Fst& fst, const WuxContainer& c,
                 U64 partitionStart, U32 volBlockSize,
                 const std::string& path, const U8* key,
                 std::vector<U8>& out) {
    const FstEntry* entry = fst.findEntry(path);
    if (!entry || entry->isDir || entry->isLink) return Error::NotFound;
    const FstSection* sec = fst.section(entry->sectionNumber);
    if (!sec) return Error::NotFound;
    if (entry->fileSize > kMaxMetaFileSize) return Error::Truncated;
    if (entry->fileSize == 0) { out.clear(); return Error::Ok; }

    // The section's bytes are encrypted in 64 KiB blocks; block N (byte offset
    // N*0x10000 within the section) is decrypted with IV = N (see makeOffsetIv).
    // The file sits at `entryOffset` bytes into the section, so we read the
    // enclosing 64 KiB block, decrypt the whole block, and copy the file's portion out of it.
    const U64 sectionStart = partitionStart + (U64)sec->address * volBlockSize;
    const U64 fileSize     = entry->fileSize;
    const U64 BLOCK        = kFstChunkSize;   // 0x10000

    out.assign(fileSize, 0);
    std::vector<U8> raw(BLOCK);
    std::vector<U8> dec(BLOCK);

    U64 remaining = fileSize;
    U64 pos       = entry->offset(fst.sectionBlockSize);   // byte offset in section
    while (remaining > 0) {
        const U64 blockIndex  = pos / BLOCK;
        const U64 blockOffset = pos % BLOCK;
        const U64 readOffset  = sectionStart + blockIndex * BLOCK;

        U8 iv[16];
        makeOffsetIv(pos, iv);   // IV = (pos >> 16) == blockIndex for block-aligned pos

        Error e = c.read(readOffset, (size_t)BLOCK, raw.data());
        if (e != Error::Ok) return e;
        e = aesCbcDecrypt(key, iv, raw.data(), (size_t)BLOCK, dec.data());
        if (e != Error::Ok) return e;

        U64 n = remaining;
        if (n > BLOCK - blockOffset) n = BLOCK - blockOffset;
        std::memcpy(out.data() + (fileSize - remaining), dec.data() + blockOffset, (size_t)n);

        remaining -= n;
        pos += n;
    }
    return Error::Ok;
}

// Everything known about one title before its content is streamed.
struct TitleData {
    const FstEntry* dir = nullptr;   // the <titleid>/ dir in the SI FST
    std::string outDir;
    std::vector<U8> tik, tmdBytes, cert;
    Tmd tmd;
    const TocPartition* gm = nullptr;
    U8 gmHeader[0x20];
    U32 gmBlockSize = 0;
    std::vector<U8> gmH3Region;
    Fst gmFst;
    bool hasGmFst = false;
    bool error = false;
    Error code = Error::Ok;
    std::string errorText;
};

} // namespace

Error WuxInstaller::extract(const char* wuxPath, const char* keyPath,
                            const char* outRoot, ExtractResult& result,
                            ProgressFn progress, void* progressUser) {
    result = ExtractResult();

    // 1. Open the .wux container.
    WuxContainer container;
    Error e = container.open(wuxPath);
    if (e != Error::Ok) {
        result.error = std::string("open .wux: ") + errorName(e);
        return e;
    }

    // 2. Load the 16-byte title key (16 raw bytes, or a 32-char hex string).
    std::vector<U8> key;
    e = parseKeyFile(keyPath, key);
    if (e != Error::Ok) {
        result.error = std::string("read game.key: ") + errorName(e) +
                       " (must be 16 raw bytes or a 32-char hex string)";
        return e;
    }

    // 3. Decrypt + parse the partition TOC.
    WudToc toc;
    std::vector<TocPartition> partitions;
    e = toc.load(container, key.data(), partitions);
    if (e != Error::Ok) {
        result.error = std::string("TOC: ") + errorName(e);
        return e;
    }

    // 4. Locate the SI (system) partition.
    const TocPartition* si = nullptr;
    for (size_t i = 0; i < partitions.size(); ++i) {
        if (partitions[i].name[0] == 'S' && partitions[i].name[1] == 'I') {
            si = &partitions[i];
            break;
        }
    }
    if (!si) {
        result.error = "SI partition not found";
        return Error::NotFound;
    }

    // 5. SI partition header + FST.
    U8 siHeader[0x20];
    e = readPartitionHeader(container, si->offset, siHeader);
    if (e != Error::Ok) {
        result.error = std::string("SI header: ") + errorName(e);
        return e;
    }
    U32 siBlockSize = readU32BE(siHeader + 0x04);
    if (siBlockSize == 0) siBlockSize = fmt::kSectorSize;
    U32 siFstSize = readU32BE(siHeader + 0x14);
    std::vector<U8> siFstBytes;
    e = readFst(container, fstOffset(siHeader, si->offset), siFstSize,
                key.data(), siFstBytes);
    if (e != Error::Ok) {
        result.error = std::string("SI FST: ") + errorName(e);
        return e;
    }
    Fst siFst;
    e = siFst.parse(siFstBytes.data(), siFstBytes.size());
    if (e != Error::Ok) {
        result.error = std::string("SI FST parse: ") + errorName(e);
        return e;
    }

    // 6. Per-title folders in the SI partition (title.tmd/.tik/.cert live
    //    here); a disc can hold several titles.
    std::vector<const FstEntry*> titleDirs = siFst.rootDirChildren();
    if (titleDirs.empty()) {
        result.error = "no title folders in SI FST";
        return Error::NotFound;
    }

    // Pass 1: read + parse the metadata of every title.
    std::vector<TitleData> titles(titleDirs.size());
    for (size_t t = 0; t < titleDirs.size(); ++t) {
        TitleData& td = titles[t];
        td.dir = titleDirs[t];
        td.outDir = std::string(outRoot) + "/" + td.dir->name;
        auto fail = [&td](Error err, const std::string& msg) {
            td.error = true;
            td.code = err;
            td.errorText = msg;
        };

        // 6a. TIK / TMD / CERT for this title (decrypted).
        e = getFstFile(siFst, container, si->offset, siBlockSize,
                       td.dir->path + "/title.tik", key.data(), td.tik);
        if (e != Error::Ok) { fail(e, "title.tik: " + std::string(errorName(e))); continue; }
        e = getFstFile(siFst, container, si->offset, siBlockSize,
                       td.dir->path + "/title.tmd", key.data(), td.tmdBytes);
        if (e != Error::Ok) { fail(e, "title.tmd: " + std::string(errorName(e))); continue; }
        e = getFstFile(siFst, container, si->offset, siBlockSize,
                       td.dir->path + "/title.cert", key.data(), td.cert);
        if (e != Error::Ok) { fail(e, "title.cert: " + std::string(errorName(e))); continue; }

        // 6b. Parse the TMD.
        e = parseTmd(td.tmdBytes.data(), td.tmdBytes.size(), td.tmd);
        if (e != Error::Ok) { fail(e, "TMD: " + std::string(errorName(e))); continue; }
        // Name the output folder by the TMD title ID (16 uppercase hex digits).
        td.outDir = std::string(outRoot) + "/" + titleIdHex(td.tmd.titleID);

        // 6c. Match the GM partition: name = "GM" + the ticket's title ID
        // (compared case-insensitively, since the on-disc casing can vary).
        std::string gmName = "GM";
        if (td.tik.size() >= 0x1DC + 8)
            gmName += hexUpper(readU64BE(td.tik.data() + 0x1DC), 16);
        const TocPartition* gm = nullptr;
        for (size_t i = 0; i < partitions.size(); ++i) {
            if (nameStartsWith(partitions[i].name, gmName)) { gm = &partitions[i]; break; }
        }
        if (!gm) { fail(Error::NotFound, "GM partition not found: " + gmName); continue; }
        td.gm = gm;

        // 6d. GM header (raw, for the h3 region) + GM FST.
        e = readPartitionHeader(container, gm->offset, td.gmHeader);
        if (e != Error::Ok) { fail(e, "GM header: " + std::string(errorName(e))); continue; }
        td.gmBlockSize = readU32BE(td.gmHeader + 0x04);
        if (td.gmBlockSize == 0) td.gmBlockSize = fmt::kSectorSize;

        U32 gmH3ListSize = readU32BE(td.gmHeader + 0x0C);
        if (gmH3ListSize > kMaxH3ListSize) {
            fail(Error::Truncated, "GM h3 list size too large");
            continue;
        }
        if (gmH3ListSize > 0) {
            td.gmH3Region.resize(gmH3ListSize);
            e = container.read(gm->offset + 0x40, gmH3ListSize, td.gmH3Region.data());
            if (e != Error::Ok) {
                fail(e, "GM h3 region: " + std::string(errorName(e)));
                continue;
            }
        }

        // The GM FST is NUS content 0: its size is align16(TMD content 0), and
        // it is a single continuous CBC stream. If the TMD has no content 0,
        // fall back to the volume-header FSTSize.
        U64 gmFstSize = 0;
        for (size_t i = 0; i < td.tmd.contents.size(); ++i)
            if (td.tmd.contents[i].index == 0) {
                gmFstSize = (td.tmd.contents[i].encryptedFileSize + 15) & ~(U64)15;
                break;
            }
        if (gmFstSize == 0) gmFstSize = readU32BE(td.gmHeader + 0x14);
        if (gmFstSize > 0) {
            std::vector<U8> gmFstBytes;
            e = readFstChained(container, fstOffset(td.gmHeader, gm->offset),
                               (U32)gmFstSize, key.data(), gmFstBytes);
            if (e != Error::Ok) { fail(e, "GM FST: " + std::string(errorName(e))); continue; }
            e = td.gmFst.parse(gmFstBytes.data(), gmFstBytes.size());
            if (e != Error::Ok) { fail(e, "GM FST parse: " + std::string(errorName(e))); continue; }
            td.hasGmFst = true;
        }
        if (td.tmd.contentCount > 1 && !td.hasGmFst &&
            td.tmd.contents.size() > 1) {
            fail(Error::Truncated, "GM FST missing but the TMD has content beyond index 0");
            continue;
        }
    }

    // Count the good titles; if none, report the first error.
    int goodTitles = 0;
    for (size_t t = 0; t < titles.size(); ++t)
        if (!titles[t].error) ++goodTitles;
    if (goodTitles == 0) {
        const TitleData& first = titles[0];
        result.error = std::string(titles[0].dir->name) + ": " +
                       (first.errorText.empty() ? std::string(errorName(first.code))
                                                : first.errorText);
        return first.code;
    }

    // Create the install root (e.g. /install) first: mkdir does not create
    // intermediate directories.
    Error dirErr = ensureDir(outRoot);
    if (dirErr != Error::Ok) {
        result.error = std::string("create ") + outRoot + ": " + errorName(dirErr);
        return dirErr;
    }

    // Pass 2: stream the content of every good title.
    int totalContents = 0;
    for (size_t t = 0; t < titles.size(); ++t)
        if (!titles[t].error) totalContents += (int)titles[t].tmd.contents.size();
    int doneContents = 0;

    for (size_t t = 0; t < titles.size(); ++t) {
        TitleData& td = titles[t];
        if (td.error) continue;

        dirErr = ensureDir(td.outDir.c_str());
        if (dirErr != Error::Ok) {
            td.error = true;
            td.code = dirErr;
            td.errorText = std::string("create ") + td.outDir + ": " + errorName(dirErr);
            continue;
        }

        int total = (int)td.tmd.contents.size();
        for (int i = 0; i < total; ++i) {
            const TmdContent& c = td.tmd.contents[i];

            U64 contentOffset;
            if (c.index == 0) {
                // content 0 (the FST) sits at FSTAddress * blockSize.
                contentOffset = td.gm->offset + (U64)readU32BE(td.gmHeader + 0x18) *
                                td.gmBlockSize;
            } else {
                const FstSection* sec = td.hasGmFst ? td.gmFst.section(c.index)
                                                     : nullptr;
                if (!sec) {
                    td.error = true;
                    td.code = Error::NotFound;
                    td.errorText = "content index out of range: " + std::to_string(c.index);
                    break;
                }
                contentOffset = td.gm->offset + (U64)sec->address * td.gmBlockSize;
            }
            U64 appSize = (c.encryptedFileSize + 15) & ~(U64)15;  // align16
            if (appSize == 0) appSize = c.encryptedFileSize;
            if (contentOffset + appSize > container.uncompressedSize()) {
                td.error = true;
                td.code = Error::Truncated;
                td.errorText = "content runs past end of image: " + std::to_string(c.id);
                break;
            }

            std::string id8 = hexUpper(c.id, 8);
            e = streamToFile(container, contentOffset, appSize,
                             (td.outDir + "/" + id8 + ".app").c_str());
            if (e != Error::Ok) {
                td.error = true;
                td.code = e;
                td.errorText = std::string("write .app: ") + errorName(e);
                break;
            }

            if (c.hashed) {
                std::vector<U8> h3;
                Error h3e = extractH3(td.gmHeader, td.gmH3Region.data(),
                                      td.gmH3Region.size(), c.index, h3);
                if (h3e == Error::Ok) {
                    e = writeWhole((td.outDir + "/" + id8 + ".h3").c_str(),
                                   h3.data(), h3.size());
                    if (e != Error::Ok) {
                        td.error = true;
                        td.code = e;
                        td.errorText = std::string("write .h3: ") + errorName(e);
                        break;
                    }
                }
            }

            if (progress) progress(doneContents + i + 1, totalContents, c.id, progressUser);
            doneContents++;
        }
        if (td.error) continue;

        // 9. Decrypted metadata.
        e = writeWhole((td.outDir + "/title.tmd").c_str(), td.tmdBytes.data(), td.tmdBytes.size());
        if (e != Error::Ok) {
            td.error = true; td.code = e;
            td.errorText = std::string("write title.tmd: ") + errorName(e);
            continue;
        }
        e = writeWhole((td.outDir + "/title.tik").c_str(), td.tik.data(), td.tik.size());
        if (e != Error::Ok) {
            td.error = true; td.code = e;
            td.errorText = std::string("write title.tik: ") + errorName(e);
            continue;
        }
        e = writeWhole((td.outDir + "/title.cert").c_str(), td.cert.data(), td.cert.size());
        if (e != Error::Ok) {
            td.error = true; td.code = e;
            td.errorText = std::string("write title.cert: ") + errorName(e);
            continue;
        }

        if (result.titleID == 0) {
            result.titleID = td.tmd.titleID;
            result.outDir = td.outDir;
        }
        result.outDirs.push_back(td.outDir);
        result.titleCount++;
        result.contentCount += total;
    }

    if (result.titleCount == 0) {
        // All titles failed in pass 2 (pass 1 was OK).
        for (size_t t = 0; t < titles.size(); ++t) {
            const TitleData& td = titles[t];
            if (td.error) {
                result.error = std::string(td.dir->name) + ": " + td.errorText;
                return td.code;
            }
        }
        result.error = "no titles extracted";
        return Error::Unknown;
    }

    result.ok = true;
    return Error::Ok;
}

} // namespace wux
