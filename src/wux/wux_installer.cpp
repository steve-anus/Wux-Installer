/*
 * wuxinstaller - .wux extraction pipeline implementation.
 *
 * Pipeline (per title found in the image):
 *   open .wux + load game.key -> decrypt TOC -> SI partition FST -> per-title
 *   TMD/TIK/CERT -> GM partition header (h3) + FST -> stream raw content to
 *   <outRoot>/<TITLEID>/ in the JWUDTool layout.
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
#include <cstring>

namespace wux {
namespace {

// Read an entire (small) file into out.
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

// Read + decrypt a partition's FST (IV = 16 zero bytes); Fst::parse verifies
// the "FST" signature.
Error readFst(const WuxContainer& c, U64 fstOffset, U32 fstSize,
              const U8* key, Fst& fst) {
    if (fstSize == 0) return Error::Truncated;
    std::vector<U8> raw(fstSize);
    Error e = c.read(fstOffset, fstSize, raw.data());
    if (e != Error::Ok) return e;
    std::vector<U8> dec(fstSize);
    U8 iv[16];
    std::memset(iv, 0, sizeof(iv));
    e = aesCbcDecrypt(key, iv, raw.data(), fstSize, dec.data());
    if (e != Error::Ok) return e;
    return fst.parse(dec.data(), dec.size());
}

// Read the raw 0x20 partition header and verify the CC93A4F5 signature.
Error readPartitionHeader(const WuxContainer& c, U64 offset, U8 header[0x20]) {
    Error e = c.read(offset, 0x20, header);
    if (e != Error::Ok) return e;
    if (std::memcmp(header, fmt::kPartitionSignature, 4) != 0)
        return Error::BadSignature;
    return Error::Ok;
}

// Read + decrypt a file described by an FST entry (TMD/TIK/CERT). Uses the
// offset-derived IV (bytes 8-15 = fileOffset >> 16).
Error getFstFile(const Fst& fst, const WuxContainer& c,
                 U64 partitionOffset, U64 headerSize,
                 const std::string& path, const U8* key,
                 std::vector<U8>& out) {
    const FstEntry* entry = fst.findEntry(path);
    if (!entry) return Error::NotFound;
    const FstContentInfo* info = fst.contentInfo(entry->contentIndex);
    if (!info) return Error::NotFound;

    U64 readOffset = partitionOffset + headerSize + info->offset();
    if (entry->fileSize == 0) { out.clear(); return Error::Ok; }

    std::vector<U8> raw(entry->fileSize);
    Error e = c.read(readOffset, entry->fileSize, raw.data());
    if (e != Error::Ok) return e;

    out.resize(raw.size());
    U8 iv[16];
    makeOffsetIv(entry->fileOffset, iv);
    e = aesCbcDecrypt(key, iv, raw.data(), raw.size(), out.data());
    return e;
}

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

    // 2. Load the 16-byte title key.
    std::vector<U8> key;
    e = readWhole(keyPath, key);
    if (e != Error::Ok) {
        result.error = std::string("open game.key: ") + errorName(e);
        return e;
    }
    if (key.size() != 16) {
        result.error = "game.key must be exactly 16 bytes";
        return Error::MissingKey;
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
    U32 siHeaderSize = readU32BE(siHeader + 0x04);
    U32 siFstSize = readU32BE(siHeader + 0x14);
    Fst siFst;
    e = readFst(container, si->offset + siHeaderSize, siFstSize, key.data(), siFst);
    if (e != Error::Ok) {
        result.error = std::string("SI FST: ") + errorName(e);
        return e;
    }

    // 6. Per-title folders in the SI partition (title.tmd/.tik/.cert live here).
    std::vector<const FstEntry*> titles = siFst.rootDirChildren();
    if (titles.empty()) {
        result.error = "no title folders in SI FST";
        return Error::NotFound;
    }
    const FstEntry* title = titles[0];   // single-title .wux is the common case

    // 6a. TIK / TMD / CERT for this title (decrypted).
    std::vector<U8> tik, tmdBytes, cert;
    e = getFstFile(siFst, container, si->offset, siHeaderSize,
                   title->path + "/title.tik", key.data(), tik);
    if (e != Error::Ok) { result.error = std::string("title.tik: ") + errorName(e); return e; }
    e = getFstFile(siFst, container, si->offset, siHeaderSize,
                   title->path + "/title.tmd", key.data(), tmdBytes);
    if (e != Error::Ok) { result.error = std::string("title.tmd: ") + errorName(e); return e; }
    e = getFstFile(siFst, container, si->offset, siHeaderSize,
                   title->path + "/title.cert", key.data(), cert);
    if (e != Error::Ok) { result.error = std::string("title.cert: ") + errorName(e); return e; }

    // 6b. Parse the TMD.
    Tmd tmd;
    e = parseTmd(tmdBytes.data(), tmdBytes.size(), tmd);
    if (e != Error::Ok) {
        result.error = std::string("TMD: ") + errorName(e);
        return e;
    }

    // 6c. Match the GM partition: name = "GM" + the ticket's title ID.
    std::string gmName = "GM";
    if (tik.size() >= 0x1DC + 8)
        gmName += hexUpper(readU64BE(tik.data() + 0x1DC), 16);
    const TocPartition* gm = nullptr;
    for (size_t i = 0; i < partitions.size(); ++i) {
        if (std::string(partitions[i].name) == gmName) { gm = &partitions[i]; break; }
    }
    if (!gm) {
        result.error = "GM partition not found: " + gmName;
        return Error::NotFound;
    }

    // 6d. GM header (raw, for the h3 region) + GM FST.
    U8 gmHeader[0x20];
    e = readPartitionHeader(container, gm->offset, gmHeader);
    if (e != Error::Ok) {
        result.error = std::string("GM header: ") + errorName(e);
        return e;
    }
    U32 gmHeaderSize = readU32BE(gmHeader + 0x04);
    U32 gmFstSize = readU32BE(gmHeader + 0x14);

    std::vector<U8> gmHeaderRaw(gmHeaderSize);
    e = container.read(gm->offset, gmHeaderSize, gmHeaderRaw.data());
    if (e != Error::Ok) {
        result.error = std::string("GM header raw: ") + errorName(e);
        return e;
    }
    Fst gmFst;
    e = readFst(container, gm->offset + gmHeaderSize, gmFstSize, key.data(), gmFst);
    if (e != Error::Ok) {
        result.error = std::string("GM FST: ") + errorName(e);
        return e;
    }
    U64 gmOrigin = gm->offset + gmHeaderSize;

    // 7. Output folder <outRoot>/<TITLEID>/.
    std::string outDir = std::string(outRoot) + "/" + titleIdHex(tmd.titleID);
    ::mkdir(outDir.c_str(), 0777);   // "already exists" is fine

    // 8. Each content: stream the raw (still-encrypted) bytes to <id8>.app, and
    //    the h3 block to <id8>.h3 when the content is hashed.
    int total = (int)tmd.contents.size();
    for (int i = 0; i < total; ++i) {
        const TmdContent& c = tmd.contents[i];

        U64 contentOffset;
        if (c.index == 0) {
            contentOffset = gmOrigin;   // JNUSLib: index 0 sits at the partition origin
        } else {
            const FstContentInfo* ci = gmFst.contentInfo(c.index);
            if (!ci) {
                result.error = "content index out of range: " + std::to_string(c.index);
                return Error::NotFound;
            }
            contentOffset = gmOrigin + ci->offset();
        }
        U64 appSize = (c.encryptedFileSize + 15) & ~(U64)15;  // align16
        if (appSize == 0) appSize = c.encryptedFileSize;

        std::string id8 = hexUpper(c.id, 8);
        e = streamToFile(container, contentOffset, appSize,
                         (outDir + "/" + id8 + ".app").c_str());
        if (e != Error::Ok) {
            result.error = std::string("write .app: ") + errorName(e);
            return e;
        }

        if (c.hashed) {
            std::vector<U8> h3;
            Error h3e = extractH3(gmHeaderRaw.data(), gmHeaderRaw.size(),
                                  tmd.contents, c.index, h3);
            if (h3e == Error::Ok) {
                e = writeWhole((outDir + "/" + id8 + ".h3").c_str(),
                               h3.data(), h3.size());
                if (e != Error::Ok) {
                    result.error = std::string("write .h3: ") + errorName(e);
                    return e;
                }
            }
        }

        if (progress) progress(i + 1, total, c.id, progressUser);
    }

    // 9. Decrypted metadata.
    e = writeWhole((outDir + "/title.tmd").c_str(), tmdBytes.data(), tmdBytes.size());
    if (e != Error::Ok) { result.error = std::string("write title.tmd: ") + errorName(e); return e; }
    e = writeWhole((outDir + "/title.tik").c_str(), tik.data(), tik.size());
    if (e != Error::Ok) { result.error = std::string("write title.tik: ") + errorName(e); return e; }
    e = writeWhole((outDir + "/title.cert").c_str(), cert.data(), cert.size());
    if (e != Error::Ok) { result.error = std::string("write title.cert: ") + errorName(e); return e; }

    result.ok = true;
    result.titleID = tmd.titleID;
    result.outDir = outDir;
    result.contentCount = total;
    return Error::Ok;
}

} // namespace wux
