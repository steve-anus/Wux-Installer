/*
 * wuxinstaller - reading the 16-byte key files (game.key, common.key).
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
#include "wux/key_file.h"
#include "wux/cleanup_guard.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace wux {
namespace {

// Hex digit value (0-15), or -1 if not a hex digit.
int hexVal(U8 c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

bool allHex(const std::vector<U8>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        if (hexVal(v[i]) < 0) return false;
    return true;
}

const char* const kTruncatedHex =
    "16 hex characters - looks like a truncated 32-char hex key "
    "(enter all 32 characters, or save the raw 16 bytes)";

} // namespace

Error readWhole(const char* path, std::vector<U8>& out,
                U64 maxBytes, bool* tooLarge) {
    // Heap buffer, taken before the open so a failed allocation leaves no
    // file open; this runs on the 192 KiB worker thread's stack (WuxExtractThread).
    std::vector<U8> buf(0x10000);
    int fd = ::open(path, O_RDONLY);
    if (fd < 0)
    {
        // Missing vs. fault, decided via stat: this platform's POSIX layer
        // does not set errno reliably, so stat is the referee.
        struct stat probe;
        return (::stat(path, &probe) != 0) ? Error::NotFound : Error::IoError;
    }
    FileGuard file(fd);   // closed on every exit, a throwing insert included
    out.clear();
    for (;;) {
        ssize_t n = ::read(file.fd(), buf.data(), buf.size());
        if (n < 0) return Error::IoError;
        if (n == 0) break;
        if (maxBytes != 0 && (U64)out.size() + (U64)n > maxBytes) {
            out.clear();
            if (tooLarge) *tooLarge = true;
            return Error::Truncated;
        }
        out.insert(out.end(), buf.data(), buf.data() + n);
    }
    return Error::Ok;
}

Error parseKeyBytes(const std::vector<U8>& raw, std::vector<U8>& key,
                    std::string* reason) {
    key.clear();
    // Exactly 16 bytes are the raw key as stored: a raw key may contain the
    // bytes of spaces, tabs or line breaks, so nothing is dropped first.
    if (raw.size() == 16) {
        if (allHex(raw)) {
            if (reason) *reason = kTruncatedHex;
            return Error::MissingKey;
        }
        key = raw;
        return Error::Ok;
    }
    std::vector<U8> clean;
    for (size_t i = 0; i < raw.size(); ++i) {
        U8 b = raw[i];
        if (b == ' ' || b == '\t' || b == '\r' || b == '\n') continue;
        clean.push_back(b);
    }
    if (clean.size() == 16) {
        if (allHex(clean)) {
            if (reason) *reason = kTruncatedHex;
            return Error::MissingKey;
        }
        key = clean;
        return Error::Ok;
    }
    if (clean.size() == 32) {
        key.resize(16);
        for (int i = 0; i < 16; ++i) {
            int hi = hexVal(clean[2 * i]);
            int lo = hexVal(clean[2 * i + 1]);
            if (hi < 0 || lo < 0) {
                key.clear();
                if (reason) *reason = "32 characters but not valid hex";
                return Error::MissingKey;
            }
            key[i] = (U8)((hi << 4) | lo);
        }
        return Error::Ok;
    }
    if (reason) *reason = "expected 16 raw bytes or a 32-character hex string, got " + std::to_string(clean.size()) + " byte(s)";
    return Error::MissingKey;
}

Error parseKeyFile(const char* path, std::vector<U8>& key,
                   std::string* reason) {
    std::vector<U8> raw;
    bool tooLarge = false;
    Error e = readWhole(path, raw, 4096, &tooLarge);
    if (e != Error::Ok) {
        key.clear();
        if (reason) {
            if (tooLarge) *reason = "file too large";
            else if (e == Error::NotFound) *reason = "file not found";
            else if (e == Error::IoError) *reason = "read error";
        }
        return e;
    }
    return parseKeyBytes(raw, key, reason);
}

} // namespace wux
