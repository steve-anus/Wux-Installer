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
#ifndef _WUX_KEY_FILE_H
#define _WUX_KEY_FILE_H

#include "wux/wux_common.h"

#include <string>
#include <vector>

namespace wux {

// Read a whole small file into out. maxBytes caps the accepted size
// (0 = unlimited): a larger file is rejected with Error::Truncated and, when
// tooLarge is given, reported through it.
Error readWhole(const char* path, std::vector<U8>& out,
                U64 maxBytes = 0, bool* tooLarge = nullptr);

// Turn a key file's bytes into a 16-byte key. Exactly 16 bytes are the raw
// key, unless all 16 are hex digits (then most likely a truncated hex key,
// refused). Otherwise ASCII whitespace is dropped and 32 hex digits are
// parsed. reason, when given, gets a human-readable cause on failure.
Error parseKeyBytes(const std::vector<U8>& raw, std::vector<U8>& key,
                    std::string* reason = nullptr);

// readWhole (4096-byte cap) followed by parseKeyBytes.
Error parseKeyFile(const char* path, std::vector<U8>& key,
                   std::string* reason = nullptr);

} // namespace wux

#endif // _WUX_KEY_FILE_H
