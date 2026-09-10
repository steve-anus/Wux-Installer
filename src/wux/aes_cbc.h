/*
 * wuxinstaller - AES-128 CBC decryption (self-contained, no external crypto)
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
#ifndef _WUX_AES_CBC_H
#define _WUX_AES_CBC_H

#include "wux_common.h"

namespace wux {

// The title key (game.key) is 16 bytes, so this is AES-128.
const size_t AesKeySize128 = 16;
const size_t AesBlock      = 16;

// Decrypt inLen bytes of AES-128 CBC ciphertext.
//   key   : 16-byte key
//   iv    : 16-byte initialization vector (16 zero bytes for TOC/FST; the
//           offset-derived IV for TMD/TIK/CERT, see wux::makeOffsetIv)
//   in/out: inLen bytes; in and out must not alias
// Returns Error::Ok, or Error::DecryptError if inLen is not a multiple of 16.
Error aesCbcDecrypt(const U8* key, const U8* iv,
                    const U8* in, size_t inLen, U8* out);

} // namespace wux

#endif // _WUX_AES_CBC_H
